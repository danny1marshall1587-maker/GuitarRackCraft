/*
 * Cyber Klon - LV2 Plugin
 * Klon Centaur Professional Overdrive + Dynamic Fuzz Face Glassy Cleanup Volume Morphing
 *
 * Core Concept:
 *   Dynamic volume-driven circuit morphing:
 *     - Full Guitar Volume / Hard Pick Attack -> Klon Centaur (dual-ganged clean blend + 18V charge pump + Germanium diode clipping + active treble control + Magic Diode mod)
 *     - Rolled-Back Volume / Soft Pick Attack  -> Fuzz Face Glassy Cleanup (Germanium chime + treble bleed sparkle)
 *   Controls:
 *     - Gain (Centaur dual-ganged overdrive/clean blend)
 *     - Treble (active 4 kHz boost/cut)
 *     - Output (master output level)
 *     - Bias: Center volume threshold for the morph transition
 *     - Sens: Transition slope/sensitivity
 *     - Sparkle: Glassy chime treble boost
 *     - Magic Mod: Vintage Germanium vs Schottky clipping diodes
 *     - Mix, Bypass
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-klon"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_GAIN           = 3,
    PORT_TREBLE         = 4,
    PORT_OUTPUT         = 5,
    PORT_BIAS           = 6,
    PORT_SENS           = 7,
    PORT_SPARKLE        = 8,
    PORT_MAGIC_MOD      = 9,
    PORT_MIX            = 10,
    PORT_COUNT          = 11
};

// ─── One-Pole IIR Filter ─────────────────────────────────────────────────────
struct OnePole {
    float z1 = 0.0f;

    float lp(float x, float fc, float sr) {
        float w  = 2.0f * (float)M_PI * fc / sr;
        float a0 = w / (1.0f + w);
        float b1 = 1.0f - a0;
        z1 = a0 * x + b1 * z1;
        return z1;
    }

    float hp(float x, float fc, float sr) {
        return x - lp(x, fc, sr);
    }

    void reset() { z1 = 0.0f; }
};

// ─── Direct Form II Transposed Biquad ────────────────────────────────────────
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    void reset() { s1 = s2 = 0.0f; }

    float process(float in) {
        float out = b0 * in + s1;
        s1 = b1 * in - a1 * out + s2;
        s2 = b2 * in - a2 * out;
        return out;
    }

    void lowShelf(float fc, float gainDB, float sr) {
        float A       = powf(10.0f, gainDB / 40.0f);
        float w0      = 2.0f * (float)M_PI * fc / sr;
        float cosw    = cosf(w0);
        float sinw    = sinf(w0);
        float alpha   = sinw / (2.0f * 0.707f);
        float a_plus  = (A + 1.0f);
        float a_minus = (A - 1.0f);
        float sqrt2a  = 2.0f * sqrtf(A) * alpha;

        float a0 = a_plus + a_minus*cosw + sqrt2a;
        b0 = (A * (a_plus - a_minus*cosw + sqrt2a)) / a0;
        b1 = (2.0f * A * (a_minus - a_plus*cosw))   / a0;
        b2 = (A * (a_plus - a_minus*cosw - sqrt2a)) / a0;
        a1 = (-2.0f * (a_minus + a_plus*cosw))      / a0;
        a2 = (a_plus + a_minus*cosw - sqrt2a)       / a0;
    }

    void highShelf(float fc, float gainDB, float sr) {
        float A       = powf(10.0f, gainDB / 40.0f);
        float w0      = 2.0f * (float)M_PI * fc / sr;
        float cosw    = cosf(w0);
        float sinw    = sinf(w0);
        float alpha   = sinw / (2.0f * 0.707f);
        float a_plus  = (A + 1.0f);
        float a_minus = (A - 1.0f);
        float sqrt2a  = 2.0f * sqrtf(A) * alpha;

        float a0 = a_plus - a_minus*cosw + sqrt2a;
        b0 = (A * (a_plus + a_minus*cosw + sqrt2a)) / a0;
        b1 = (-2.0f * A * (a_minus + a_plus*cosw))  / a0;
        b2 = (A * (a_plus + a_minus*cosw - sqrt2a)) / a0;
        a1 = (2.0f * (a_minus - a_plus*cosw))       / a0;
        a2 = (a_plus - a_minus*cosw - sqrt2a)       / a0;
    }

    void peaking(float fc, float gainDB, float Q, float sr) {
        float A     = powf(10.0f, gainDB / 40.0f);
        float w0    = 2.0f * (float)M_PI * fc / sr;
        float cosw  = cosf(w0);
        float sinw  = sinf(w0);
        float alpha = sinw / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw)     / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw)     / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
};

// ─── Precision Dual-Detector Dynamics Follower ──────────────────────────────
struct DynamicsDetector {
    float env_fast = 0.0f;
    float env_slow = 0.0f;

    float process(float x, float atk_fast, float rel_fast, float rel_slow) {
        float ax = fabsf(x);
        if (ax > env_fast)
            env_fast += atk_fast * (ax - env_fast);
        else
            env_fast += rel_fast * (ax - env_fast);

        if (ax > env_slow)
            env_slow += (atk_fast * 0.4f) * (ax - env_slow);
        else
            env_slow += rel_slow * (ax - env_slow);

        return 0.65f * env_fast + 0.35f * env_slow;
    }

    void reset() { env_fast = env_slow = 0.0f; }
};

// ─── Nonlinear Saturation Stages ─────────────────────────────────────────────

// Klon Centaur Dual-Ganged Clean Blend + Germanium Diode Soft Clipping Stage
static inline float klon_overdrive_stage(float clean_signal, float raw_x, float gain, bool magic_mod) {
    // Klon dual-ganged pot behavior:
    // Gain=0%  -> 100% clean 18V buffered boost
    // Gain=100% -> 90% Germanium clipped overdrive + 10% clean
    float drive_factor = gain;
    float clean_factor = 1.0f - gain * 0.70f;

    float g = 1.0f + drive_factor * 15.0f; // 1..16 gain
    float drive_in = raw_x * g;

    // Germanium 1N34A clipping diodes (~0.35V threshold)
    float clipped;
    if (magic_mod) {
        // Magic Schottky/Ge hybrid: slightly asymmetrical with richer 2nd harmonic bloom
        if (drive_in >= 0.0f) {
            clipped = tanhf(drive_in * 1.6f) * 0.60f;
        } else {
            clipped = -tanhf(-drive_in * 1.1f) * 0.70f;
        }
    } else {
        // Classic Klon Centaur 1N34A Germanium pair
        clipped = tanhf(drive_in * 1.35f) * 0.65f;
    }

    return clean_factor * clean_signal + drive_factor * clipped * 1.25f;
}

// Fuzz Face Germanium Dynamic Circuit
static inline float fuzzface_circuit(float x, float gain) {
    float g = 2.0f + gain * 6.5f;
    float gx = x * g;
    if (gx >= 0.0f) {
        return tanhf(gx * 1.25f) * 0.88f;
    } else {
        return -tanhf(-gx * 0.85f) * 0.95f;
    }
}

// ─── Plugin Structure ────────────────────────────────────────────────────────
struct CyberKlon {
    const float* audio_in  = nullptr;
    float*       audio_out = nullptr;

    const float* p_bypass    = nullptr;
    const float* p_gain      = nullptr;
    const float* p_treble    = nullptr;
    const float* p_output    = nullptr;
    const float* p_bias      = nullptr;
    const float* p_sens      = nullptr;
    const float* p_sparkle   = nullptr;
    const float* p_magic_mod = nullptr;
    const float* p_mix       = nullptr;

    double sample_rate = 48000.0;

    OnePole hp_in;
    OnePole lp_pre;
    OnePole buffer_sag;

    // Klon Active Treble EQ (4 kHz shelving/peaking)
    Biquad  klon_treble_eq;
    OnePole treble_sparkle_hp;

    DynamicsDetector detector;
    float atk_coeff      = 0.08f;
    float rel_fast_coeff = 0.008f;
    float rel_slow_coeff = 0.002f;

    // Smoothed parameters
    float smooth_gain    = 0.40f;
    float smooth_treb    = 0.50f;
    float smooth_out     = 0.70f;
    float smooth_bias    = 0.50f;
    float smooth_sens    = 0.50f;
    float smooth_sparkle = 0.60f;
    float smooth_mix     = 1.00f;
    float smooth_morph   = 0.50f;

    float prev_treb_gain = 0.0f;
};

static LV2_Handle instantiate(const LV2_Descriptor*, double rate,
                               const char*, const LV2_Feature* const*) {
    auto* p = new CyberKlon();
    p->sample_rate = rate > 1000.0 ? rate : 48000.0;

    p->atk_coeff      = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.012f));
    p->rel_fast_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.070f));
    p->rel_slow_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.150f));

    return (LV2_Handle)p;
}

static void connect_port(LV2_Handle handle, uint32_t port, void* data) {
    auto* p = (CyberKlon*)handle;
    switch (port) {
        case PORT_AUDIO_IN:   p->audio_in    = (const float*)data; break;
        case PORT_AUDIO_OUT:  p->audio_out   = (float*)data;       break;
        case PORT_BYPASS:     p->p_bypass    = (const float*)data; break;
        case PORT_GAIN:       p->p_gain      = (const float*)data; break;
        case PORT_TREBLE:     p->p_treble    = (const float*)data; break;
        case PORT_OUTPUT:     p->p_output    = (const float*)data; break;
        case PORT_BIAS:       p->p_bias      = (const float*)data; break;
        case PORT_SENS:       p->p_sens      = (const float*)data; break;
        case PORT_SPARKLE:    p->p_sparkle   = (const float*)data; break;
        case PORT_MAGIC_MOD:  p->p_magic_mod = (const float*)data; break;
        case PORT_MIX:        p->p_mix       = (const float*)data; break;
    }
}

static void activate(LV2_Handle handle) {
    auto* p = (CyberKlon*)handle;
    p->hp_in.reset();
    p->lp_pre.reset();
    p->buffer_sag.reset();
    p->klon_treble_eq.reset();
    p->treble_sparkle_hp.reset();
    p->detector.reset();

    p->smooth_gain    = 0.40f;
    p->smooth_treb    = 0.50f;
    p->smooth_out     = 0.70f;
    p->smooth_bias    = 0.50f;
    p->smooth_sens    = 0.50f;
    p->smooth_sparkle = 0.60f;
    p->smooth_mix     = 1.00f;
    p->smooth_morph   = 0.50f;
    p->prev_treb_gain = 0.0f;
}

static void run(LV2_Handle handle, uint32_t n_samples) {
    auto* p = (CyberKlon*)handle;
    if (!p->audio_in || !p->audio_out) return;

    const float* in  = p->audio_in;
    float*       out = p->audio_out;

    if (p->p_bypass && *p->p_bypass < 0.5f) {
        if (out != in) {
            std::memcpy(out, in, n_samples * sizeof(float));
        }
        return;
    }

    // Read control ports
    float t_gain    = p->p_gain      ? std::max(0.0f, std::min(100.0f, *p->p_gain)) / 100.0f : 0.40f;
    float t_treb    = p->p_treble    ? std::max(0.0f, std::min(100.0f, *p->p_treble)) / 100.0f : 0.50f;
    float t_out     = p->p_output    ? std::max(0.0f, std::min(100.0f, *p->p_output)) / 100.0f : 0.70f;
    float t_bias    = p->p_bias      ? std::max(0.0f, std::min(100.0f, *p->p_bias)) / 100.0f : 0.50f;
    float t_sens    = p->p_sens      ? std::max(0.0f, std::min(100.0f, *p->p_sens)) / 100.0f : 0.50f;
    float t_sparkle = p->p_sparkle   ? std::max(0.0f, std::min(100.0f, *p->p_sparkle)) / 100.0f : 0.60f;
    bool  magic_mod = p->p_magic_mod ? (*p->p_magic_mod > 0.5f) : false;
    float t_mix     = p->p_mix       ? std::max(0.0f, std::min(100.0f, *p->p_mix)) / 100.0f : 1.00f;

    // Klon Active Treble Control (4 kHz active shelf, -14 dB .. +14 dB)
    float treb_gain = (t_treb - 0.5f) * 28.0f;
    if (fabsf(treb_gain - p->prev_treb_gain) > 0.05f) {
        p->klon_treble_eq.highShelf(4000.0f, treb_gain, (float)p->sample_rate);
        p->prev_treb_gain = treb_gain;
    }

    float smooth_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.010f));
    float morph_coeff  = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.020f));

    for (uint32_t i = 0; i < n_samples; ++i) {
        p->smooth_gain    += smooth_coeff * (t_gain    - p->smooth_gain);
        p->smooth_treb    += smooth_coeff * (t_treb    - p->smooth_treb);
        p->smooth_out     += smooth_coeff * (t_out     - p->smooth_out);
        p->smooth_bias    += smooth_coeff * (t_bias    - p->smooth_bias);
        p->smooth_sens    += smooth_coeff * (t_sens    - p->smooth_sens);
        p->smooth_sparkle += smooth_coeff * (t_sparkle - p->smooth_sparkle);
        p->smooth_mix     += smooth_coeff * (t_mix     - p->smooth_mix);

        float raw_in = in[i];
        float dry    = raw_in;

        // 1. Klon 18V Buffered Clean Signal
        float clean_buffered = p->hp_in.hp(raw_in, 45.0f, (float)p->sample_rate);
        clean_buffered = p->lp_pre.lp(clean_buffered, 12000.0f, (float)p->sample_rate);

        // 2. Dynamic Volume Tracking for Circuit Morphing
        float input_level = p->detector.process(raw_in, p->atk_coeff, p->rel_fast_coeff, p->rel_slow_coeff);

        float center_threshold = 0.015f + p->smooth_bias * 0.22f;
        float transition_width = 0.02f + (1.0f - p->smooth_sens) * 0.15f;

        float morph_raw = 0.5f + (input_level - center_threshold) / (transition_width * 2.0f);
        float target_morph = std::max(0.0f, std::min(1.0f, morph_raw));

        p->smooth_morph += morph_coeff * (target_morph - p->smooth_morph);
        float morph = p->smooth_morph; // 0 = Glassy Clean, 1 = Klon Centaur

        // ─── Circuit Path A: Klon Centaur Dual-Ganged Overdrive ───
        float x_klon = klon_overdrive_stage(clean_buffered, raw_in, p->smooth_gain, magic_mod);
        x_klon = p->klon_treble_eq.process(x_klon);

        // ─── Circuit Path B: Fuzz Face Glassy Cleanup ───
        float x_fuzz = fuzzface_circuit(raw_in, p->smooth_gain);
        float sparkle_hp = p->treble_sparkle_hp.hp(raw_in, 2400.0f, (float)p->sample_rate);
        float sparkle_boost = 1.0f + p->smooth_sparkle * 1.6f;
        float x_glassy_clean = raw_in * 1.08f + sparkle_hp * (0.65f * sparkle_boost);

        float x_fuzz_path = morph * x_fuzz + (1.0f - morph) * x_glassy_clean;

        // ─── Dynamic Volume Crossfade ───
        float x_blended = morph * x_klon + (1.0f - morph) * x_fuzz_path;

        // 3. Master Level & 18V Headroom Makeup
        float makeup = 1.60f;
        float vol = p->smooth_out * p->smooth_out * makeup;
        float wet = x_blended * vol;

        // 4. Dry / Wet Mix Blend
        float final_out = (1.0f - p->smooth_mix) * dry + p->smooth_mix * wet;

        if (final_out > 1.2f)  final_out = 1.2f  - 0.2f * expf(-(final_out - 1.2f));
        if (final_out < -1.2f) final_out = -1.2f + 0.2f * expf(final_out + 1.2f);

        out[i] = final_out;
    }
}

static void deactivate(LV2_Handle) {}

static void cleanup(LV2_Handle handle) {
    delete (CyberKlon*)handle;
}

static const void* extension_data(const char*) {
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
