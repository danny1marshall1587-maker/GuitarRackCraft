#include <lv2/core/lv2.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#define DUSKVERB_URI "http://dusk-audio.com/plugins/duskverb"

// High-fidelity 16-channel Feedback Delay Network + Dattorro diffusion stages
class DuskVerbDSP {
public:
    enum Algorithm {
        ALG_PLATE = 0,
        ALG_HALL,
        ALG_CHAMBER,
        ALG_ROOM,
        ALG_AMBIENT,
        NUM_ALGS
    };

    struct Allpass {
        std::vector<float> buf;
        size_t idx = 0;
        float g = 0.5f;

        void init(size_t len, float gain) {
            buf.assign(len > 0 ? len : 1, 0.0f);
            idx = 0;
            g = gain;
        }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
        inline float process(float in) {
            float b = buf[idx];
            float v = in + b * g;
            float out = b - v * g;
            buf[idx] = v;
            idx = (idx + 1) % buf.size();
            return out;
        }
    };

    struct DelayLine {
        std::vector<float> buf;
        size_t idx = 0;
        float lp = 0.0f;

        void init(size_t len) {
            buf.assign(len > 0 ? len : 1, 0.0f);
            idx = 0;
            lp = 0.0f;
        }
        void clear() {
            std::fill(buf.begin(), buf.end(), 0.0f);
            lp = 0.0f;
        }
        inline float read() const { return buf[idx]; }
        inline void write(float in, float damp) {
            lp = in * (1.0f - damp) + lp * damp;
            buf[idx] = lp;
            idx = (idx + 1) % buf.size();
        }
    };

    float sample_rate = 48000.0f;
    float param_algorithm = 0.0f;
    float param_decay = 2.5f;
    float param_predelay = 20.0f;
    float param_damping = 0.3f;
    float param_width = 100.0f;
    float param_mix = 50.0f;

    // Predelay
    std::vector<float> predelay_buf_l;
    std::vector<float> predelay_buf_r;
    size_t predelay_idx = 0;

    // Diffusion allpasses
    Allpass diff[4];

    // 16-channel FDN
    static const int FDN_SIZE = 16;
    DelayLine fdn_delays[FDN_SIZE];
    float fdn_state[FDN_SIZE];

    // Householder matrix for 16-channel mixing: H = I - 2/N * 1*1^T
    // Fast O(N) calculation: sum = sum(x), out[i] = x[i] - 2/16 * sum = x[i] - 0.125 * sum

    void init(float sr) {
        sample_rate = sr > 8000.0f ? sr : 48000.0f;
        predelay_buf_l.assign((size_t)(sample_rate * 0.5f), 0.0f);
        predelay_buf_r.assign((size_t)(sample_rate * 0.5f), 0.0f);
        predelay_idx = 0;

        // Diffusion prime lengths
        int diff_lens[4] = {
            (int)(sample_rate * 0.0047f),
            (int)(sample_rate * 0.0036f),
            (int)(sample_rate * 0.0127f),
            (int)(sample_rate * 0.0093f)
        };
        for (int i = 0; i < 4; ++i) {
            diff[i].init(std::max(diff_lens[i], 1), 0.65f);
        }

        rebuild_tanks();
    }

    void clear() {
        std::fill(predelay_buf_l.begin(), predelay_buf_l.end(), 0.0f);
        std::fill(predelay_buf_r.begin(), predelay_buf_r.end(), 0.0f);
        for (int i = 0; i < 4; ++i) diff[i].clear();
        for (int i = 0; i < FDN_SIZE; ++i) {
            fdn_delays[i].clear();
            fdn_state[i] = 0.0f;
        }
    }

    void rebuild_tanks() {
        // Base mutually prime delay lengths tailored by algorithm
        float scale = 1.0f;
        int alg = static_cast<int>(param_algorithm);
        if (alg == ALG_PLATE) scale = 0.75f;
        else if (alg == ALG_HALL) scale = 1.35f;
        else if (alg == ALG_CHAMBER) scale = 0.95f;
        else if (alg == ALG_ROOM) scale = 0.55f;
        else if (alg == ALG_AMBIENT) scale = 2.0f;

        const int primes[FDN_SIZE] = {
            647, 733, 857, 997, 1153, 1279, 1429, 1579,
            1723, 1877, 2053, 2221, 2381, 2557, 2713, 2909
        };

        for (int i = 0; i < FDN_SIZE; ++i) {
            size_t len = static_cast<size_t>(primes[i] * (sample_rate / 44100.0f) * scale);
            fdn_delays[i].init(len > 10 ? len : 10);
            fdn_state[i] = 0.0f;
        }
    }

    void process(const float* in_l, const float* in_r, float* out_l, float* out_r, uint32_t count) {
        int predelay_samples = static_cast<int>(param_predelay * 0.001f * sample_rate);
        if (predelay_samples >= (int)predelay_buf_l.size())
            predelay_samples = (int)predelay_buf_l.size() - 1;

        // Calculate decay feedback gain based on T60: g = 10^(-3 * avg_delay / (decay * sr))
        float avg_delay_sec = 0.04f;
        float decay_time = std::max(param_decay, 0.1f);
        float feedback_gain = std::pow(0.001f, avg_delay_sec / decay_time);
        feedback_gain = std::min(std::max(feedback_gain, 0.0f), 0.98f);

        float wet_gain = param_mix * 0.01f;
        float dry_gain = 1.0f - wet_gain;
        float damp = std::min(std::max(param_damping, 0.01f), 0.95f);
        float width = param_width * 0.01f;

        for (uint32_t i = 0; i < count; ++i) {
            // Predelay ring buffer
            predelay_buf_l[predelay_idx] = in_l[i];
            predelay_buf_r[predelay_idx] = in_r[i];

            size_t r_idx = (predelay_idx + predelay_buf_l.size() - predelay_samples) % predelay_buf_l.size();
            float pre_l = predelay_buf_l[r_idx];
            float pre_r = predelay_buf_r[r_idx];
            predelay_idx = (predelay_idx + 1) % predelay_buf_l.size();

            // Diffusion stage
            float mono_in = (pre_l + pre_r) * 0.5f;
            float diffused = diff[3].process(diff[2].process(diff[1].process(diff[0].process(mono_in))));

            // Read from 16 FDN delay lines
            float sum = 0.0f;
            for (int k = 0; k < FDN_SIZE; ++k) {
                fdn_state[k] = fdn_delays[k].read();
                sum += fdn_state[k];
            }

            // Householder reflection mixing: out_k = state_k - (2/16) * sum
            float h_factor = 2.0f / (float)FDN_SIZE * sum;
            for (int k = 0; k < FDN_SIZE; ++k) {
                float mixed = (fdn_state[k] - h_factor) * feedback_gain;
                // Add diffused input to tank
                float input_sign = (k % 2 == 0) ? 1.0f : -1.0f;
                fdn_delays[k].write(mixed + diffused * input_sign * 0.35f, damp);
            }

            // Synthesize stereo output from interleaved taps
            float rev_l = 0.0f;
            float rev_r = 0.0f;
            for (int k = 0; k < FDN_SIZE; ++k) {
                if (k % 2 == 0) rev_l += fdn_state[k];
                else rev_r += fdn_state[k];
            }
            rev_l *= 0.35f;
            rev_r *= 0.35f;

            // Stereo width matrix
            float mid = (rev_l + rev_r) * 0.5f;
            float side = (rev_l - rev_r) * 0.5f * width;
            float final_l = mid + side;
            float final_r = mid - side;

            out_l[i] = in_l[i] * dry_gain + final_l * wet_gain;
            out_r[i] = in_r[i] * dry_gain + final_r * wet_gain;
        }
    }
};

enum {
    DUSK_PORT_AUDIO_IN_L  = 0,
    DUSK_PORT_AUDIO_IN_R  = 1,
    DUSK_PORT_AUDIO_OUT_L = 2,
    DUSK_PORT_AUDIO_OUT_R = 3,
    DUSK_PORT_ALGORITHM   = 4,
    DUSK_PORT_DECAY       = 5,
    DUSK_PORT_PREDELAY    = 6,
    DUSK_PORT_DAMPING     = 7,
    DUSK_PORT_WIDTH       = 8,
    DUSK_PORT_MIX         = 9
};

struct DuskVerbLV2 {
    DuskVerbDSP dsp;
    const float* in_l;
    const float* in_r;
    float* out_l;
    float* out_r;

    const float* p_algorithm;
    const float* p_decay;
    const float* p_predelay;
    const float* p_damping;
    const float* p_width;
    const float* p_mix;
    int last_alg = -1;
};

static LV2_Handle dusk_instantiate(
    const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    DuskVerbLV2* plugin = new DuskVerbLV2();
    if (!plugin) return nullptr;
    plugin->dsp.init(static_cast<float>(rate));
    plugin->dsp.clear();
    plugin->last_alg = -1;
    return (LV2_Handle)plugin;
}

static void dusk_connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    DuskVerbLV2* plugin = (DuskVerbLV2*)instance;
    if (!plugin) return;
    switch (port) {
        case DUSK_PORT_AUDIO_IN_L:  plugin->in_l = (const float*)data; break;
        case DUSK_PORT_AUDIO_IN_R:  plugin->in_r = (const float*)data; break;
        case DUSK_PORT_AUDIO_OUT_L: plugin->out_l = (float*)data; break;
        case DUSK_PORT_AUDIO_OUT_R: plugin->out_r = (float*)data; break;
        case DUSK_PORT_ALGORITHM:   plugin->p_algorithm = (const float*)data; break;
        case DUSK_PORT_DECAY:       plugin->p_decay = (const float*)data; break;
        case DUSK_PORT_PREDELAY:    plugin->p_predelay = (const float*)data; break;
        case DUSK_PORT_DAMPING:     plugin->p_damping = (const float*)data; break;
        case DUSK_PORT_WIDTH:       plugin->p_width = (const float*)data; break;
        case DUSK_PORT_MIX:         plugin->p_mix = (const float*)data; break;
        default: break;
    }
}

static void dusk_activate(LV2_Handle instance)
{
    DuskVerbLV2* plugin = (DuskVerbLV2*)instance;
    if (plugin) plugin->dsp.clear();
}

static void dusk_run(LV2_Handle instance, uint32_t n_samples)
{
    DuskVerbLV2* plugin = (DuskVerbLV2*)instance;
    if (!plugin || !plugin->out_l || !plugin->out_r || n_samples == 0) return;

    if (plugin->p_algorithm) {
        int alg = static_cast<int>(*plugin->p_algorithm + 0.5f);
        if (alg < 0) alg = 0;
        if (alg >= DuskVerbDSP::NUM_ALGS) alg = DuskVerbDSP::NUM_ALGS - 1;
        plugin->dsp.param_algorithm = static_cast<float>(alg);
        if (plugin->last_alg != alg) {
            plugin->dsp.rebuild_tanks();
            plugin->last_alg = alg;
        }
    }
    if (plugin->p_decay)    plugin->dsp.param_decay = *plugin->p_decay;
    if (plugin->p_predelay) plugin->dsp.param_predelay = *plugin->p_predelay;
    if (plugin->p_damping)  plugin->dsp.param_damping = *plugin->p_damping;
    if (plugin->p_width)    plugin->dsp.param_width = *plugin->p_width;
    if (plugin->p_mix)      plugin->dsp.param_mix = *plugin->p_mix;

    const float* in_l = plugin->in_l;
    const float* in_r = plugin->in_r ? plugin->in_r : plugin->in_l;

    if (!in_l) {
        std::memset(plugin->out_l, 0, n_samples * sizeof(float));
        std::memset(plugin->out_r, 0, n_samples * sizeof(float));
        return;
    }

    plugin->dsp.process(in_l, in_r, plugin->out_l, plugin->out_r, n_samples);
}

static void dusk_deactivate(LV2_Handle instance) { (void)instance; }

static void dusk_cleanup(LV2_Handle instance)
{
    DuskVerbLV2* plugin = (DuskVerbLV2*)instance;
    if (plugin) delete plugin;
}

static const void* dusk_extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor dusk_descriptor = {
    DUSKVERB_URI,
    dusk_instantiate,
    dusk_connect_port,
    dusk_activate,
    dusk_run,
    dusk_deactivate,
    dusk_cleanup,
    dusk_extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &dusk_descriptor : nullptr;
}
