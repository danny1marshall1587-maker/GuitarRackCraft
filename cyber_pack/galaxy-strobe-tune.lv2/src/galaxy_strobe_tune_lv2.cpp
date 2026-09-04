#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "lv2/lv2.h"
#include "StrobeTunerCore.hpp"

#define GALAXY_STROBE_TUNE_URI "http://moddevices.com/plugins/danny/galaxy-strobe-tune"

enum PortIndex {
    PORT_AUDIO_IN_L      = 0,
    PORT_AUDIO_IN_R      = 1,
    PORT_AUDIO_OUT_L     = 2,
    PORT_AUDIO_OUT_R     = 3,
    PORT_BYPASS          = 4,
    PORT_MUTE_MODE       = 5,
    PORT_REF_A           = 6,
    PORT_PROFILE         = 7,
    PORT_CAPO            = 8,
    PORT_STABILITY       = 9,
    PORT_INPUT_GAIN      = 10,
    PORT_DETECTED_FREQ   = 11,
    PORT_CENTS_OFFSET    = 12,
    PORT_DETECTED_NOTE   = 13,
    PORT_IN_TUNE         = 14,
    PORT_SIGNAL_LEVEL    = 15
};

struct GalaxyStrobeTuneLV2 {
    // Audio ports
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;

    // Control input ports
    const float* bypass;
    const float* mute_mode;
    const float* ref_a;
    const float* profile;
    const float* capo;
    const float* stability;
    const float* input_gain;

    // Control output ports
    float*       detected_freq;
    float*       cents_offset;
    float*       detected_note;
    float*       in_tune;
    float*       signal_level;

    // DSP Engine & temporary mono buffer
    HeadRushDSP::StrobeTunerCore engine;
    std::vector<float> monoMixBuffer;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor*     descriptor,
                             double                    sample_rate,
                             const char*               bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;
    (void)features;

    GalaxyStrobeTuneLV2* self = new (std::nothrow) GalaxyStrobeTuneLV2();
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);
    self->monoMixBuffer.assign(4096, 0.0f);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    GalaxyStrobeTuneLV2* self = (GalaxyStrobeTuneLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_L:
            self->inL = (const float*)data_location;
            break;
        case PORT_AUDIO_IN_R:
            self->inR = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT_L:
            self->outL = (float*)data_location;
            break;
        case PORT_AUDIO_OUT_R:
            self->outR = (float*)data_location;
            break;
        case PORT_BYPASS:
            self->bypass = (const float*)data_location;
            break;
        case PORT_MUTE_MODE:
            self->mute_mode = (const float*)data_location;
            break;
        case PORT_REF_A:
            self->ref_a = (const float*)data_location;
            break;
        case PORT_PROFILE:
            self->profile = (const float*)data_location;
            break;
        case PORT_CAPO:
            self->capo = (const float*)data_location;
            break;
        case PORT_STABILITY:
            self->stability = (const float*)data_location;
            break;
        case PORT_INPUT_GAIN:
            self->input_gain = (const float*)data_location;
            break;
        case PORT_DETECTED_FREQ:
            self->detected_freq = (float*)data_location;
            break;
        case PORT_CENTS_OFFSET:
            self->cents_offset = (float*)data_location;
            break;
        case PORT_DETECTED_NOTE:
            self->detected_note = (float*)data_location;
            break;
        case PORT_IN_TUNE:
            self->in_tune = (float*)data_location;
            break;
        case PORT_SIGNAL_LEVEL:
            self->signal_level = (float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    GalaxyStrobeTuneLV2* self = (GalaxyStrobeTuneLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    GalaxyStrobeTuneLV2* self = (GalaxyStrobeTuneLV2*)instance;
    if (!self || sample_count == 0) return;

    // Resolve audio input buffers with mono fallback
    const float* inL = self->inL;
    const float* inR = self->inR;

    if (!inL && !inR) {
        if (self->outL) std::memset(self->outL, 0, sample_count * sizeof(float));
        if (self->outR) std::memset(self->outR, 0, sample_count * sizeof(float));
        return;
    }

    if (!inL) inL = inR;
    if (!inR) inR = inL;

    float* outL = self->outL ? self->outL : (float*)inL;
    float* outR = self->outR ? self->outR : outL;

    // Set input gain multiplier
    float inGain = (self->input_gain && *self->input_gain > 0.0f) ? *self->input_gain : 1.0f;
    self->engine.setInputGain(inGain);

    // 1. Continuous Pitch Detection with AGC
    if (self->monoMixBuffer.size() < sample_count) {
        self->monoMixBuffer.resize(sample_count);
    }

    // Mix Left & Right to mono for tracking
    for (uint32_t i = 0; i < sample_count; ++i) {
        self->monoMixBuffer[i] = 0.5f * (inL[i] + inR[i]);
    }

    self->engine.pushSamples(self->monoMixBuffer.data(), sample_count);

    float refA = (self->ref_a && *self->ref_a > 300.0f) ? *self->ref_a : 440.0f;
    int profIdx = self->profile ? static_cast<int>(*self->profile + 0.5f) : 0;
    int capoVal = self->capo ? static_cast<int>(*self->capo + 0.5f) : 0;
    float stabVal = self->stability ? *self->stability : 0.99f;

    self->engine.updateTuningCalculations(refA, profIdx, capoVal, stabVal);

    // 2. Audio Output Routing (Silent Stage Mute when active, or transparent passthrough)
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    bool muteActive = (!isBypassed) && (self->mute_mode && *self->mute_mode >= 0.5f);

    if (muteActive) {
        if (self->outL) std::memset(self->outL, 0, sample_count * sizeof(float));
        if (self->outR) std::memset(self->outR, 0, sample_count * sizeof(float));
    } else {
        if (self->outL && self->outL != inL) std::memcpy(self->outL, inL, sample_count * sizeof(float));
        if (self->outR && self->outR != inR) std::memcpy(self->outR, inR, sample_count * sizeof(float));
    }

    // 3. Report Live Pitch & Signal Level Telemetry
    float freq = self->engine.getDetectedFrequency();
    float cents = self->engine.getCentsDeviation();
    int noteIdx = self->engine.getDetectedNoteIndex();
    bool inTune = self->engine.isInTune();
    float sigLevel = self->engine.getSignalLevel();

    if (self->detected_freq) {
        *self->detected_freq = freq;
    }
    if (self->cents_offset) {
        *self->cents_offset = (noteIdx >= 0) ? cents : 0.0f;
    }
    if (self->detected_note) {
        *self->detected_note = static_cast<float>(noteIdx);
    }
    if (self->in_tune) {
        *self->in_tune = inTune ? 1.0f : 0.0f;
    }
    if (self->signal_level) {
        *self->signal_level = sigLevel;
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    GalaxyStrobeTuneLV2* self = (GalaxyStrobeTuneLV2*)instance;
    if (self) {
        delete self;
    }
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    GALAXY_STROBE_TUNE_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : nullptr;
}
