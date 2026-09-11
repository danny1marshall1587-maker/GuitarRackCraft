#include <lv2/core/lv2.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "DFJPverb.hpp"

#define DFJPVERB_URI "http://distrho.sf.net/plugins/DFJPverb"

enum {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_T60           = 4,
    PORT_SIZE          = 5,
    PORT_DAMP          = 6,
    PORT_EARLY_DIFF    = 7,
    PORT_LOW_CUTOFF    = 8,
    PORT_HIGH_CUTOFF   = 9,
    PORT_LOW           = 10,
    PORT_MID           = 11,
    PORT_HIGH          = 12,
    PORT_MOD_DEPTH     = 13,
    PORT_MOD_FREQ      = 14,
    PORT_DRY           = 15,
    PORT_WET           = 16
};

struct DFJPverbLV2 {
    DFJPverb dsp;
    float sample_rate;

    const float* in_l;
    const float* in_r;
    float* out_l;
    float* out_r;

    const float* p_t60;
    const float* p_size;
    const float* p_damp;
    const float* p_early_diff;
    const float* p_low_cutoff;
    const float* p_high_cutoff;
    const float* p_low;
    const float* p_mid;
    const float* p_high;
    const float* p_mod_depth;
    const float* p_mod_freq;
    const float* p_dry;
    const float* p_wet;
};

static LV2_Handle instantiate(
    const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;
    (void)features;

    DFJPverbLV2* plugin = new DFJPverbLV2();
    if (!plugin) return nullptr;

    plugin->sample_rate = static_cast<float>(rate);
    plugin->dsp.init(plugin->sample_rate);
    plugin->dsp.clear();

    plugin->in_l = nullptr;
    plugin->in_r = nullptr;
    plugin->out_l = nullptr;
    plugin->out_r = nullptr;

    return (LV2_Handle)plugin;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    DFJPverbLV2* plugin = (DFJPverbLV2*)instance;
    if (!plugin) return;

    switch (port) {
        case PORT_AUDIO_IN_L:  plugin->in_l = (const float*)data; break;
        case PORT_AUDIO_IN_R:  plugin->in_r = (const float*)data; break;
        case PORT_AUDIO_OUT_L: plugin->out_l = (float*)data; break;
        case PORT_AUDIO_OUT_R: plugin->out_r = (float*)data; break;
        case PORT_T60:         plugin->p_t60 = (const float*)data; break;
        case PORT_SIZE:        plugin->p_size = (const float*)data; break;
        case PORT_DAMP:        plugin->p_damp = (const float*)data; break;
        case PORT_EARLY_DIFF:  plugin->p_early_diff = (const float*)data; break;
        case PORT_LOW_CUTOFF:  plugin->p_low_cutoff = (const float*)data; break;
        case PORT_HIGH_CUTOFF: plugin->p_high_cutoff = (const float*)data; break;
        case PORT_LOW:         plugin->p_low = (const float*)data; break;
        case PORT_MID:         plugin->p_mid = (const float*)data; break;
        case PORT_HIGH:        plugin->p_high = (const float*)data; break;
        case PORT_MOD_DEPTH:   plugin->p_mod_depth = (const float*)data; break;
        case PORT_MOD_FREQ:    plugin->p_mod_freq = (const float*)data; break;
        case PORT_DRY:         plugin->p_dry = (const float*)data; break;
        case PORT_WET:         plugin->p_wet = (const float*)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    DFJPverbLV2* plugin = (DFJPverbLV2*)instance;
    if (plugin) {
        plugin->dsp.clear();
    }
}

static void run(LV2_Handle instance, uint32_t n_samples)
{
    DFJPverbLV2* plugin = (DFJPverbLV2*)instance;
    if (!plugin || !plugin->out_l || !plugin->out_r || n_samples == 0) return;

    if (plugin->p_t60)         plugin->dsp.set_t60(*plugin->p_t60);
    if (plugin->p_size)        plugin->dsp.set_size(*plugin->p_size);
    if (plugin->p_damp)        plugin->dsp.set_damp(*plugin->p_damp);
    if (plugin->p_early_diff)  plugin->dsp.set_early_diff(*plugin->p_early_diff);
    if (plugin->p_low_cutoff)  plugin->dsp.set_low_cutoff(*plugin->p_low_cutoff);
    if (plugin->p_high_cutoff) plugin->dsp.set_high_cutoff(*plugin->p_high_cutoff);
    if (plugin->p_low)         plugin->dsp.set_low(*plugin->p_low);
    if (plugin->p_mid)         plugin->dsp.set_mid(*plugin->p_mid);
    if (plugin->p_high)        plugin->dsp.set_high(*plugin->p_high);
    if (plugin->p_mod_depth)   plugin->dsp.set_mod_depth(*plugin->p_mod_depth);
    if (plugin->p_mod_freq)    plugin->dsp.set_mod_freq(*plugin->p_mod_freq);
    if (plugin->p_dry)         plugin->dsp.set_dry(*plugin->p_dry);
    if (plugin->p_wet)         plugin->dsp.set_wet(*plugin->p_wet);

    const float* in_l = plugin->in_l;
    const float* in_r = plugin->in_r ? plugin->in_r : plugin->in_l;

    if (!in_l) {
        std::memset(plugin->out_l, 0, n_samples * sizeof(float));
        std::memset(plugin->out_r, 0, n_samples * sizeof(float));
        return;
    }

    plugin->dsp.process(in_l, in_r, plugin->out_l, plugin->out_r, n_samples);
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    DFJPverbLV2* plugin = (DFJPverbLV2*)instance;
    if (plugin) delete plugin;
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    DFJPVERB_URI,
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
