/*
 * Cyber PureSustain Delay - Seamless Crossfade Pad Delay LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * Core DSP Architecture:
 *  1. Pristine Phase-Pure Stereo Delay Line with Sub-Sample 4-Point Hermite Interpolation.
 *  2. Dual-Head Equal-Power Crossfade Record Engine ("Melt"):
 *     Dissolves loop seams and transient beating into an unbroken sustained pad
 *     at high feedback without any allpass diffusion / phase-smearing mud.
 *  3. Non-Dispersive 1-Pole Analog Tone Damping (2 kHz to 18 kHz) + DC blocker.
 *  4. Dynamic Sidechain Ducker: Keeps picking attack and lead solos clear over the pad.
 *  5. Soft-Knee Hyperbolic Tangent Tape Saturation in feedback loop.
 */

#include "lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-puresustain-delay"
#define MAX_DELAY_SEC 2.5f

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_BYPASS        = 4,
    PORT_TIME          = 5,
    PORT_FEEDBACK      = 6,
    PORT_MELT          = 7,
    PORT_TONE          = 8,
    PORT_DUCK          = 9,
    PORT_MIX           = 10
};

class CyberPureSustainDelay {
private:
    double sample_rate;

    // Delay Buffers
    float* delay_buf_l;
    float* delay_buf_r;
    int max_delay_samples;
    int write_pos;

    // Filter States
    float lp_state_l;
    float lp_state_r;
    float hp_state_l;
    float hp_state_r;

    // Dynamic Sidechain Ducker
    float env_follower;

    // Parameter Smoothing
    float current_delay_samples;

    // Ports
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_time;
    const float* p_feedback;
    const float* p_melt;
    const float* p_tone;
    const float* p_duck;
    const float* p_mix;

    // 4-Point Hermite Interpolation
    inline float read_hermite(const float* buffer, float pos) {
        int i1 = (int)pos;
        int i0 = i1 - 1;
        int i2 = i1 + 1;
        int i3 = i1 + 2;

        if (i0 < 0) i0 += max_delay_samples;
        if (i1 < 0) i1 += max_delay_samples;
        if (i2 < 0) i2 += max_delay_samples;
        if (i3 < 0) i3 += max_delay_samples;

        if (i0 >= max_delay_samples) i0 -= max_delay_samples;
        if (i1 >= max_delay_samples) i1 -= max_delay_samples;
        if (i2 >= max_delay_samples) i2 -= max_delay_samples;
        if (i3 >= max_delay_samples) i3 -= max_delay_samples;

        float frac = pos - (float)i1;
        float frac2 = frac * frac;
        float frac3 = frac2 * frac;

        float y0 = buffer[i0];
        float y1 = buffer[i1];
        float y2 = buffer[i2];
        float y3 = buffer[i3];

        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

public:
    CyberPureSustainDelay(double sr) : sample_rate(sr) {
        max_delay_samples = (int)(sample_rate * MAX_DELAY_SEC) + 2048;
        delay_buf_l = (float*)calloc(max_delay_samples, sizeof(float));
        delay_buf_r = (float*)calloc(max_delay_samples, sizeof(float));
        write_pos = 0;

        lp_state_l = lp_state_r = 0.0f;
        hp_state_l = hp_state_r = 0.0f;
        env_follower = 0.0f;
        current_delay_samples = (float)(sample_rate * 0.4);
    }

    ~CyberPureSustainDelay() {
        if (delay_buf_l) free(delay_buf_l);
        if (delay_buf_r) free(delay_buf_r);
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN_L:  p_in_l = (const float*)data; break;
            case PORT_AUDIO_IN_R:  p_in_r = (const float*)data; break;
            case PORT_AUDIO_OUT_L: p_out_l = (float*)data; break;
            case PORT_AUDIO_OUT_R: p_out_r = (float*)data; break;
            case PORT_BYPASS:      p_bypass = (const float*)data; break;
            case PORT_TIME:        p_time = (const float*)data; break;
            case PORT_FEEDBACK:    p_feedback = (const float*)data; break;
            case PORT_MELT:        p_melt = (const float*)data; break;
            case PORT_TONE:        p_tone = (const float*)data; break;
            case PORT_DUCK:        p_duck = (const float*)data; break;
            case PORT_MIX:         p_mix = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (*p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        float time_ms = std::max(20.0f, std::min(2000.0f, *p_time));
        float feedback_pct = std::max(0.0f, std::min(110.0f, *p_feedback));
        float melt_amt = std::max(0.0f, std::min(100.0f, *p_melt)) * 0.01f;
        float tone_pct = std::max(0.0f, std::min(100.0f, *p_tone)) * 0.01f;
        float duck_amt = std::max(0.0f, std::min(100.0f, *p_duck)) * 0.01f;
        float mix = std::max(0.0f, std::min(100.0f, *p_mix)) * 0.01f;

        float target_delay_samples = (time_ms * 0.001f) * (float)sample_rate;
        target_delay_samples = std::max(64.0f, std::min((float)(max_delay_samples - 1024), target_delay_samples));

        // Time parameter slew (smooth transitions)
        float time_slew = 1.0f - expf(-1.0f / (0.05f * (float)sample_rate));

        // Feedback calculation: 100% is unity, >100% enters saturated sustain
        float fb_gain = feedback_pct * 0.01f;

        // Tone Damping: 1-pole non-dispersive lowpass filter (2 kHz to 18 kHz)
        float lp_cutoff = 2000.0f + (tone_pct * tone_pct) * 16000.0f;
        float lp_coeff = 1.0f - expf(-2.0f * (float)M_PI * lp_cutoff / (float)sample_rate);

        // Subsonic DC blocker filter (35 Hz)
        float hp_coeff = 1.0f - expf(-2.0f * (float)M_PI * 35.0f / (float)sample_rate);

        // Sidechain Ducker Coefficients (5ms attack, 180ms release)
        float duck_att = 1.0f - expf(-1.0f / (0.005f * (float)sample_rate));
        float duck_rel = 1.0f - expf(-1.0f / (0.180f * (float)sample_rate));

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_l = p_in_l[i];
            float in_r = p_in_r ? p_in_r[i] : in_l;
            float in_mono = 0.5f * (in_l + in_r);
            float in_abs = fabsf(in_mono);

            // Smooth delay time interpolation
            current_delay_samples += (target_delay_samples - current_delay_samples) * time_slew;

            // Envelope detection for dynamic ducking
            if (in_abs > env_follower) {
                env_follower += (in_abs - env_follower) * duck_att;
            } else {
                env_follower += (in_abs - env_follower) * duck_rel;
            }

            // Calculate ducking multiplier: reduces wet pad level while actively picking
            float duck_reduction = std::max(0.12f, 1.0f - (env_follower * 2.8f * duck_amt));

            // Primary Read Head Position
            float read_pos_l = (float)write_pos - current_delay_samples;
            float read_pos_r = (float)write_pos - (current_delay_samples * 1.04f); // Subtle stereo spread

            while (read_pos_l < 0.0f) read_pos_l += (float)max_delay_samples;
            while (read_pos_r < 0.0f) read_pos_r += (float)max_delay_samples;
            while (read_pos_l >= (float)max_delay_samples) read_pos_l -= (float)max_delay_samples;
            while (read_pos_r >= (float)max_delay_samples) read_pos_r -= (float)max_delay_samples;

            float delayed_primary_l = read_hermite(delay_buf_l, read_pos_l);
            float delayed_primary_r = read_hermite(delay_buf_r, read_pos_r);

            // Continuous Crossfade Engine ("Melt"):
            // Second read head offset by half the delay period with equal-power raised-cosine windowing
            float delayed_l = delayed_primary_l;
            float delayed_r = delayed_primary_r;

            if (melt_amt > 0.005f) {
                float half_delay = current_delay_samples * 0.5f;
                float read_sec_l = read_pos_l - half_delay;
                float read_sec_r = read_pos_r - half_delay;

                while (read_sec_l < 0.0f) read_sec_l += (float)max_delay_samples;
                while (read_sec_r < 0.0f) read_sec_r += (float)max_delay_samples;
                while (read_sec_l >= (float)max_delay_samples) read_sec_l -= (float)max_delay_samples;
                while (read_sec_r >= (float)max_delay_samples) read_sec_r -= (float)max_delay_samples;

                float delayed_sec_l = read_hermite(delay_buf_l, read_sec_l);
                float delayed_sec_r = read_hermite(delay_buf_r, read_sec_r);

                // Phase of loop cycle for continuous crossfade
                float cycle_phase = fmodf((float)write_pos, current_delay_samples) / current_delay_samples;
                float angle = cycle_phase * (float)M_PI; // 0 to pi
                float cos_val = cosf(angle);
                float sin_val = sinf(angle);
                float w0 = cos_val * cos_val; // Equal power: w0 + w1 = cos^2 + sin^2 = 1
                float w1 = sin_val * sin_val;

                // Blend melted crossfade with primary signal based on melt parameter
                float melted_l = delayed_primary_l * w0 + delayed_sec_l * w1;
                float melted_r = delayed_primary_r * w0 + delayed_sec_r * w1;

                delayed_l = delayed_primary_l * (1.0f - melt_amt) + melted_l * melt_amt;
                delayed_r = delayed_primary_r * (1.0f - melt_amt) + melted_r * melt_amt;
            }

            // Non-Dispersive 1-Pole Tone Damping
            lp_state_l += lp_coeff * (delayed_l - lp_state_l);
            lp_state_r += lp_coeff * (delayed_r - lp_state_r);

            // DC Blocking
            hp_state_l += hp_coeff * (lp_state_l - hp_state_l);
            hp_state_r += hp_coeff * (lp_state_r - hp_state_r);

            float wet_clean_l = lp_state_l - hp_state_l;
            float wet_clean_r = lp_state_r - hp_state_r;

            // Soft-Knee Saturation in feedback loop
            float sat_l = tanhf(wet_clean_l * fb_gain);
            float sat_r = tanhf(wet_clean_r * fb_gain);

            // Cross-feedback stereo ping-pong
            delay_buf_l[write_pos] = in_l + sat_l * 0.75f + sat_r * 0.25f;
            delay_buf_r[write_pos] = in_r + sat_r * 0.75f + sat_l * 0.25f;

            if (++write_pos >= max_delay_samples) write_pos = 0;

            // Apply dynamic sidechain ducking to wet output
            float ducked_wet_l = wet_clean_l * duck_reduction;
            float ducked_wet_r = wet_clean_r * duck_reduction;

            // Master Output Mix
            p_out_l[i] = in_l * (1.0f - mix) + ducked_wet_l * mix;
            if (p_out_r) {
                p_out_r[i] = in_r * (1.0f - mix) + ducked_wet_r * mix;
            }
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* path,
                             const LV2_Feature* const* features) {
    return new CyberPureSustainDelay(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberPureSustainDelay*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberPureSustainDelay*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete (CyberPureSustainDelay*)instance;
}

static const void* extension_data(const char* uri) {
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
