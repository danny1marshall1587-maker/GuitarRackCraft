#include <lv2.h>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>

#define PLUGIN_URI "http://cyberaudio.com/plugins/cyber-spring-reverb"

enum PortIndex {
    PORT_AUDIO_IN  = 0,
    PORT_AUDIO_OUT = 1,
    PORT_DWELL     = 2,
    PORT_TONE      = 3,
    PORT_MIX       = 4,
    PORT_DRIP      = 5,
    PORT_DECAY     = 6,
    PORT_BYPASS    = 7
};

class CyberSpringReverb {
private:
    // Ports
    const float* p_in;
    float* p_out;
    const float* p_dwell;
    const float* p_tone;
    const float* p_mix;
    const float* p_drip;
    const float* p_decay;
    const float* p_bypass;

    double sample_rate;

    // 3 Physical Spring Tank Delays
    static const int NUM_SPRINGS = 3;
    static const int MAX_DELAY = 16384;
    float spring_buffers[NUM_SPRINGS][MAX_DELAY];
    int spring_delays[NUM_SPRINGS];
    int write_pos[NUM_SPRINGS];

    // Input Diffusion Allpasses (2 stages)
    static const int DIFF_DELAY1 = 128;
    static const int DIFF_DELAY2 = 192;
    float diff_buf1[DIFF_DELAY1];
    float diff_buf2[DIFF_DELAY2];
    int diff_idx1;
    int diff_idx2;

    // Dispersive Allpass Chains (16 stages per spring for the classic "drip / boing")
    static const int DISP_STAGES = 16;
    float disp_x[NUM_SPRINGS][DISP_STAGES];
    float disp_y[NUM_SPRINGS][DISP_STAGES];

    // Damping and Tone Filters
    float lp_filter[NUM_SPRINGS];
    float out_tone_lp;

    // Smooth parameter states
    float smoothed_mix;
    float smoothed_decay;

    inline float soft_clip(float x) {
        if (x > 1.5f) return 1.0f;
        if (x < -1.5f) return -1.0f;
        return x * (1.5f - 0.5f * x * x);
    }

public:
    CyberSpringReverb(double rate) : sample_rate(rate),
        p_in(nullptr), p_out(nullptr),
        p_dwell(nullptr), p_tone(nullptr), p_mix(nullptr),
        p_drip(nullptr), p_decay(nullptr), p_bypass(nullptr),
        diff_idx1(0), diff_idx2(0), out_tone_lp(0.0f),
        smoothed_mix(0.35f), smoothed_decay(0.5f)
    {
        // Calculate nominal physical spring lengths (typical 3-spring Accutronics tank)
        // Spring 1: ~33.5ms, Spring 2: ~37.3ms, Spring 3: ~41.8ms
        spring_delays[0] = std::max(16, std::min(MAX_DELAY - 1, (int)(0.0335 * sample_rate)));
        spring_delays[1] = std::max(16, std::min(MAX_DELAY - 1, (int)(0.0373 * sample_rate)));
        spring_delays[2] = std::max(16, std::min(MAX_DELAY - 1, (int)(0.0418 * sample_rate)));

        for (int k = 0; k < NUM_SPRINGS; ++k) {
            std::memset(spring_buffers[k], 0, sizeof(spring_buffers[k]));
            write_pos[k] = 0;
            lp_filter[k] = 0.0f;
            for (int s = 0; s < DISP_STAGES; ++s) {
                disp_x[k][s] = 0.0f;
                disp_y[k][s] = 0.0f;
            }
        }
        std::memset(diff_buf1, 0, sizeof(diff_buf1));
        std::memset(diff_buf2, 0, sizeof(diff_buf2));
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN:  p_in = (const float*)data; break;
            case PORT_AUDIO_OUT: p_out = (float*)data; break;
            case PORT_DWELL:     p_dwell = (const float*)data; break;
            case PORT_TONE:      p_tone = (const float*)data; break;
            case PORT_MIX:       p_mix = (const float*)data; break;
            case PORT_DRIP:      p_drip = (const float*)data; break;
            case PORT_DECAY:     p_decay = (const float*)data; break;
            case PORT_BYPASS:    p_bypass = (const float*)data; break;
        }
    }

    void activate() {
        for (int k = 0; k < NUM_SPRINGS; ++k) {
            std::memset(spring_buffers[k], 0, sizeof(spring_buffers[k]));
            write_pos[k] = 0;
            lp_filter[k] = 0.0f;
            for (int s = 0; s < DISP_STAGES; ++s) {
                disp_x[k][s] = 0.0f;
                disp_y[k][s] = 0.0f;
            }
        }
        std::memset(diff_buf1, 0, sizeof(diff_buf1));
        std::memset(diff_buf2, 0, sizeof(diff_buf2));
        diff_idx1 = 0;
        diff_idx2 = 0;
        out_tone_lp = 0.0f;
    }

    void run(uint32_t sample_count) {
        if (!p_in || !p_out) return;

        bool active = p_bypass ? (*p_bypass > 0.5f) : true;
        if (!active) {
            std::memcpy(p_out, p_in, sizeof(float) * sample_count);
            return;
        }

        // Parameter reading and normalization (0.0 to 1.0)
        float dwell_val = p_dwell ? (*p_dwell * 0.01f) : 0.5f;
        float tone_val  = p_tone  ? (*p_tone  * 0.01f) : 0.6f;
        float mix_val   = p_mix   ? (*p_mix   * 0.01f) : 0.35f;
        float drip_val  = p_drip  ? (*p_drip  * 0.01f) : 0.65f;
        float decay_val = p_decay ? (*p_decay * 0.01f) : 0.5f;

        // Dwell: controls input gain and tube saturation (0.5x to 3.5x gain)
        float drive_gain = 0.5f + 3.0f * (dwell_val * dwell_val);

        // Tone: lowpass damping frequency (1500Hz to 8500Hz)
        float tone_freq = 1500.0f + 7000.0f * (tone_val * tone_val);
        float tone_alpha = 1.0f - std::exp(-2.0f * 3.14159265f * tone_freq / (float)sample_rate);

        // Drip: allpass dispersion coefficient a (-0.35 to -0.75)
        // More negative 'a' creates stronger high-frequency dispersion ("drip")
        float disp_a = -0.35f - 0.40f * drip_val;

        // Decay: feedback coefficient (0.65 to 0.92)
        float target_feedback = 0.65f + 0.27f * decay_val;

        // Smooth mix and feedback
        float smooth_factor = 0.005f;

        for (uint32_t i = 0; i < sample_count; ++i) {
            smoothed_mix += smooth_factor * (mix_val - smoothed_mix);
            smoothed_decay += smooth_factor * (target_feedback - smoothed_decay);

            float in_sample = p_in[i];

            // 1. Input Drive with 6V6 Tube Saturation
            float driven = soft_clip(in_sample * drive_gain);

            // 2. Input Diffusion (transducer gap simulation using 2 cascaded allpasses)
            // Diffuser 1
            float d1_in = driven;
            float d1_delayed = diff_buf1[diff_idx1];
            float d1_out = -0.6f * d1_in + d1_delayed;
            diff_buf1[diff_idx1] = d1_in + 0.6f * d1_out;
            diff_idx1 = (diff_idx1 + 1) % DIFF_DELAY1;

            // Diffuser 2
            float d2_in = d1_out;
            float d2_delayed = diff_buf2[diff_idx2];
            float d2_out = 0.6f * d2_in + d2_delayed;
            diff_buf2[diff_idx2] = d2_in - 0.6f * d2_out;
            diff_idx2 = (diff_idx2 + 1) % DIFF_DELAY2;

            float diffused_in = d2_out;

            // 3. Read outputs from the 3 spring delay lines
            float s0 = spring_buffers[0][write_pos[0]];
            float s1 = spring_buffers[1][write_pos[1]];
            float s2 = spring_buffers[2][write_pos[2]];

            // 4. Spring Mechanical Cross-Coupling (end-plate reflection matrix)
            float c0 =  0.60f * s0 + 0.40f * s1 - 0.35f * s2;
            float c1 = -0.35f * s0 + 0.60f * s1 + 0.40f * s2;
            float c2 =  0.40f * s0 - 0.35f * s1 + 0.60f * s2;

            float coupled[NUM_SPRINGS] = { c0, c1, c2 };

            // 5. Process each spring feedback loop
            for (int k = 0; k < NUM_SPRINGS; ++k) {
                // Input injection + feedback from coupled matrix
                float spring_in = diffused_in + coupled[k] * smoothed_decay;

                // Mild non-linear soft limiting in the spring loop
                spring_in = soft_clip(spring_in);

                // Spring internal steel damping (low-pass filter)
                lp_filter[k] += tone_alpha * (spring_in - lp_filter[k]);
                float filtered = lp_filter[k];

                // Cascaded Allpass Dispersion (the "CHIRP / DRIP" effect)
                float ap_sig = filtered;
                for (int st = 0; st < DISP_STAGES; ++st) {
                    float ap_in = ap_sig;
                    float ap_out = disp_a * ap_in + disp_x[k][st] - disp_a * disp_y[k][st];
                    disp_x[k][st] = ap_in;
                    disp_y[k][st] = ap_out;
                    ap_sig = ap_out;
                }

                // Write into spring delay buffer
                spring_buffers[k][write_pos[k]] = ap_sig;
                write_pos[k] = (write_pos[k] + 1) % spring_delays[k];
            }

            // 6. Combine spring outputs (alternating polarity for spatial depth)
            float raw_wet = (s0 - 0.85f * s1 + 0.90f * s2) * 0.6f;

            // 7. Output Tone Filtering
            out_tone_lp += tone_alpha * (raw_wet - out_tone_lp);
            float wet_signal = out_tone_lp;

            // 8. Dry / Wet Blend
            p_out[i] = (1.0f - smoothed_mix) * in_sample + smoothed_mix * wet_signal;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    return (LV2_Handle)new CyberSpringReverb(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberSpringReverb*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberSpringReverb*)instance)->activate();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberSpringReverb*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete (CyberSpringReverb*)instance;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    nullptr
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
