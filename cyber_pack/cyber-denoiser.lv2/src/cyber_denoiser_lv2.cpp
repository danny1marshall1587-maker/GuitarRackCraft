#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2/lv2.h"
#include "CyberDenoiserEngine.hpp"

#define CYBER_DENOISER_URI "http://moddevices.com/plugins/danny/cyber-denoiser"

enum PortIndex {
    PORT_AUDIO_IN_L      = 0,
    PORT_AUDIO_IN_R      = 1,
    PORT_AUDIO_OUT_L     = 2,
    PORT_AUDIO_OUT_R     = 3,
    PORT_BYPASS          = 4,
    PORT_T1              = 5,
    PORT_T2              = 6,
    PORT_T3              = 7,
    PORT_T4              = 8,
    PORT_T5              = 9,
    PORT_T6              = 10,
    PORT_T7              = 11,
    PORT_T8              = 12,
    PORT_T9              = 13,
    PORT_T10             = 14,
    PORT_LEARN           = 15,
    PORT_REDUCTION       = 16,
    PORT_SENSITIVITY     = 17,
    PORT_LISTEN_NOISE    = 18,
    PORT_LOW_CUT         = 19,
    PORT_LEARNING_STATUS = 20,
    PORT_L1              = 21,
    PORT_L2              = 22,
    PORT_L3              = 23,
    PORT_L4              = 24,
    PORT_L5              = 25,
    PORT_L6              = 26,
    PORT_L7              = 27,
    PORT_L8              = 28,
    PORT_L9              = 29,
    PORT_L10             = 30
};

struct CyberDenoiserLV2 {
    // Audio ports
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;

    // Control input ports
    const float* bypass;
    const float* t[10];
    const float* learn;
    const float* reduction;
    const float* sensitivity;
    const float* listen_noise;
    const float* low_cut;

    // Control output ports
    float*       learning_status;
    float*       l[10];

    // DSP Engine
    AudioDSP::CyberDenoiserEngine engine;
    double sampleRate;
    bool prevLearnTrigger;
};

static LV2_Handle instantiate(const LV2_Descriptor*     descriptor,
                             double                    sample_rate,
                             const char*               bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;
    (void)features;

    CyberDenoiserLV2* self = (CyberDenoiserLV2*)std::calloc(1, sizeof(CyberDenoiserLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);
    self->prevLearnTrigger = false;

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    CyberDenoiserLV2* self = (CyberDenoiserLV2*)instance;
    if (!self) return;

    if (port >= PORT_T1 && port <= PORT_T10) {
        self->t[port - PORT_T1] = (const float*)data_location;
        return;
    }

    if (port >= PORT_L1 && port <= PORT_L10) {
        self->l[port - PORT_L1] = (float*)data_location;
        return;
    }

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
        case PORT_LEARN:
            self->learn = (const float*)data_location;
            break;
        case PORT_REDUCTION:
            self->reduction = (const float*)data_location;
            break;
        case PORT_SENSITIVITY:
            self->sensitivity = (const float*)data_location;
            break;
        case PORT_LISTEN_NOISE:
            self->listen_noise = (const float*)data_location;
            break;
        case PORT_LOW_CUT:
            self->low_cut = (const float*)data_location;
            break;
        case PORT_LEARNING_STATUS:
            self->learning_status = (float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    CyberDenoiserLV2* self = (CyberDenoiserLV2*)instance;
    if (!self) return;
    self->engine.reset();
    self->prevLearnTrigger = false;
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    CyberDenoiserLV2* self = (CyberDenoiserLV2*)instance;
    if (!self || !self->outL) return;

    const float* inL = self->inL ? self->inL : self->outL;
    const float* inR = self->inR ? self->inR : inL;
    float* outL = self->outL;
    float* outR = self->outR ? self->outR : outL;

    // Check learn trigger
    if (self->learn) {
        bool learnVal = (*self->learn > 0.5f);
        if (learnVal && !self->prevLearnTrigger) {
            self->engine.startLearn();
        }
        self->prevLearnTrigger = learnVal;
    }

    // Set 10-band thresholds
    for (int i = 0; i < 10; ++i) {
        if (self->t[i]) {
            self->engine.setThresholdDb(i, *self->t[i]);
        }
    }

    // Set Master Parameters
    if (self->reduction) self->engine.setReductionAmount(*self->reduction);
    if (self->sensitivity) self->engine.setThresholdOffsetDb(*self->sensitivity);
    if (self->listen_noise) self->engine.setListenNoise(*self->listen_noise > 0.5f);
    if (self->low_cut) self->engine.setLowCut(*self->low_cut > 0.5f);

    // Check bypass (0 = bypass, 1 = active)
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);

    if (isBypassed) {
        if (outL != inL) std::memcpy(outL, inL, sample_count * sizeof(float));
        if (outR != inR) std::memcpy(outR, inR, sample_count * sizeof(float));
    } else {
        self->engine.processBlock(inL, inR, outL, outR, sample_count);
    }

    // Output learn status (1.0 = learning, 0.0 = ready)
    if (self->learning_status) {
        *self->learning_status = self->engine.isLearning() ? 1.0f : 0.0f;
    }

    // Output 10-band level telemetry for live spectrum visualizer
    for (int i = 0; i < 10; ++i) {
        if (self->l[i]) {
            float level = self->engine.getBandLevel(i);
            *self->l[i] = std::min(1.0f, level * 4.0f);
        }
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    CyberDenoiserLV2* self = (CyberDenoiserLV2*)instance;
    if (self) {
        std::free(self);
    }
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    CYBER_DENOISER_URI,
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
