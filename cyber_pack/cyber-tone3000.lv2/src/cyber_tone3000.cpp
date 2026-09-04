#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2.h"
#include "Tone3000Engine.hpp"

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-tone3000"

enum PortIndex {
    PORT_AUDIO_IN     = 0,
    PORT_AUDIO_OUT    = 1,
    PORT_BYPASS       = 2,
    PORT_INPUT_GAIN   = 3,
    PORT_OUTPUT_LEVEL = 4,
    PORT_GATE         = 5,
    PORT_BASS         = 6,
    PORT_MID          = 7,
    PORT_TREBLE       = 8,
    PORT_MODEL        = 9,
    PORT_IR           = 10,
    PORT_IR_BLEND     = 11
};

struct CyberTone3000LV2 {
    const float* in;
    float*       out;

    const float* bypass;
    const float* inputGain;
    const float* outputLevel;
    const float* gate;
    const float* bass;
    const float* mid;
    const float* treble;
    const float* model;
    const float* ir;
    const float* irBlend;

    AudioDSP::Tone3000Engine engine;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    CyberTone3000LV2* self = (CyberTone3000LV2*)std::calloc(1, sizeof(CyberTone3000LV2));
    if (!self) return nullptr;
    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    CyberTone3000LV2* self = (CyberTone3000LV2*)instance;
    if (!self) return;
    switch (port) {
        case PORT_AUDIO_IN:     self->in          = (const float*)data; break;
        case PORT_AUDIO_OUT:    self->out         = (float*)data;       break;
        case PORT_BYPASS:       self->bypass      = (const float*)data; break;
        case PORT_INPUT_GAIN:   self->inputGain   = (const float*)data; break;
        case PORT_OUTPUT_LEVEL: self->outputLevel = (const float*)data; break;
        case PORT_GATE:         self->gate        = (const float*)data; break;
        case PORT_BASS:         self->bass        = (const float*)data; break;
        case PORT_MID:          self->mid         = (const float*)data; break;
        case PORT_TREBLE:       self->treble      = (const float*)data; break;
        case PORT_MODEL:        self->model       = (const float*)data; break;
        case PORT_IR:           self->ir          = (const float*)data; break;
        case PORT_IR_BLEND:     self->irBlend     = (const float*)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    CyberTone3000LV2* self = (CyberTone3000LV2*)instance;
    if (self) self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    CyberTone3000LV2* self = (CyberTone3000LV2*)instance;
    if (!self || !self->out) return;

    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    if (self->inputGain)   self->engine.setInputGainDb(*self->inputGain);
    if (self->outputLevel) self->engine.setOutputLevelDb(*self->outputLevel);
    if (self->gate)        self->engine.setGateThresholdDb(*self->gate);
    if (self->bass)        self->engine.setBassDb(*self->bass);
    if (self->mid)         self->engine.setMidDb(*self->mid);
    if (self->treble)      self->engine.setTrebleDb(*self->treble);
    if (self->model)       self->engine.setModel(static_cast<int>(std::round(*self->model)));
    if (self->ir)          self->engine.setIr(static_cast<int>(std::round(*self->ir)));
    if (self->irBlend)     self->engine.setIrBlend(*self->irBlend * 0.01f);

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
    } else {
        self->engine.process(in, out, sample_count);
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance) {
    CyberTone3000LV2* self = (CyberTone3000LV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    PLUGIN_URI, instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
