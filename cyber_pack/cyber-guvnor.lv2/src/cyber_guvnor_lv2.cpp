/*
 * Cyber Guv'nor - LV2 Plugin
 * 1988 Marshall The Guv'nor Distortion + Dynamic Fuzz Face Glassy Cleanup Morphing
 *
 * Core Concept:
 *   Dynamic volume-driven circuit morphing:
 *     - Full Guitar Volume / Hard Pick Attack -> Marshall The Guv'nor (dual op-amp + back-to-back red LED clipping + 3-band British EQ stack + Hot Rod mod)
 *     - Rolled-Back Volume / Soft Pick Attack  -> Fuzz Face Glassy Cleanup (Germanium chime + treble bleed sparkle)
 *   Controls:
 *     - Gain, Bass, Middle, Treble, Level
 *     - Bias: Center volume threshold for the morph transition
 *     - Sens: Transition slope/sensitivity
 *     - Sparkle: Glassy chime treble boost
 *     - Hot Rod: Low-end resonance and harmonic bite mod
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-guvnor"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_GAIN           = 3,
    PORT_BASS           = 4,
    PORT_MIDDLE         = 5,
    PORT_TREBLE         = 6,
    PORT_LEVEL          = 7,
    PORT_BIAS           = 8,
    PORT_SENS           = 9,
    PORT_SPARKLE        = 10,
    PORT_HOT_ROD        = 11,
    PORT_MIX            = 12,
    PORT_COUNT          = 13
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

// Marshall The Guv'nor Dual Op-Amp + Red LED Soft/Hard Clipping
// Open, high-headroom, punchy British stack character with rich 2nd/3rd harmonics
static inline float guvnor_drive_stage(float x, float gain, bool hot_rod) {
    float g = 1.5f + gain * 14.0f; // 1.5..15.5 gain
    if (hot_rod) g *= 1.3f;
    float in_sig = x * g;

    // Stage 1: Dual Op-Amp variable feedback saturation
    float op1 = in_sig / (1.0f + fabsf(in_sig) * 0.45f);

    // Stage 2: Back-to-back Red LEDs forward voltage clipping (~1.8V threshold emulation)
    // Red LEDs have wider headroom and softer knee than standard silicon diodes
    float led_clip;
    if (op1 >= 0.0f) {
        led_clip = tanhf(op1 * 1.05f) * 0.95f;
    } else {
        float neg_scale = hot_rod ? 0.85f : 0.98f;
        led_clip = -tanhf(-op1 * neg_scale) * 0.95f;
    }

    return led_clip * 0.90f;
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
struct CyberGuvnor {
    const float* audio_in  = nullptr;
    float*       audio_out = nullptr;

    const float* p_bypass   = nullptr;
    const float* p_gain     = nullptr;
    const float* p_bass     = nullptr;
    const float* p_middle   = nullptr;
    const float* p_treble   = nullptr;
    const float* p_level    = nullptr;
    const float* p_bias     = nullptr;
    const float* p_sens     = nullptr;
    const float* p_sparkle  = nullptr;
    const float* p_hot_rod  = nullptr;
    const float* p_mix      = nullptr;

    double sample_rate = 48000.0;

    OnePole hp_in;
    OnePole lp_pre;
    OnePole op_sag;

    // 3-Band Marshall Tonestack
    Biquad eq_bass;
    Biquad eq_middle;
    Biquad eq_treble;
    Biquad hot_rod_boost;
    OnePole treble_sparkle_hp;

    DynamicsDetector detector;
    float atk_coeff      = 0.08f;
    float rel_fast_coeff = 0.008f;
    float rel_slow_coeff = 0.002f;

    // Smoothed parameters
    float smooth_gain    = 0.50f;
    float smooth_bass    = 0.50f;
    float smooth_mid     = 0.50f;
    float smooth_treb    = 0.50f;
    float smooth_level   = 0.70f;
    float smooth_bias    = 0.50f;
    float smooth_sens    = 0.50f;
    float smooth_sparkle = 0.60f;
    float smooth_mix     = 1.00f;
    float smooth_morph   = 0.50f;

    float prev_bass_gain = 0.0f;
    float prev_mid_gain  = 0.0f;
    float prev_treb_gain = 0.0f;
};

static LV2_Handle instantiate(const LV2_Descriptor*, double rate,
                               const char*, const LV2_Feature* const*) {
    auto* p = new CyberGuvnor();
    p->sample_rate = rate > 1000.0 ? rate : 48000.0;

    p->atk_coeff      = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.012f));
    p->rel_fast_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.070f));
    p->rel_slow_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.150f));

    return (LV2_Handle)p;
}

static void connect_port(LV2_Handle handle, uint32_t port, void* data) {
    auto* p = (CyberGuvnor*)handle;
    switch (port) {
        case PORT_AUDIO_IN:  p->audio_in  = (const float*)data; break;
        case PORT_AUDIO_OUT: p->audio_out = (float*)data;       break;
        case PORT_BYPASS:    p->p_bypass   = (const float*)data; break;
        case PORT_GAIN:      p->p_gain     = (const float*)data; break;
        case PORT_BASS:      p->p_bass     = (const float*)data; break;
        case PORT_MIDDLE:    p->p_middle   = (const float*)data; break;
        case PORT_TREBLE:    p->p_treble   = (const float*)data; break;
        case PORT_LEVEL:     p->p_level    = (const float*)data; break;
        case PORT_BIAS:      p->p_bias     = (const float*)data; break;
        case PORT_SENS:      p->p_sens     = (const float*)data; break;
        case PORT_SPARKLE:   p->p_sparkle  = (const float*)data; break;
        case PORT_HOT_ROD:   p->p_hot_rod  = (const float*)data; break;
        case PORT_MIX:       p->p_mix      = (const float*)data; break;
    }
}

static void activate(LV2_Handle handle) {
    auto* p = (CyberGuvnor*)handle;
    p->hp_in.reset();
    p->lp_pre.reset();
    p->op_sag.reset();
    p->eq_bass.reset();
    p->eq_middle.reset();
    p->eq_treble.reset();
    p->hot_rod_boost.reset();
    p->treble_sparkle_hp.reset();
    p->detector.reset();

    p->smooth_gain    = 0.50f;
    p->smooth_bass    = 0.50f;
    p->smooth_mid     = 0.50f;
    p->smooth_treb    = 0.50f;
    p->smooth_level   = 0.70f;
    p->smooth_bias    = 0.50f;
    p->smooth_sens    = 0.50f;
    p->smooth_sparkle = 0.60f;
    p->smooth_mix     = 1.00f;
    p->smooth_morph   = 0.50f;
    p->prev_bass_gain = 0.0f;
    p->prev_mid_gain  = 0.0f;
    p->prev_treb_gain = 0.0f;
}

static void run(LV2_Handle handle, uint32_t n_samples) {
    auto* p = (CyberGuvnor*)handle;
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
    float t_gain    = p->p_gain    ? std::max(0.0f, std::min(100.0f, *p->p_gain)) / 100.0f : 0.50f;
    float t_bass    = p->p_bass    ? std::max(0.0f, std::min(100.0f, *p->p_bass)) / 100.0f : 0.50f;
    float t_mid     = p->p_middle  ? std::max(0.0f, std::min(100.0f, *p->p_middle)) / 100.0f : 0.50f;
    float t_treb    = p->p_treble  ? std::max(0.0f, std::min(100.0f, *p->p_treble)) / 100.0f : 0.50f;
    float t_level   = p->p_level   ? std::max(0.0f, std::min(100.0f, *p->p_level)) / 100.0f : 0.70f;
    float t_bias    = p->p_bias    ? std::max(0.0f, std::min(100.0f, *p->p_bias)) / 100.0f : 0.50f;
    float t_sens    = p->p_sens    ? std::max(0.0f, std::min(100.0f, *p->p_sens)) / 100.0f : 0.50f;
    float t_sparkle = p->p_sparkle ? std::max(0.0f, std::min(100.0f, *p->p_sparkle)) / 100.0f : 0.60f;
    bool  hot_rod   = p->p_hot_rod ? (*p->p_hot_rod > 0.5f) : false;
    float t_mix     = p->p_mix     ? std::max(0.0f, std::min(100.0f, *p->p_mix)) / 100.0f : 1.00f;

    // Extended Wide-Range 3-Band Marshall Tonestack:
    // Bass (90 Hz resonant shelf, -16 dB .. +16 dB for massive thump or tight bass cut)
    float bass_gain = (t_bass - 0.5f) * 32.0f;
    if (fabsf(bass_gain - p->prev_bass_gain) > 0.05f) {
        p->eq_bass.lowShelf(90.0f, bass_gain, (float)p->sample_rate);
        p->prev_bass_gain = bass_gain;
    }

    // Middle (700 Hz interactive mid sweep, -18 dB deep scoop .. +12 dB vocal lead boost)
    float mid_gain = (t_mid - 0.5f) * 30.0f;
    if (fabsf(mid_gain - p->prev_mid_gain) > 0.05f) {
        p->eq_middle.peaking(700.0f, mid_gain, 1.2f, (float)p->sample_rate);
        p->prev_mid_gain = mid_gain;
    }

    // Treble (3.5 kHz British bite & sizzle, -17 dB warm dark .. +17 dB razor cut)
    float treb_gain = (t_treb - 0.5f) * 34.0f;
    if (fabsf(treb_gain - p->prev_treb_gain) > 0.05f) {
        p->eq_treble.highShelf(3500.0f, treb_gain, (float)p->sample_rate);
        p->prev_treb_gain = treb_gain;
    }

    // Hot Rod Mod: +4.0 dB deep resonance at 80 Hz
    if (hot_rod) {
        p->hot_rod_boost.lowShelf(80.0f, 4.0f, (float)p->sample_rate);
    }

    float smooth_coeff = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.010f));
    float morph_coeff  = 1.0f - expf(-1.0f / ((float)p->sample_rate * 0.020f));

    for (uint32_t i = 0; i < n_samples; ++i) {
        p->smooth_gain    += smooth_coeff * (t_gain    - p->smooth_gain);
        p->smooth_bass    += smooth_coeff * (t_bass    - p->smooth_bass);
        p->smooth_mid     += smooth_coeff * (t_mid     - p->smooth_mid);
        p->smooth_treb    += smooth_coeff * (t_treb    - p->smooth_treb);
        p->smooth_level   += smooth_coeff * (t_level   - p->smooth_level);
        p->smooth_bias    += smooth_coeff * (t_bias    - p->smooth_bias);
        p->smooth_sens    += smooth_coeff * (t_sens    - p->smooth_sens);
        p->smooth_sparkle += smooth_coeff * (t_sparkle - p->smooth_sparkle);
        p->smooth_mix     += smooth_coeff * (t_mix     - p->smooth_mix);

        float raw_in = in[i];
        float dry    = raw_in;

        // 1. Input Buffering & Conditioning
        float x = raw_in;
        if (hot_rod) {
            x = p->hot_rod_boost.process(x);
        }

        x = p->hp_in.hp(x, 60.0f, (float)p->sample_rate);
        x = p->lp_pre.lp(x, 9500.0f, (float)p->sample_rate);

        float sag = p->op_sag.lp(fabsf(x), 35.0f, (float)p->sample_rate);
        x *= (1.0f / (1.0f + sag * 0.85f));

        // 2. Dynamic Volume Tracking for Circuit Morphing
        float input_level = p->detector.process(raw_in, p->atk_coeff, p->rel_fast_coeff, p->rel_slow_coeff);

        float center_threshold = 0.015f + p->smooth_bias * 0.22f;
        float transition_width = 0.02f + (1.0f - p->smooth_sens) * 0.15f;

        float morph_raw = 0.5f + (input_level - center_threshold) / (transition_width * 2.0f);
        float target_morph = std::max(0.0f, std::min(1.0f, morph_raw));

        p->smooth_morph += morph_coeff * (target_morph - p->smooth_morph);
        float morph = p->smooth_morph; // 0 = Glassy Clean, 1 = Marshall Guv'nor

        // ─── Circuit Path A: Marshall The Guv'nor ───
        float x_guvnor = guvnor_drive_stage(x, p->smooth_gain, hot_rod);
        x_guvnor = p->eq_bass.process(x_guvnor);
        x_guvnor = p->eq_middle.process(x_guvnor);
        x_guvnor = p->eq_treble.process(x_guvnor);

        // ─── Circuit Path B: Fuzz Face Glassy Cleanup ───
        float x_fuzz = fuzzface_circuit(x, p->smooth_gain);
        float sparkle_hp = p->treble_sparkle_hp.hp(x, 2400.0f, (float)p->sample_rate);
        float sparkle_boost = 1.0f + p->smooth_sparkle * 1.6f;
        float x_glassy_clean = x * 1.08f + sparkle_hp * (0.65f * sparkle_boost);

        float x_fuzz_path = morph * x_fuzz + (1.0f - morph) * x_glassy_clean;

        // ─── Dynamic Volume Crossfade ───
        float x_blended = morph * x_guvnor + (1.0f - morph) * x_fuzz_path;

        // 3. Master Level & Makeup
        float makeup = 1.60f;
        float vol = p->smooth_level * p->smooth_level * makeup;
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
    delete (CyberGuvnor*)handle;
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
