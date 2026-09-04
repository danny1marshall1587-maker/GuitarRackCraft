#include <cstdlib>
#include <cstring>
#include <cmath>
#include "lv2/lv2.h"
#include "MainsHumEngine.hpp"

#define MAINS_HUM_URI "http://moddevices.com/plugins/danny/mains-hum-killer"

enum PortIndex {
    PORT_AUDIO_IN   = 0,
    PORT_AUDIO_OUT  = 1,
    PORT_BYPASS     = 2,
    PORT_GRID_FREQ  = 3,
    PORT_DEPTH      = 4,
    PORT_HARMONICS  = 5,
    PORT_BUZZ_KILL  = 6,
    PORT_Q_WIDTH    = 7,
    PORT_FINE_TUNE  = 8,
    PORT_LISTEN_MODE= 9
};

struct MainsHumLV2 {
    const float* in;
    float*       out;
    const float* bypass;
    const float* grid_freq;
    const float* depth;
    const float* harmonics;
    const float* buzz_kill;
    const float* q_width;
    const float* fine_tune;
    const float* listen_mode;

    AudioDSP::MainsHumEngine engine;
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

    MainsHumLV2* self = (MainsHumLV2*)std::calloc(1, sizeof(MainsHumLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    MainsHumLV2* self = (MainsHumLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN:
            self->in = (const float*)data_location;
            break;
        case PORT_AUDIO_OUT:
            self->out = (float*)data_location;
            break;
        case PORT_BYPASS:
            self->bypass = (const float*)data_location;
            break;
        case PORT_GRID_FREQ:
            self->grid_freq = (const float*)data_location;
            break;
        case PORT_DEPTH:
            self->depth = (const float*)data_location;
            break;
        case PORT_HARMONICS:
            self->harmonics = (const float*)data_location;
            break;
        case PORT_BUZZ_KILL:
            self->buzz_kill = (const float*)data_location;
            break;
        case PORT_Q_WIDTH:
            self->q_width = (const float*)data_location;
            break;
        case PORT_FINE_TUNE:
            self->fine_tune = (const float*)data_location;
            break;
        case PORT_LISTEN_MODE:
            self->listen_mode = (const float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    MainsHumLV2* self = (MainsHumLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    MainsHumLV2* self = (MainsHumLV2*)instance;
    if (!self || !self->out || sample_count == 0) return;

    const float* in = self->in ? self->in : self->out;
    float* out = self->out;

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
        return;
    }

    if (self->grid_freq) self->engine.setGridMode(static_cast<int>(*self->grid_freq + 0.5f));
    if (self->depth) self->engine.setDepthDb(*self->depth);
    if (self->harmonics) self->engine.setHarmonicsCount(static_cast<int>(*self->harmonics + 0.5f));
    if (self->buzz_kill) self->engine.setBuzzKill(*self->buzz_kill);
    if (self->q_width) self->engine.setQ(*self->q_width);
    if (self->fine_tune) self->engine.setFineTune(*self->fine_tune);
    if (self->listen_mode) self->engine.setListenHumOnly(*self->listen_mode >= 0.5f);

    self->engine.process(in, out, sample_count);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    MainsHumLV2* self = (MainsHumLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    MAINS_HUM_URI,
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
