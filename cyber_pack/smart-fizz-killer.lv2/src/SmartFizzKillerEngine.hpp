#ifndef SMART_FIZZ_KILLER_ENGINE_HPP
#define SMART_FIZZ_KILLER_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float SFK_PI = 3.14159265358979323846f;
constexpr float SFK_TWO_PI = 6.28318530717958647692f;

// ============================================================================
// Stage 1: Cascaded All-Pass Phase Smear
// ============================================================================
// Smears high-frequency transient alignment that causes "ice-pick" harshness
// in digital distortion. 4 cascaded 2nd-order all-pass filters create a
// frequency-dependent group delay that softens the attack without reducing volume.

struct AllPassSmear {
    // 2nd-order all-pass: y = a2*x + a1*x1 + x2 - a1*y1 - a2*y2
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float a1 = 0, a2 = 0;

    void reset() { x1 = x2 = y1 = y2 = 0; }

    void setFreq(float freq, float Q, float sampleRate) {
        float w0 = SFK_TWO_PI * std::clamp(freq, 20.0f, sampleRate * 0.49f) / sampleRate;
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;
        a1 = (-2.0f * std::cos(w0)) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    inline float process(float x) {
        float y = a2 * x + a1 * x1 + x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
};

// ============================================================================
// Stage 2: Dynamic High-Frequency Suppressor (Smart Fizz Cut)
// ============================================================================
// Envelope-followed de-fizzer. Detects energy in the fizz band (4-9kHz) and
// dynamically cuts highs ONLY when they spike past a threshold. Clean playing
// stays bright; distortion fizz gets instantly clamped.

struct DynamicFizzCut {
    // Envelope follower state
    float envelope = 0;
    // High-shelf filter state
    float hsx1 = 0, hsx2 = 0, hsy1 = 0, hsy2 = 0;
    // Bandpass detector state
    float bpx1 = 0, bpx2 = 0, bpy1 = 0, bpy2 = 0;
    float bp_b0 = 0, bp_b1 = 0, bp_b2 = 0, bp_a1 = 0, bp_a2 = 0;

    void reset() {
        envelope = 0;
        hsx1 = hsx2 = hsy1 = hsy2 = 0;
        bpx1 = bpx2 = bpy1 = bpy2 = 0;
    }

    void setupDetector(float freq, float sampleRate) {
        // Bandpass filter for fizz detection
        float w0 = SFK_TWO_PI * std::clamp(freq, 100.0f, sampleRate * 0.49f) / sampleRate;
        float Q = 1.5f; // Moderate bandwidth
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;
        bp_b0 = alpha / a0;
        bp_b1 = 0.0f;
        bp_b2 = -alpha / a0;
        bp_a1 = (-2.0f * std::cos(w0)) / a0;
        bp_a2 = (1.0f - alpha) / a0;
    }

    inline float detectFizz(float x) {
        float y = bp_b0 * x + bp_b1 * bpx1 + bp_b2 * bpx2 - bp_a1 * bpy1 - bp_a2 * bpy2;
        bpx2 = bpx1; bpx1 = x;
        bpy2 = bpy1; bpy1 = y;
        return y;
    }

    // Apply dynamic high-shelf cut
    inline float applyShelf(float x, float gain, float freq, float sampleRate) {
        // 1st-order high-shelf: simple and CPU-friendly
        float w0 = SFK_TWO_PI * std::clamp(freq, 100.0f, sampleRate * 0.49f) / sampleRate;
        float cosw = std::cos(w0);
        float sinw = std::sin(w0);
        float A = std::sqrt(std::max(0.001f, gain));
        float alpha = sinw / 2.0f * std::sqrt((A + 1.0f / A) * 1.0f + 2.0f);

        float a0 = (A + 1.0f) - (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha;
        float b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw + 2.0f * std::sqrt(A) * alpha)) / a0;
        float b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
        float b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha)) / a0;
        float a1v = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
        float a2v = ((A + 1.0f) - (A - 1.0f) * cosw - 2.0f * std::sqrt(A) * alpha) / a0;

        float y = b0 * x + b1 * hsx1 + b2 * hsx2 - a1v * hsy1 - a2v * hsy2;
        hsx2 = hsx1; hsx1 = x;
        hsy2 = hsy1; hsy1 = y;
        return y;
    }
};

// ============================================================================
// Stage 3: Soft Clipper Waveshaper with 2× Oversampling
// ============================================================================
// tanh() curve rounds off sharp digital peaks, converting jagged clipping into
// smooth even-order harmonics. 2× oversampling prevents aliasing from the
// nonlinear function.

struct SoftClipper {
    // Simple 2× oversampling anti-alias filter states
    float upX1 = 0, upY1 = 0;   // Upsample LP
    float dnX1 = 0, dnY1 = 0;   // Downsample LP
    float lpCoeff = 0;

    void reset() {
        upX1 = upY1 = dnX1 = dnY1 = 0;
    }

    void prepare(float sampleRate) {
        // Simple 1-pole LP at Nyquist/2 for anti-alias
        float fc = sampleRate * 0.45f; // Slightly below Nyquist
        float twoSR = sampleRate * 2.0f;
        lpCoeff = 1.0f - std::exp(-SFK_TWO_PI * fc / twoSR);
    }

    inline float process(float x, float drive, float symmetry) {
        // drive: 0..1 (0=bypass, 1=heavy saturation)
        // symmetry: 0..1 (0=symmetric/odd harmonics, 1=asymmetric/even harmonics)

        if (drive < 0.001f) return x; // Bypass when drive is zero

        float gain = 1.0f + drive * 8.0f; // 1× to 9× gain into clipper
        float asymOffset = symmetry * 0.3f; // DC bias for asymmetric clipping

        // Upsample: insert zero, then LP filter (×2 rate)
        float s0 = x * gain + asymOffset;
        float s1 = 0.0f; // Zero-stuffed sample

        // Process at 2× rate
        // Sample 0
        upY1 += (s0 - upY1) * lpCoeff;
        float shaped0 = std::tanh(upY1 * 1.5f);

        // Sample 1 (interpolated zero)
        upY1 += (s1 - upY1) * lpCoeff;
        float shaped1 = std::tanh(upY1 * 1.5f);

        // Downsample: LP filter then decimate
        dnY1 += (shaped0 - dnY1) * lpCoeff;
        float out = dnY1;
        dnY1 += (shaped1 - dnY1) * lpCoeff;

        // Remove DC offset from asymmetry
        out -= asymOffset * 0.5f;

        // Normalize output level (tanh compresses, so compensate)
        float makeupGain = 1.0f / std::tanh(gain * 1.5f + 0.001f);
        out *= makeupGain;

        return out;
    }
};

// ============================================================================
// Stage 4: Tilt EQ + Body Boost + Final High Cut
// ============================================================================
// Tilt filter: single control that simultaneously darkens highs and thickens lows.
// Body: resonant bell boost at ~350Hz for physical "push."
// High-cut: gentle 2nd-order LP for final polish.

struct TiltEQ {
    // Tilt filter (1-pole)
    float tiltState = 0;
    // Body bell filter
    float bellX1 = 0, bellX2 = 0, bellY1 = 0, bellY2 = 0;
    // High-cut LP filter
    float hcX1 = 0, hcX2 = 0, hcY1 = 0, hcY2 = 0;

    void reset() {
        tiltState = 0;
        bellX1 = bellX2 = bellY1 = bellY2 = 0;
        hcX1 = hcX2 = hcY1 = hcY2 = 0;
    }

    inline float processTilt(float x, float tiltAmount, float sampleRate) {
        // tiltAmount: -1 to +1 (-1=dark, 0=flat, +1=bright)
        float freq = 800.0f; // Tilt pivot frequency
        float coeff = 1.0f - std::exp(-SFK_TWO_PI * freq / sampleRate);
        tiltState += (x - tiltState) * coeff;
        float lo = tiltState;
        float hi = x - lo;
        // Mix based on tilt: negative darkens (more lo, less hi)
        float loGain = 1.0f + tiltAmount * -0.5f; // -1→1.5, 0→1, +1→0.5
        float hiGain = 1.0f + tiltAmount * 0.5f;  // -1→0.5, 0→1, +1→1.5
        return lo * loGain + hi * hiGain;
    }

    inline float processBell(float x, float gainDb, float sampleRate) {
        // Parametric bell at 350Hz
        if (gainDb < 0.1f) return x;
        float freq = 350.0f;
        float Q = 1.2f;
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = SFK_TWO_PI * freq / sampleRate;
        float alpha = std::sin(w0) / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        float b0 = (1.0f + alpha * A) / a0;
        float b1 = (-2.0f * std::cos(w0)) / a0;
        float b2 = (1.0f - alpha * A) / a0;
        float a1 = b1; // Same for peaking
        float a2 = (1.0f - alpha / A) / a0;

        float y = b0 * x + b1 * bellX1 + b2 * bellX2 - a1 * bellY1 - a2 * bellY2;
        bellX2 = bellX1; bellX1 = x;
        bellY2 = bellY1; bellY1 = y;
        return y;
    }

    inline float processHighCut(float x, float cutFreq, float sampleRate) {
        // 2nd-order Butterworth low-pass
        float w0 = SFK_TWO_PI * std::clamp(cutFreq, 1000.0f, sampleRate * 0.49f) / sampleRate;
        float cosw = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * 0.707f); // Q = 0.707 (Butterworth)

        float a0 = 1.0f + alpha;
        float b0 = ((1.0f - cosw) / 2.0f) / a0;
        float b1 = (1.0f - cosw) / a0;
        float b2 = b0;
        float a1 = (-2.0f * cosw) / a0;
        float a2 = (1.0f - alpha) / a0;

        float y = b0 * x + b1 * hcX1 + b2 * hcX2 - a1 * hcY1 - a2 * hcY2;
        hcX2 = hcX1; hcX1 = x;
        hcY2 = hcY1; hcY1 = y;
        return y;
    }
};

// ============================================================================
// Main Engine: Smart Fizz Killer
// ============================================================================

class SmartFizzKillerEngine {
public:
    SmartFizzKillerEngine() = default;

    void prepare(double sampleRate) {
        mSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 4; ++i) smear[ch][i].reset();
            fizzCut[ch].reset();
            clipper[ch].reset();
            clipper[ch].prepare(static_cast<float>(mSampleRate));
            tiltEQ[ch].reset();
        }
    }

    void reset() { prepare(mSampleRate); }

    // --- Parameter Setters ---
    void setSmearFreq(float hz)        { mSmearFreq = std::clamp(hz, 800.0f, 6000.0f); }
    void setFizzFreq(float hz)         { mFizzFreq = std::clamp(hz, 3000.0f, 8000.0f); }
    void setFizzDepth(float db)        { mFizzDepthDb = std::clamp(db, 0.0f, 18.0f); }
    void setFizzSensitivity(float pct) { mFizzSens = std::clamp(pct / 100.0f, 0.0f, 1.0f); }
    void setSaturation(float pct)      { mSatDrive = std::clamp(pct / 100.0f, 0.0f, 1.0f); }
    void setSymmetry(float pct)        { mSatSymmetry = std::clamp(pct / 100.0f, 0.0f, 1.0f); }
    void setTilt(float pct)            { mTilt = std::clamp(pct / 100.0f, -1.0f, 1.0f); }
    void setBody(float db)             { mBodyDb = std::clamp(db, 0.0f, 12.0f); }
    void setHighCut(float hz)          { mHighCutFreq = std::clamp(hz, 4000.0f, 16000.0f); }
    void setMix(float pct)             { mMix = std::clamp(pct / 100.0f, 0.0f, 1.0f); }
    void setOutputDb(float db)         { mOutputGain = std::pow(10.0f, std::clamp(db, -12.0f, 6.0f) / 20.0f); }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t numSamples) {
        if (!outL || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);
        const bool isStereo = (inR && outR && inR != inL);

        // Update all-pass smear filters
        for (int ch = 0; ch < 2; ++ch) {
            float qSpread[4] = { 0.5f, 0.8f, 1.2f, 2.0f };
            for (int i = 0; i < 4; ++i) {
                smear[ch][i].setFreq(mSmearFreq * (0.8f + i * 0.15f), qSpread[i], sRate);
            }
            fizzCut[ch].setupDetector(mFizzFreq, sRate);
        }

        // Envelope follower coefficients for fizz detection
        float envAtt = 1.0f - std::exp(-1.0f / (1.0f * sRate / 1000.0f));   // 1ms attack
        float envRel = 1.0f - std::exp(-1.0f / (30.0f * sRate / 1000.0f));  // 30ms release

        // Fizz threshold from sensitivity
        float fizzThreshold = std::pow(10.0f, (-60.0f + mFizzSens * 50.0f) / 20.0f);

        const float* inputs[2] = { inL, isStereo ? inR : inL };
        float* outputs[2] = { outL, isStereo ? outR : outL };
        int numCh = isStereo ? 2 : 1;

        for (int ch = 0; ch < numCh; ++ch) {
            const float* inBuf = inputs[ch];
            float* outBuf = outputs[ch];

            for (uint32_t s = 0; s < numSamples; ++s) {
                float dry = inBuf[s];
                float x = dry;

                // ===== Stage 1: Phase Smear =====
                for (int i = 0; i < 4; ++i) {
                    x = smear[ch][i].process(x);
                }

                // ===== Stage 2: Dynamic Fizz Cut =====
                if (mFizzDepthDb > 0.1f) {
                    float fizzSignal = fizzCut[ch].detectFizz(x);
                    float fizzLevel = std::abs(fizzSignal);

                    // Envelope follower
                    float envCoeff = (fizzLevel > fizzCut[ch].envelope) ? envAtt : envRel;
                    fizzCut[ch].envelope += (fizzLevel - fizzCut[ch].envelope) * envCoeff;

                    // Dynamic gain reduction: only cut when fizz exceeds threshold
                    float fizzExcess = std::max(0.0f, fizzCut[ch].envelope - fizzThreshold);
                    float cutRatio = std::min(1.0f, fizzExcess / (fizzThreshold + 1e-6f));
                    float shelfGainDb = -mFizzDepthDb * cutRatio;
                    float shelfGainLinear = std::pow(10.0f, shelfGainDb / 20.0f);

                    x = fizzCut[ch].applyShelf(x, shelfGainLinear, mFizzFreq * 0.7f, sRate);
                }

                // ===== Stage 3: Soft Clipper =====
                x = clipper[ch].process(x, mSatDrive, mSatSymmetry);

                // ===== Stage 4: Tilt EQ + Body + High Cut =====
                x = tiltEQ[ch].processTilt(x, mTilt, sRate);
                x = tiltEQ[ch].processBell(x, mBodyDb, sRate);
                x = tiltEQ[ch].processHighCut(x, mHighCutFreq, sRate);

                // ===== Dry/Wet Mix + Output Trim =====
                float wet = x;
                float mixed = dry * (1.0f - mMix) + wet * mMix;
                outBuf[s] = mixed * mOutputGain;
            }
        }

        if (!isStereo && outR && outR != outL) {
            std::memcpy(outR, outL, numSamples * sizeof(float));
        }
    }

private:
    double mSampleRate = 48000.0;

    // Stage 1: 4 cascaded all-pass filters per channel
    AllPassSmear smear[2][4];

    // Stage 2: Dynamic fizz cut per channel
    DynamicFizzCut fizzCut[2];

    // Stage 3: Soft clipper per channel
    SoftClipper clipper[2];

    // Stage 4: Tilt EQ per channel
    TiltEQ tiltEQ[2];

    // Parameters
    float mSmearFreq = 2500.0f;
    float mFizzFreq = 5500.0f;
    float mFizzDepthDb = 6.0f;
    float mFizzSens = 0.5f;
    float mSatDrive = 0.25f;
    float mSatSymmetry = 0.3f;
    float mTilt = -0.3f;
    float mBodyDb = 3.0f;
    float mHighCutFreq = 9000.0f;
    float mMix = 1.0f;
    float mOutputGain = 1.0f;
};

} // namespace AudioDSP

#endif // SMART_FIZZ_KILLER_ENGINE_HPP
