#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2.h"
#include "SmartFizzKillerEngine.hpp"

#define SFK_MONO_URI "http://moddevices.com/plugins/danny/smart-fizz-killer-mono"

enum PortIndex {
    PORT_AUDIO_IN     = 0,
    PORT_AUDIO_OUT    = 1,
    PORT_BYPASS       = 2,
    PORT_SMEAR        = 3,
    PORT_FIZZ_FREQ    = 4,
    PORT_FIZZ_DEPTH   = 5,
    PORT_FIZZ_SENS    = 6,
    PORT_SATURATION   = 7,
    PORT_SYMMETRY     = 8,
    PORT_TILT         = 9,
    PORT_BODY         = 10,
    PORT_HIGH_CUT     = 11,
    PORT_MIX          = 12,
    PORT_OUTPUT       = 13
};

struct SmartFizzKillerMonoLV2 {
    const float* in;
    float*       out;

    const float* bypass;
    const float* smear;
    const float* fizzFreq;
    const float* fizzDepth;
    const float* fizzSens;
    const float* saturation;
    const float* symmetry;
    const float* tilt;
    const float* body;
    const float* highCut;
    const float* mix;
    const float* output;

    AudioDSP::SmartFizzKillerEngine engine;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    SmartFizzKillerMonoLV2* self = (SmartFizzKillerMonoLV2*)std::calloc(1, sizeof(SmartFizzKillerMonoLV2));
    if (!self) return nullptr;
    self->sampleRate = sample_rate;
    self->engine.prepare(sample_rate);
    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    SmartFizzKillerMonoLV2* self = (SmartFizzKillerMonoLV2*)instance;
    if (!self) return;
    switch (port) {
        case PORT_AUDIO_IN:    self->in         = (const float*)data; break;
        case PORT_AUDIO_OUT:   self->out        = (float*)data;       break;
        case PORT_BYPASS:      self->bypass     = (const float*)data; break;
        case PORT_SMEAR:       self->smear      = (const float*)data; break;
        case PORT_FIZZ_FREQ:   self->fizzFreq   = (const float*)data; break;
        case PORT_FIZZ_DEPTH:  self->fizzDepth  = (const float*)data; break;
        case PORT_FIZZ_SENS:   self->fizzSens   = (const float*)data; break;
        case PORT_SATURATION:  self->saturation = (const float*)data; break;
        case PORT_SYMMETRY:    self->symmetry   = (const float*)data; break;
        case PORT_TILT:        self->tilt       = (const float*)data; break;
        case PORT_BODY:        self->body       = (const float*)data; break;
        case PORT_HIGH_CUT:    self->highCut    = (const float*)data; break;
        case PORT_MIX:         self->mix        = (const float*)data; break;
        case PORT_OUTPUT:      self->output     = (const float*)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance) {
    SmartFizzKillerMonoLV2* self = (SmartFizzKillerMonoLV2*)instance;
    if (self) self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    SmartFizzKillerMonoLV2* self = (SmartFizzKillerMonoLV2*)instance;
    if (!self || !self->out) return;

    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    if (self->smear)      self->engine.setSmearFreq(*self->smear);
    if (self->fizzFreq)   self->engine.setFizzFreq(*self->fizzFreq);
    if (self->fizzDepth)  self->engine.setFizzDepth(*self->fizzDepth);
    if (self->fizzSens)   self->engine.setFizzSensitivity(*self->fizzSens);
    if (self->saturation) self->engine.setSaturation(*self->saturation);
    if (self->symmetry)   self->engine.setSymmetry(*self->symmetry);
    if (self->tilt)       self->engine.setTilt(*self->tilt);
    if (self->body)       self->engine.setBody(*self->body);
    if (self->highCut)    self->engine.setHighCut(*self->highCut);
    if (self->mix)        self->engine.setMix(*self->mix);
    if (self->output)     self->engine.setOutputDb(*self->output);

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
    } else {
        self->engine.process(in, nullptr, out, nullptr, sample_count);
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance) {
    SmartFizzKillerMonoLV2* self = (SmartFizzKillerMonoLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    SFK_MONO_URI, instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
