#pragma once

#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Denormal prevention
inline float flush_denormal(float v) {
    if (std::abs(v) < 1.0e-15f) return 0.0f;
    return v;
}

// 2nd-Order Butterworth High-Pass Filter for Sub-Rumble cut
class BiquadHPF {
private:
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
    float current_freq;
    double sample_rate;

public:
    BiquadHPF() : b0(1), b1(0), b2(0), a1(0), a2(0),
                  x1(0), x2(0), y1(0), y2(0),
                  current_freq(0), sample_rate(48000.0) {}

    void init(double sr) {
        sample_rate = sr;
        reset();
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
        current_freq = 0.0f;
    }

    void setFrequency(float freq) {
        freq = std::clamp(freq, 20.0f, (float)(sample_rate * 0.45));
        if (std::abs(freq - current_freq) < 0.1f) return;
        current_freq = freq;

        float w0 = (float)(2.0 * M_PI * freq / sample_rate);
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float alpha = sinw / 1.41421356f; // Q = 0.7071 (Butterworth)

        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = (-(1.0f + cosw)) / a0;
        b2 = ((1.0f + cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    inline float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        y = flush_denormal(y);
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// Single Dynamic Peaking/Notch Filter Stage
struct BiquadNotchStage {
    float center_freq;
    float q;
    float w0, cosw, sinw, alpha;
    float x1, x2, y1, y2;
    float smoothed_gain_db;

    void init(float f, float q_val, double sample_rate) {
        q = q_val;
        x1 = x2 = y1 = y2 = 0.0f;
        smoothed_gain_db = 0.0f;
        updateFrequency(f, sample_rate);
    }

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
        smoothed_gain_db = 0.0f;
    }

    void updateFrequency(float f, double sample_rate) {
        center_freq = std::clamp(f, 30.0f, (float)(sample_rate * 0.45));
        w0 = (float)(2.0 * M_PI * center_freq / sample_rate);
        cosw = std::cos(w0);
        sinw = std::sin(w0);
        alpha = sinw / (2.0f * q);
    }

    inline float process(float in_sample, float target_cut_db) {
        // Smooth gain transition
        smoothed_gain_db += 0.05f * (-target_cut_db - smoothed_gain_db);

        if (std::abs(smoothed_gain_db) < 0.05f) {
            x2 = x1; x1 = in_sample;
            y2 = y1; y1 = in_sample;
            return in_sample;
        }

        float A = std::pow(10.0f, smoothed_gain_db / 40.0f); // sqrt(G)
        float a0 = 1.0f + alpha / A;
        float b0 = (1.0f + alpha * A) / a0;
        float b1 = (-2.0f * cosw) / a0;
        float b2 = (1.0f - alpha * A) / a0;
        float a1 = b1;
        float a2 = (1.0f - alpha / A) / a0;

        float out = b0 * in_sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        out = flush_denormal(out);
        x2 = x1; x1 = in_sample;
        y2 = y1; y1 = out;
        return out;
    }
};

// Dynamic Parametric Notch Band with Harmonic Tracking (Fundamental, 2nd Harmonic 75%, 3rd Harmonic 25%)
class DynamicNotchBand {
private:
    double sample_rate;

    float center_freq;
    float current_q;
    float max_depth_db;
    float sensitivity; // 0.0 to 1.0
    float harmonic_scale; // 0.0 to 1.0

    // Bandpass sidechain detector filter states (tuned to fundamental)
    float bp_b0, bp_b2, bp_a1, bp_a2;
    float bp_x1, bp_x2, bp_y1, bp_y2;

    // Envelope follower states
    float envelope;
    float attack_coeff;
    float release_coeff;

    // 3 Harmonically-Linked Notch Stages:
    // Stage 0: Fundamental (100% cut)
    // Stage 1: 2nd Harmonic (75% cut)
    // Stage 2: 3rd Harmonic (25% cut)
    BiquadNotchStage stage_f0;
    BiquadNotchStage stage_2f0;
    BiquadNotchStage stage_3f0;

    // Last processed detector sample for listen mode
    float last_bp_sample;

    // Metering
    float current_reduction_db;

public:
    DynamicNotchBand() : sample_rate(48000.0), center_freq(100.0f), current_q(3.0f),
                         max_depth_db(18.0f), sensitivity(0.5f), harmonic_scale(1.0f),
                         bp_b0(0), bp_b2(0), bp_a1(0), bp_a2(0),
                         bp_x1(0), bp_x2(0), bp_y1(0), bp_y2(0),
                         envelope(0), attack_coeff(0), release_coeff(0),
                         last_bp_sample(0.0f), current_reduction_db(0.0f) {}

    void init(double sr, float default_freq, float default_q, float default_depth) {
        sample_rate = sr;
        center_freq = default_freq;
        current_q = default_q;
        max_depth_db = default_depth;
        sensitivity = 0.5f;
        harmonic_scale = 1.0f;

        // Attack ~ 1.5ms (ultra fast to catch palm thump immediately)
        attack_coeff = 1.0f - std::exp(-1.0f / (float)(0.0015 * sample_rate));
        // Release ~ 60ms (musical acoustic decay)
        release_coeff = 1.0f - std::exp(-1.0f / (float)(0.060 * sample_rate));

        stage_f0.init(center_freq, current_q, sample_rate);
        stage_2f0.init(2.0f * center_freq, current_q * 1.35f, sample_rate);
        stage_3f0.init(3.0f * center_freq, current_q * 1.70f, sample_rate);

        reset();
        updateFrequencies(center_freq, current_q);
    }

    void reset() {
        bp_x1 = bp_x2 = bp_y1 = bp_y2 = 0.0f;
        stage_f0.reset();
        stage_2f0.reset();
        stage_3f0.reset();
        envelope = 0.0f;
        last_bp_sample = 0.0f;
        current_reduction_db = 0.0f;
    }

    void updateFrequencies(float freq, float q) {
        freq = std::clamp(freq, 30.0f, (float)(sample_rate * 0.14)); // Keep 3f0 safely below Nyquist
        q = std::clamp(q, 0.5f, 10.0f);

        center_freq = freq;
        current_q = q;

        stage_f0.updateFrequency(center_freq, sample_rate);
        stage_2f0.updateFrequency(2.0f * center_freq, sample_rate);
        stage_3f0.updateFrequency(3.0f * center_freq, sample_rate);

        // Constant skirt gain bandpass filter for fundamental detection:
        float w0 = (float)(2.0 * M_PI * center_freq / sample_rate);
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float alpha = sinw / (2.0f * current_q);

        float bp_a0 = 1.0f + alpha;
        bp_b0 = alpha / bp_a0;
        bp_b2 = -alpha / bp_a0;
        bp_a1 = (-2.0f * cosw) / bp_a0;
        bp_a2 = (1.0f - alpha) / bp_a0;
    }

    void setParams(float freq, float sens_percent, float depth_db, float harm_percent, float q = 3.0f) {
        if (std::abs(freq - center_freq) > 0.1f || std::abs(q - current_q) > 0.05f) {
            updateFrequencies(freq, q);
        }
        sensitivity = std::clamp(sens_percent * 0.01f, 0.0f, 1.0f);
        max_depth_db = std::clamp(depth_db, 0.0f, 24.0f);
        harmonic_scale = std::clamp(harm_percent * 0.01f, 0.0f, 1.0f);
    }

    inline float process(float in_sample, bool listen_band = false) {
        // 1. Bandpass filter for detection of fundamental resonance
        float bp_y = bp_b0 * in_sample + bp_b2 * bp_x2 - bp_a1 * bp_y1 - bp_a2 * bp_y2;
        bp_y = flush_denormal(bp_y);
        bp_x2 = bp_x1; bp_x1 = in_sample;
        bp_y2 = bp_y1; bp_y1 = bp_y;
        last_bp_sample = bp_y;

        if (listen_band) {
            return bp_y * 1.5f;
        }

        // 2. Envelope follower on bandpassed signal
        float rect = std::abs(bp_y);
        if (rect > envelope) {
            envelope += attack_coeff * (rect - envelope);
        } else {
            envelope += release_coeff * (rect - envelope);
        }
        envelope = flush_denormal(envelope);

        // 3. Dynamic Threshold
        float thresh_db = -6.0f - (sensitivity * 36.0f);
        float thresh_lin = std::pow(10.0f, thresh_db / 20.0f);

        float fundamental_cut_db = 0.0f;
        if (envelope > thresh_lin && envelope > 1.0e-5f) {
            float env_db = 20.0f * std::log10(envelope);
            float overshoot_db = env_db - thresh_db;
            fundamental_cut_db = std::min(max_depth_db, overshoot_db * 1.5f);
        }

        current_reduction_db = fundamental_cut_db;

        // Calculate Harmonic Cuts:
        // Fundamental: 100% of reduction
        // 2nd Harmonic: 75% of reduction
        // 3rd Harmonic: 25% of reduction
        float cut_f0  = fundamental_cut_db;
        float cut_2f0 = fundamental_cut_db * 0.75f * harmonic_scale;
        float cut_3f0 = fundamental_cut_db * 0.25f * harmonic_scale;

        // Process through the 3 cascaded stages
        float s = in_sample;
        s = stage_f0.process(s, cut_f0);
        s = stage_2f0.process(s, cut_2f0);
        s = stage_3f0.process(s, cut_3f0);

        return s;
    }

    float getReductionDb() const {
        return current_reduction_db;
    }

    float getListenSample() const {
        return last_bp_sample;
    }
};

// Zero-Latency Transparent Soft-Knee Safety Ceiling & Slew Limiter
// - 100% linear 1:1 bit-exact passthrough for normal dynamic playing below threshold
// - Hyperbolic soft-knee ceiling smoothly prevents rogue piezo transients from ever exceeding ceiling M
// - 0 samples latency (no lookahead buffer, live performance ready)
class SoftKneePeakClamp {
private:
    float ceiling_lin;
    float thresh_lin;
    float span;
    float current_clamp_db;

public:
    SoftKneePeakClamp() : ceiling_lin(1.0f), thresh_lin(1.0f), span(0.1f), current_clamp_db(0.0f) {}

    void setCeiling(float clamp_db) {
        clamp_db = std::clamp(clamp_db, -12.0f, 0.0f);
        if (std::abs(clamp_db - current_clamp_db) < 0.05f) return;
        current_clamp_db = clamp_db;

        if (current_clamp_db >= -0.1f) {
            // Off / Bypassed: Ceiling at 1.0 (0 dBFS) with full 1:1 range
            ceiling_lin = 1.0f;
            thresh_lin = 1.0f;
            span = 0.05f;
        } else {
            // Target ceiling amplitude
            ceiling_lin = std::pow(10.0f, current_clamp_db / 20.0f);
            // Soft-knee starts at 75% of ceiling (~2.5 dB below ceiling)
            thresh_lin = ceiling_lin * 0.75f;
            span = ceiling_lin - thresh_lin;
        }
    }

    inline float process(float in_sample) {
        if (current_clamp_db >= -0.1f) {
            return in_sample; // Complete 100% bypass
        }

        float abs_s = std::abs(in_sample);
        if (abs_s <= thresh_lin) {
            // 100% bit-exact linear uncompressed passthrough
            return in_sample;
        }

        // Soft-knee hyperbolic curve: seamlessly continuous derivative (C1)
        float excess = abs_s - thresh_lin;
        float clamped = thresh_lin + span * std::tanh(excess / span);
        float sign = (in_sample >= 0.0f) ? 1.0f : -1.0f;
        return flush_denormal(sign * clamped);
    }
};

// Top-Level Piezo Thump Killer Processing Engine
class PiezoThumpEngine {
private:
    double sample_rate;

    BiquadHPF hpf;
    DynamicNotchBand thump_band; // Band 1: 60-180 Hz
    DynamicNotchBand mud_band;   // Band 2: 150-400 Hz
    SoftKneePeakClamp peak_clamp; // Zero-latency safety ceiling

    float smoothed_out_gain;

public:
    PiezoThumpEngine() : sample_rate(48000.0), smoothed_out_gain(1.0f) {}

    void init(double sr) {
        sample_rate = sr;
        hpf.init(sr);
        hpf.setFrequency(40.0f);

        // Thump band: default 95 Hz, Q = 3.0, Depth = 14 dB
        thump_band.init(sr, 95.0f, 3.0f, 14.0f);

        // Mud band: default 220 Hz, Q = 2.4, Depth = 10 dB
        mud_band.init(sr, 220.0f, 2.4f, 10.0f);

        // Peak clamp: default -4 dBFS ceiling
        peak_clamp.setCeiling(-4.0f);

        smoothed_out_gain = 1.0f;
    }

    void reset() {
        hpf.reset();
        thump_band.reset();
        mud_band.reset();
    }

    void process(const float* in, float* out, uint32_t sample_count,
                 float hpf_freq,
                 float thump_freq, float thump_sens, float thump_depth,
                 float mud_freq, float mud_sens, float mud_depth,
                 float harmonics_pct,
                 float peak_clamp_db,
                 float listen_mode, float out_gain_db)
    {
        // Update filter parameters
        hpf.setFrequency(hpf_freq);
        thump_band.setParams(thump_freq, thump_sens, thump_depth, harmonics_pct, 3.0f);
        mud_band.setParams(mud_freq, mud_sens, mud_depth, harmonics_pct, 2.4f);
        peak_clamp.setCeiling(peak_clamp_db);

        int mode = (int)(listen_mode + 0.5f); // 0 = Normal, 1 = Listen Thump, 2 = Listen Mud

        float target_out_gain = std::pow(10.0f, out_gain_db / 20.0f);

        for (uint32_t i = 0; i < sample_count; ++i) {
            smoothed_out_gain += 0.005f * (target_out_gain - smoothed_out_gain);
            float s = in[i];

            if (mode == 1) {
                out[i] = thump_band.process(s, true);
                continue;
            } else if (mode == 2) {
                out[i] = mud_band.process(s, true);
                continue;
            }

            // Normal processing chain:
            // 1. Sub-Rumble Cut (HPF)
            s = hpf.process(s);

            // 2. Dynamic Thump Notch (Fundamental + 2nd harm @ 75% + 3rd harm @ 25%)
            s = thump_band.process(s, false);

            // 3. Dynamic Mud Notch (Fundamental + 2nd harm @ 75% + 3rd harm @ 25%)
            s = mud_band.process(s, false);

            // 4. Zero-Latency Soft-Knee Safety Ceiling (Shaves off rogue piezo spikes without compressing normal dynamics)
            s = peak_clamp.process(s);

            // 5. Clean Output Trim
            out[i] = s * smoothed_out_gain;
        }
    }

    float getThumpReductionDb() const {
        return thump_band.getReductionDb();
    }

    float getMudReductionDb() const {
        return mud_band.getReductionDb();
    }
};
