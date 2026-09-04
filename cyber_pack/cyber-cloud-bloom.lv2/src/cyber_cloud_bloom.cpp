/*
 * Cyber Cloud Bloom - Ambient Swell-Diffused Pad Delay LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * Core DSP Architecture:
 *  1. Dual-Path Splitter: Dry signal remains crisp and punchy.
 *  2. SlowGear Envelope Detector: Strips pick attack & harsh clicks from delay input.
 *  3. Stereo Ping-Pong / Cross-Feedback Delay Line with tempo sync & millisecond modes.
 *  4. 8-Stage Allpass Cloud Diffuser: Progressively smears echo repeats into lush ambient synth pads.
 *  5. Analog Tape Wow/Flutter & Tone Tilt Filter in diffusion feedback loop.
 *  6. Shimmer Overtone Generator (+12st pitch sparkle).
 *  7. Freeze / Infinite Hold latch.
 */

#include "lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-cloud-bloom"
#define MAX_DELAY_SEC 3.0f

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_BYPASS        = 4,
    PORT_TIME          = 5,
    PORT_FEEDBACK      = 6,
    PORT_BLOOM         = 7,
    PORT_SMEAR         = 8,
    PORT_WARMTH        = 9,
    PORT_MOD           = 10,
    PORT_SHIMMER       = 11,
    PORT_HOLD          = 12,
    PORT_MIX           = 13
};

// Allpass Diffuser Stage
struct AllpassStage {
    float* buffer;
    int size;
    int index;
    float coeff;

    void init(int sz, float c) {
        size = std::max(1, sz);
        coeff = c;
        buffer = (float*)calloc(size, sizeof(float));
        index = 0;
    }

    void free_mem() {
        if (buffer) {
            free(buffer);
            buffer = nullptr;
        }
    }

    inline float process(float in) {
        float buf_out = buffer[index];
        float temp = in + coeff * buf_out;
        float out = buf_out - coeff * temp;
        buffer[index] = temp;
        if (++index >= size) index = 0;
        return out;
    }
};

class CyberCloudBloom {
private:
    double sample_rate;

    // Buffers
    float* delay_buf_l;
    float* delay_buf_r;
    int max_delay_samples;
    int write_pos;

    // Allpass Diffusers (4 per channel = 8 stages)
    AllpassStage diff_l[4];
    AllpassStage diff_r[4];

    // Envelope Follower (SlowGear Swell)
    float env_level;
    float swell_gain;
    float last_in_level;

    // Tape Modulation LFO
    float lfo_phase;

    // Filters
    float lp_state_l;
    float lp_state_r;
    float hp_state_l;
    float hp_state_r;

    // Shimmer Pitch Buffers
    float* shimmer_buf;
    int shimmer_size;
    int shimmer_pos;
    float shimmer_phase;

    // Ports
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_time;
    const float* p_feedback;
    const float* p_bloom;
    const float* p_smear;
    const float* p_warmth;
    const float* p_mod;
    const float* p_shimmer;
    const float* p_hold;
    const float* p_mix;

public:
    CyberCloudBloom(double sr) : sample_rate(sr) {
        max_delay_samples = (int)(sample_rate * MAX_DELAY_SEC) + 1024;
        delay_buf_l = (float*)calloc(max_delay_samples, sizeof(float));
        delay_buf_r = (float*)calloc(max_delay_samples, sizeof(float));
        write_pos = 0;

        // Initialize Prime Allpass Diffusion Stages
        int diff_sizes_l[4] = { 143, 379, 709, 1283 };
        int diff_sizes_r[4] = { 157, 397, 727, 1301 };
        float scale = (float)(sample_rate / 48000.0);

        for (int i = 0; i < 4; ++i) {
            diff_l[i].init((int)(diff_sizes_l[i] * scale), 0.65f);
            diff_r[i].init((int)(diff_sizes_r[i] * scale), 0.65f);
        }

        env_level = 0.0f;
        swell_gain = 0.0f;
        last_in_level = 0.0f;
        lfo_phase = 0.0f;

        lp_state_l = lp_state_r = 0.0f;
        hp_state_l = hp_state_r = 0.0f;

        shimmer_size = (int)(sample_rate * 0.05f); // 50ms grain window
        shimmer_buf = (float*)calloc(shimmer_size, sizeof(float));
        shimmer_pos = 0;
        shimmer_phase = 0.0f;
    }

    ~CyberCloudBloom() {
        if (delay_buf_l) free(delay_buf_l);
        if (delay_buf_r) free(delay_buf_r);
        if (shimmer_buf) free(shimmer_buf);
        for (int i = 0; i < 4; ++i) {
            diff_l[i].free_mem();
            diff_r[i].free_mem();
        }
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
            case PORT_BLOOM:       p_bloom = (const float*)data; break;
            case PORT_SMEAR:       p_smear = (const float*)data; break;
            case PORT_WARMTH:      p_warmth = (const float*)data; break;
            case PORT_MOD:         p_mod = (const float*)data; break;
            case PORT_SHIMMER:     p_shimmer = (const float*)data; break;
            case PORT_HOLD:        p_hold = (const float*)data; break;
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

        float time_ms = *p_time;
        float feedback = std::min(0.98f, *p_feedback * 0.01f);
        float bloom_sec = std::max(0.01f, *p_bloom * 0.01f * 2.5f); // 10ms to 2.5s rise
        float smear = *p_smear * 0.01f;
        float warmth = *p_warmth * 0.01f;
        float mod_depth = *p_mod * 0.01f;
        float shimmer_amt = *p_shimmer * 0.01f;
        float hold_amt = *p_hold * 0.01f;
        float mix = *p_mix * 0.01f;

        float target_delay_samples = (time_ms * 0.001f) * (float)sample_rate;
        target_delay_samples = std::max(10.0f, std::min((float)(max_delay_samples - 500), target_delay_samples));

        // Swell rise/fall coefficients
        float attack_coeff = 1.0f - expf(-1.0f / (bloom_sec * (float)sample_rate));
        float release_coeff = 1.0f - expf(-1.0f / (0.04f * (float)sample_rate));

        // Tone filter cutoff coefficients
        float lp_cutoff = 1000.0f + (1.0f - warmth) * 14000.0f; // 1kHz to 15kHz
        float lp_coeff = 1.0f - expf(-2.0f * (float)M_PI * lp_cutoff / (float)sample_rate);
        float hp_coeff = 1.0f - expf(-2.0f * (float)M_PI * 60.0f / (float)sample_rate);

        // Update Diffuser coefficients with smear control
        for (int i = 0; i < 4; ++i) {
            diff_l[i].coeff = 0.3f + smear * 0.45f;
            diff_r[i].coeff = 0.3f + smear * 0.45f;
        }

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_l = p_in_l[i];
            float in_r = p_in_r ? p_in_r[i] : in_l;
            float in_mono = 0.5f * (in_l + in_r);
            float in_abs = fabsf(in_mono);

            // SlowGear Transient Detection
            // Fast attack detection on input transient
            if (in_abs > env_level + 0.02f) {
                env_level = in_abs;
                swell_gain = 0.0f; // Reset swell to zero on new pick attack
            } else {
                env_level += (in_abs - env_level) * release_coeff;
                swell_gain += (1.0f - swell_gain) * attack_coeff; // Smooth exponential bloom
            }

            // Swelled input entering delay line (pick attack removed)
            float swelled_input = in_mono * swell_gain * (1.0f - hold_amt);

            // Tape Wow & Flutter LFO
            lfo_phase += (1.2f / (float)sample_rate);
            if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;
            float lfo = sinf(2.0f * (float)M_PI * lfo_phase);
            float mod_samples = lfo * mod_depth * 18.0f;

            // Interpolated Delay Read
            float r_pos_l = (float)write_pos - target_delay_samples + mod_samples;
            float r_pos_r = (float)write_pos - (target_delay_samples * 1.15f) - mod_samples;

            while (r_pos_l < 0) r_pos_l += max_delay_samples;
            while (r_pos_r < 0) r_pos_r += max_delay_samples;
            while (r_pos_l >= max_delay_samples) r_pos_l -= max_delay_samples;
            while (r_pos_r >= max_delay_samples) r_pos_r -= max_delay_samples;

            int i_l0 = (int)r_pos_l;
            int i_l1 = (i_l0 + 1) % max_delay_samples;
            float frac_l = r_pos_l - (float)i_l0;
            float delayed_l = delay_buf_l[i_l0] + frac_l * (delay_buf_l[i_l1] - delay_buf_l[i_l0]);

            int i_r0 = (int)r_pos_r;
            int i_r1 = (i_r0 + 1) % max_delay_samples;
            float frac_r = r_pos_r - (float)i_r0;
            float delayed_r = delay_buf_r[i_r0] + frac_r * (delay_buf_r[i_r1] - delay_buf_r[i_r0]);

            // Allpass Cloud Diffusion Tank
            float diffused_l = delayed_l;
            float diffused_r = delayed_r;
            for (int d = 0; d < 4; ++d) {
                diffused_l = diff_l[d].process(diffused_l);
                diffused_r = diff_r[d].process(diffused_r);
            }

            // Cross-blend between distinct echo and smeared cloud
            float cloud_l = delayed_l * (1.0f - smear) + diffused_l * smear;
            float cloud_r = delayed_r * (1.0f - smear) + diffused_r * smear;

            // Octave Shimmer Processing
            if (shimmer_amt > 0.01f) {
                shimmer_buf[shimmer_pos] = 0.5f * (cloud_l + cloud_r);
                shimmer_phase += 2.0f; // Double speed = +1 Octave
                if (shimmer_phase >= shimmer_size) shimmer_phase -= shimmer_size;
                int sh_idx = (int)shimmer_phase;
                float sh_sample = shimmer_buf[sh_idx];
                cloud_l += sh_sample * shimmer_amt * 0.4f;
                cloud_r += sh_sample * shimmer_amt * 0.4f;
                if (++shimmer_pos >= shimmer_size) shimmer_pos = 0;
            }

            // Warmth & Damping Filters
            lp_state_l += lp_coeff * (cloud_l - lp_state_l);
            lp_state_r += lp_coeff * (cloud_r - lp_state_r);
            hp_state_l += hp_coeff * (lp_state_l - hp_state_l);
            hp_state_r += hp_coeff * (lp_state_r - hp_state_r);

            float wet_l = lp_state_l - hp_state_l;
            float wet_r = lp_state_r - hp_state_r;

            // Soft saturation in feedback loop
            float fb_gain = feedback + (1.15f - feedback) * hold_amt;
            float fb_l = tanhf((wet_l + swelled_input) * fb_gain);
            float fb_r = tanhf((wet_r + swelled_input) * fb_gain);

            // Cross-feedback ping pong
            delay_buf_l[write_pos] = swelled_input + fb_r * 0.35f + fb_l * 0.65f;
            delay_buf_r[write_pos] = swelled_input + fb_l * 0.35f + fb_r * 0.65f;

            if (++write_pos >= max_delay_samples) write_pos = 0;

            // Master Output Mix
            p_out_l[i] = in_l * (1.0f - mix) + wet_l * mix;
            if (p_out_r) p_out_r[i] = in_r * (1.0f - mix) + wet_r * mix;
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* path,
                             const LV2_Feature* const* features) {
    return new CyberCloudBloom(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberCloudBloom*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberCloudBloom*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete (CyberCloudBloom*)instance;
}

static const void* extension_data(const char* uri) {
    return NULL;
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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LV2_EXPORT __declspec(dllexport)
#else
  #define LV2_EXPORT __attribute__((visibility("default")))
#endif

LV2_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}

#ifdef __cplusplus
}
#endif

