#include <lv2/core/lv2.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#define TAPE_ECHO_URI "http://dusk-audio.com/plugins/tape-echo-2"

class SpaceTapeEchoDSP {
public:
    float sample_rate = 48000.0f;
    std::vector<float> tape_buffer;
    size_t write_pos = 0;
    size_t buf_size = 0;

    // Filters and state
    float rec_lp = 0.0f;
    float rec_hp = 0.0f;
    float fb_lp = 0.0f;
    float lfo_phase = 0.0f;

    // Spring tank
    std::vector<float> spring_buf[3];
    size_t spring_pos[3] = {0, 0, 0};

    void init(float sr) {
        sample_rate = sr > 8000.0f ? sr : 48000.0f;
        buf_size = static_cast<size_t>(sample_rate * 3.0f); // 3 seconds max
        tape_buffer.assign(buf_size, 0.0f);
        write_pos = 0;
        rec_lp = rec_hp = fb_lp = lfo_phase = 0.0f;

        // Simple spring diffusers
        size_t s_lens[3] = { static_cast<size_t>(sr * 0.023f), static_cast<size_t>(sr * 0.037f), static_cast<size_t>(sr * 0.049f) };
        for (int i = 0; i < 3; ++i) {
            spring_buf[i].assign(s_lens[i] > 10 ? s_lens[i] : 10, 0.0f);
            spring_pos[i] = 0;
        }
    }

    void reset() {
        std::fill(tape_buffer.begin(), tape_buffer.end(), 0.0f);
        rec_lp = rec_hp = fb_lp = lfo_phase = 0.0f;
        for (int i = 0; i < 3; ++i) std::fill(spring_buf[i].begin(), spring_buf[i].end(), 0.0f);
    }

    inline float hermite(float y0, float y1, float y2, float y3, float frac) const {
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    inline float read_tape(float delay_samples) const {
        if (delay_samples < 2.0f) delay_samples = 2.0f;
        if (delay_samples > (float)(buf_size - 4)) delay_samples = (float)(buf_size - 4);

        float rpos = (float)write_pos - delay_samples;
        while (rpos < 0.0f) rpos += (float)buf_size;
        while (rpos >= (float)buf_size) rpos -= (float)buf_size;

        int i1 = (int)rpos;
        float frac = rpos - (float)i1;
        int i0 = (i1 - 1 + buf_size) % buf_size;
        int i2 = (i1 + 1) % buf_size;
        int i3 = (i1 + 2) % buf_size;

        return hermite(tape_buffer[i0], tape_buffer[i1], tape_buffer[i2], tape_buffer[i3], frac);
    }

    void process(
        const float* in_l, const float* in_r,
        float* out_l, float* out_r,
        uint32_t count,
        int mode,          // 0: Head 1, 1: Head 2, 2: Head 3, 3: 1+2, 4: 2+3, 5: 1+2+3
        float repeat_rate, // 50ms .. 800ms base (Head 1)
        float intensity,   // 0 .. 1.2 (self-oscillation)
        float tape_age,    // 0 .. 1 (wow/flutter & dark HF loss)
        float spring_mix,  // 0 .. 1
        float echo_level,  // 0 .. 1
        float dry_level    // 0 .. 1
    ) {
        float base_delay = repeat_rate * 0.001f * sample_rate;
        // Head spacing: Head 1 = 1.0x, Head 2 = 1.91x, Head 3 = 2.76x
        float d1 = base_delay;
        float d2 = base_delay * 1.9117f;
        float d3 = base_delay * 2.7612f;

        float wow_flutter_depth = 0.003f * (1.0f + tape_age * 4.0f) * sample_rate;
        float hf_loss_damp = 0.15f + tape_age * 0.45f;

        for (uint32_t i = 0; i < count; ++i) {
            float in_mono = (in_l[i] + (in_r ? in_r[i] : in_l[i])) * 0.5f;

            // Wow & flutter LFO
            lfo_phase += 1.8f / sample_rate;
            if (lfo_phase > 6.2831853f) lfo_phase -= 6.2831853f;
            float wobble = (std::sin(lfo_phase) + 0.35f * std::sin(lfo_phase * 3.7f)) * wow_flutter_depth;

            // Read heads
            float h1 = read_tape(d1 + wobble);
            float h2 = read_tape(d2 + wobble * 1.2f);
            float h3 = read_tape(d3 + wobble * 1.4f);

            float tape_out = 0.0f;
            switch (mode) {
                case 0: tape_out = h1; break;
                case 1: tape_out = h2; break;
                case 2: tape_out = h3; break;
                case 3: tape_out = (h1 + h2) * 0.65f; break;
                case 4: tape_out = (h2 + h3) * 0.65f; break;
                case 5: tape_out = (h1 + h2 + h3) * 0.45f; break;
                default: tape_out = h2; break;
            }

            // Feedback loop with organic magnetic saturation & high frequency loss
            fb_lp += hf_loss_damp * (tape_out - fb_lp);
            float fb_signal = fb_lp * intensity;
            // Asymmetric soft tape saturation
            float sat_signal = std::tanh(in_mono + fb_signal * 1.1f + 0.04f * tape_age) - (0.04f * tape_age);

            // Record to tape
            tape_buffer[write_pos] = sat_signal;
            write_pos = (write_pos + 1) % buf_size;

            // Spring tank path
            float spring_in = in_mono + tape_out * 0.4f;
            float sp = 0.0f;
            for (int k = 0; k < 3; ++k) {
                float d = spring_buf[k][spring_pos[k]];
                float vn = spring_in - 0.55f * d;
                spring_buf[k][spring_pos[k]] = vn;
                spring_pos[k] = (spring_pos[k] + 1) % spring_buf[k].size();
                sp += d - 0.55f * vn;
            }
            sp *= 0.33f;

            float wet_l = tape_out * echo_level + sp * spring_mix;
            float wet_r = tape_out * echo_level + sp * spring_mix * 0.9f;

            out_l[i] = in_l[i] * dry_level + wet_l;
            if (out_r) out_r[i] = (in_r ? in_r[i] : in_l[i]) * dry_level + wet_r;
        }
    }
};

enum {
    PORT_TAPE_IN_L     = 0,
    PORT_TAPE_IN_R     = 1,
    PORT_TAPE_OUT_L    = 2,
    PORT_TAPE_OUT_R    = 3,
    PORT_TAPE_MODE     = 4,
    PORT_TAPE_RATE     = 5,
    PORT_TAPE_INTENSITY= 6,
    PORT_TAPE_AGE      = 7,
    PORT_TAPE_SPRING   = 8,
    PORT_TAPE_ECHO_VOL = 9,
    PORT_TAPE_DRY_VOL  = 10
};

struct DuskTapeEchoLV2 {
    SpaceTapeEchoDSP dsp;
    const float* in_l;
    const float* in_r;
    float* out_l;
    float* out_r;

    const float* p_mode;
    const float* p_rate;
    const float* p_intensity;
    const float* p_age;
    const float* p_spring;
    const float* p_echo_vol;
    const float* p_dry_vol;
};

static LV2_Handle tape_instantiate(
    const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    DuskTapeEchoLV2* plugin = new DuskTapeEchoLV2();
    if (!plugin) return nullptr;
    plugin->dsp.init(static_cast<float>(rate));
    return (LV2_Handle)plugin;
}

static void tape_connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    DuskTapeEchoLV2* plugin = (DuskTapeEchoLV2*)instance;
    if (!plugin) return;
    switch (port) {
        case PORT_TAPE_IN_L:      plugin->in_l = (const float*)data; break;
        case PORT_TAPE_IN_R:      plugin->in_r = (const float*)data; break;
        case PORT_TAPE_OUT_L:     plugin->out_l = (float*)data; break;
        case PORT_TAPE_OUT_R:     plugin->out_r = (float*)data; break;
        case PORT_TAPE_MODE:      plugin->p_mode = (const float*)data; break;
        case PORT_TAPE_RATE:      plugin->p_rate = (const float*)data; break;
        case PORT_TAPE_INTENSITY: plugin->p_intensity = (const float*)data; break;
        case PORT_TAPE_AGE:       plugin->p_age = (const float*)data; break;
        case PORT_TAPE_SPRING:    plugin->p_spring = (const float*)data; break;
        case PORT_TAPE_ECHO_VOL:  plugin->p_echo_vol = (const float*)data; break;
        case PORT_TAPE_DRY_VOL:   plugin->p_dry_vol = (const float*)data; break;
        default: break;
    }
}

static void tape_activate(LV2_Handle instance)
{
    DuskTapeEchoLV2* plugin = (DuskTapeEchoLV2*)instance;
    if (plugin) plugin->dsp.reset();
}

static void tape_run(LV2_Handle instance, uint32_t n_samples)
{
    DuskTapeEchoLV2* plugin = (DuskTapeEchoLV2*)instance;
    if (!plugin || !plugin->out_l || !plugin->out_r || n_samples == 0) return;

    int mode = plugin->p_mode ? static_cast<int>(*plugin->p_mode + 0.5f) : 1;
    float rate = plugin->p_rate ? *plugin->p_rate : 300.0f;
    float intensity = plugin->p_intensity ? *plugin->p_intensity : 0.6f;
    float age = plugin->p_age ? *plugin->p_age : 0.3f;
    float spring = plugin->p_spring ? *plugin->p_spring : 0.3f;
    float echo_vol = plugin->p_echo_vol ? *plugin->p_echo_vol : 0.8f;
    float dry_vol = plugin->p_dry_vol ? *plugin->p_dry_vol : 1.0f;

    const float* in_l = plugin->in_l;
    const float* in_r = plugin->in_r ? plugin->in_r : plugin->in_l;

    if (!in_l) {
        std::memset(plugin->out_l, 0, n_samples * sizeof(float));
        std::memset(plugin->out_r, 0, n_samples * sizeof(float));
        return;
    }

    plugin->dsp.process(in_l, in_r, plugin->out_l, plugin->out_r, n_samples, mode, rate, intensity, age, spring, echo_vol, dry_vol);
}

static void tape_deactivate(LV2_Handle instance) { (void)instance; }

static void tape_cleanup(LV2_Handle instance)
{
    DuskTapeEchoLV2* plugin = (DuskTapeEchoLV2*)instance;
    if (plugin) delete plugin;
}

static const void* tape_extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor tape_descriptor = {
    TAPE_ECHO_URI,
    tape_instantiate,
    tape_connect_port,
    tape_activate,
    tape_run,
    tape_deactivate,
    tape_cleanup,
    tape_extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &tape_descriptor : nullptr;
}
