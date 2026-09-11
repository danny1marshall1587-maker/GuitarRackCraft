#include <lv2/core/lv2.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "GrainEngine.hpp"

#define GRAINSOFSAND_URI "http://stefan.local/plugins/grainsofsand"

enum {
    PORT_AUDIO_IN   = 0,
    PORT_AUDIO_OUT_L= 1,
    PORT_AUDIO_OUT_R= 2,
    PORT_DELAY      = 3,
    PORT_SPRAY      = 4,
    PORT_STRETCH    = 5,
    PORT_SIZE       = 6,
    PORT_DENSITY    = 7,
    PORT_JITTER     = 8,
    PORT_SHAPE      = 9,
    PORT_REVERSE    = 10,
    PORT_PITCH      = 11,
    PORT_QUANTIZE   = 12,
    PORT_DETUNE     = 13,
    PORT_PITCH_RND  = 14,
    PORT_FEEDBACK   = 15,
    PORT_FB_MODE    = 16,
    PORT_TONE       = 17,
    PORT_REPEAT_DICE= 18,
    PORT_FREEZE     = 19,
    PORT_THRESH     = 20,
    PORT_SPREAD     = 21,
    PORT_MIX        = 22,
    PORT_LEVEL      = 23,
    PORT_FREEZE_STATE= 24
};

struct GrainsOfSandLV2 {
    gos::GrainEngine engine;
    gos::Params params;
    float sample_rate;

    const float* in;
    float* out_l;
    float* out_r;

    const float* p_delay;
    const float* p_spray;
    const float* p_stretch;
    const float* p_size;
    const float* p_density;
    const float* p_jitter;
    const float* p_shape;
    const float* p_reverse;
    const float* p_pitch;
    const float* p_quantize;
    const float* p_detune;
    const float* p_pitch_rnd;
    const float* p_feedback;
    const float* p_fb_mode;
    const float* p_tone;
    const float* p_repeat_dice;
    const float* p_freeze;
    const float* p_thresh;
    const float* p_spread;
    const float* p_mix;
    const float* p_level;
    float* p_freeze_state;
};

static LV2_Handle grains_instantiate(
    const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    GrainsOfSandLV2* plugin = new GrainsOfSandLV2();
    if (!plugin) return nullptr;
    plugin->sample_rate = static_cast<float>(rate);
    plugin->engine.setup(plugin->sample_rate);
    return (LV2_Handle)plugin;
}

static void grains_connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    GrainsOfSandLV2* plugin = (GrainsOfSandLV2*)instance;
    if (!plugin) return;

    switch (port) {
        case PORT_AUDIO_IN:    plugin->in = (const float*)data; break;
        case PORT_AUDIO_OUT_L: plugin->out_l = (float*)data; break;
        case PORT_AUDIO_OUT_R: plugin->out_r = (float*)data; break;
        case PORT_DELAY:       plugin->p_delay = (const float*)data; break;
        case PORT_SPRAY:       plugin->p_spray = (const float*)data; break;
        case PORT_STRETCH:     plugin->p_stretch = (const float*)data; break;
        case PORT_SIZE:        plugin->p_size = (const float*)data; break;
        case PORT_DENSITY:     plugin->p_density = (const float*)data; break;
        case PORT_JITTER:      plugin->p_jitter = (const float*)data; break;
        case PORT_SHAPE:       plugin->p_shape = (const float*)data; break;
        case PORT_REVERSE:     plugin->p_reverse = (const float*)data; break;
        case PORT_PITCH:       plugin->p_pitch = (const float*)data; break;
        case PORT_QUANTIZE:    plugin->p_quantize = (const float*)data; break;
        case PORT_DETUNE:      plugin->p_detune = (const float*)data; break;
        case PORT_PITCH_RND:   plugin->p_pitch_rnd = (const float*)data; break;
        case PORT_FEEDBACK:    plugin->p_feedback = (const float*)data; break;
        case PORT_FB_MODE:     plugin->p_fb_mode = (const float*)data; break;
        case PORT_TONE:        plugin->p_tone = (const float*)data; break;
        case PORT_REPEAT_DICE: plugin->p_repeat_dice = (const float*)data; break;
        case PORT_FREEZE:      plugin->p_freeze = (const float*)data; break;
        case PORT_THRESH:      plugin->p_thresh = (const float*)data; break;
        case PORT_SPREAD:      plugin->p_spread = (const float*)data; break;
        case PORT_MIX:         plugin->p_mix = (const float*)data; break;
        case PORT_LEVEL:       plugin->p_level = (const float*)data; break;
        case PORT_FREEZE_STATE:plugin->p_freeze_state = (float*)data; break;
        default: break;
    }
}

static void grains_activate(LV2_Handle instance)
{
    GrainsOfSandLV2* plugin = (GrainsOfSandLV2*)instance;
    if (plugin) plugin->engine.reset();
}

static void grains_run(LV2_Handle instance, uint32_t n_samples)
{
    GrainsOfSandLV2* plugin = (GrainsOfSandLV2*)instance;
    if (!plugin || !plugin->out_l || !plugin->out_r || n_samples == 0) return;

    if (plugin->p_delay)       plugin->params.delayMs = *plugin->p_delay;
    if (plugin->p_spray)       plugin->params.sprayMs = *plugin->p_spray;
    if (plugin->p_stretch)     plugin->params.stretch = *plugin->p_stretch;
    if (plugin->p_size)        plugin->params.sizeMs = *plugin->p_size;
    if (plugin->p_density)     plugin->params.density = *plugin->p_density;
    if (plugin->p_jitter)      plugin->params.jitter = *plugin->p_jitter * 0.01f;
    if (plugin->p_shape)       plugin->params.shape = *plugin->p_shape * 0.01f;
    if (plugin->p_reverse)     plugin->params.reverse = *plugin->p_reverse * 0.01f;
    if (plugin->p_pitch)       plugin->params.pitch = *plugin->p_pitch;
    if (plugin->p_quantize)    plugin->params.quantize = static_cast<int>(*plugin->p_quantize);
    if (plugin->p_detune)      plugin->params.detune = *plugin->p_detune;
    if (plugin->p_pitch_rnd)   plugin->params.intervalChance = *plugin->p_pitch_rnd * 0.01f;
    if (plugin->p_feedback)    plugin->params.feedback = *plugin->p_feedback * 0.01f;
    if (plugin->p_fb_mode)     plugin->params.fbMode = static_cast<int>(*plugin->p_fb_mode);
    if (plugin->p_tone)        plugin->params.toneHz = *plugin->p_tone;
    if (plugin->p_repeat_dice) plugin->params.repeatDice = *plugin->p_repeat_dice * 0.01f;
    if (plugin->p_freeze)      plugin->params.freeze = (*plugin->p_freeze > 0.5f);
    if (plugin->p_thresh)      plugin->params.threshDb = *plugin->p_thresh;
    if (plugin->p_spread)      plugin->params.spread = *plugin->p_spread * 0.01f;

    float mix = plugin->p_mix ? (*plugin->p_mix * 0.01f) : 0.5f;
    float level_db = plugin->p_level ? *plugin->p_level : 0.0f;
    float out_gain = std::pow(10.0f, level_db * 0.05f);

    const float* in = plugin->in;
    if (!in) {
        std::memset(plugin->out_l, 0, n_samples * sizeof(float));
        std::memset(plugin->out_r, 0, n_samples * sizeof(float));
        return;
    }

    plugin->engine.setParams(plugin->params);
    plugin->engine.process(in, plugin->out_l, plugin->out_r, n_samples);

    if (plugin->p_freeze_state) {
        *plugin->p_freeze_state = plugin->engine.frozen() ? 1.0f : 0.0f;
    }

    // Apply dry/wet mix and output gain
    for (uint32_t i = 0; i < n_samples; ++i) {
        float dry = in[i];
        plugin->out_l[i] = (dry * (1.0f - mix) + plugin->out_l[i] * mix) * out_gain;
        plugin->out_r[i] = (dry * (1.0f - mix) + plugin->out_r[i] * mix) * out_gain;
    }
}

static void grains_deactivate(LV2_Handle instance) { (void)instance; }

static void grains_cleanup(LV2_Handle instance)
{
    GrainsOfSandLV2* plugin = (GrainsOfSandLV2*)instance;
    if (plugin) delete plugin;
}

static const void* grains_extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor grains_descriptor = {
    GRAINSOFSAND_URI,
    grains_instantiate,
    grains_connect_port,
    grains_activate,
    grains_run,
    grains_deactivate,
    grains_cleanup,
    grains_extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &grains_descriptor : nullptr;
}
