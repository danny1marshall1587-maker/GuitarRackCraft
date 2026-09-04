#ifndef MAINS_HUM_ENGINE_HPP
#define MAINS_HUM_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float TWO_PI_F = 6.28318530717958647692f;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

/**
 * @brief High-Q Precision Notch Filter (Biquad) with variable cut depth
 */
struct PrecisionNotchFilter {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void setup(float freq, float sampleRate, float Q, float depthDb) {
        freq = std::clamp(freq, 20.0f, sampleRate * 0.49f);
        float w0 = TWO_PI_F * freq / sampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * Q);

        float gain = dbToGain(depthDb); // e.g. -40dB = 0.01

        // Standard Notch with variable depth blending
        float b0_n = 1.0f;
        float b1_n = -2.0f * cosw0;
        float b2_n = 1.0f;
        float a0_n = 1.0f + alpha;
        float a1_n = -2.0f * cosw0;
        float a2_n = 1.0f - alpha;

        // Scale by depth
        b0 = (b0_n + (1.0f - gain) * alpha * 0.0f) / a0_n * (1.0f - gain) + gain;
        b1 = b1_n / a0_n * (1.0f - gain) + gain * 0.0f;
        b2 = b2_n / a0_n * (1.0f - gain);
        a1 = a1_n / a0_n;
        a2 = a2_n / a0_n;
    }

    inline float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

/**
 * @brief Professional Electric Guitar Mains Hum & Single-Coil Buzz Remover Engine.
 * Implements a 16-stage harmonic notch filter array (50Hz / 60Hz fundamental + overtones up to 1kHz)
 * with adaptive high-frequency buzz suppressor and "Hum Only / Difference" audition mode.
 */
class MainsHumEngine {
public:
    MainsHumEngine() = default;
    ~MainsHumEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        mNotches.resize(16);

        mGridMode = 0; // 0 = 50 Hz (UK/EU), 1 = 60 Hz (US)
        mFineTune = 0.0f;
        mDepthDb = -40.0f;
        mHarmonicsCount = 10;
        mQ = 45.0f;
        mBuzzKill = 0.5f;
        mListenHumOnly = false;

        mBuzzEnvelope = 0.0f;
        mBuzzLp = 0.0f;
        mBuzzHp = 0.0f;

        updateFilters();
        reset();
    }

    void reset() {
        for (auto& n : mNotches) n.reset();
        mBuzzEnvelope = 0.0f;
        mBuzzLp = 0.0f;
        mBuzzHp = 0.0f;
    }

    void setGridMode(int mode) {
        mGridMode = std::clamp(mode, 0, 1);
        updateFilters();
    }

    void setFineTune(float offsetHz) {
        mFineTune = std::clamp(offsetHz, -3.0f, 3.0f);
        updateFilters();
    }

    void setDepthDb(float depth) {
        mDepthDb = std::clamp(depth, -60.0f, 0.0f);
        updateFilters();
    }

    void setHarmonicsCount(int count) {
        mHarmonicsCount = std::clamp(count, 1, 16);
        updateFilters();
    }

    void setQ(float qVal) {
        mQ = std::clamp(qVal, 15.0f, 80.0f);
        updateFilters();
    }

    void setBuzzKill(float buzz01) {
        mBuzzKill = std::clamp(buzz01, 0.0f, 1.0f);
    }

    void setListenHumOnly(bool listen) {
        mListenHumOnly = listen;
    }

    void updateFilters() {
        float baseFreq = (mGridMode == 0 ? 50.0f : 60.0f) + mFineTune;
        float sRate = static_cast<float>(mSampleRate);

        for (int i = 0; i < 16; ++i) {
            float harmonicFreq = baseFreq * (i + 1);
            if (harmonicFreq < sRate * 0.48f) {
                // Progressive harmonic depth slope
                float harmonicDepth = mDepthDb * std::pow(0.92f, static_cast<float>(i));
                mNotches[i].setup(harmonicFreq, sRate, mQ, harmonicDepth);
            }
        }
    }

    void process(const float* in, float* out, uint32_t numSamples) {
        if (!in || !out || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);

        // High frequency buzz detector filters (1 kHz - 6 kHz)
        float buzzHpCoeff = 1.0f - std::exp(-TWO_PI_F * 1200.0f / sRate);
        float buzzLpCoeff = 1.0f - std::exp(-TWO_PI_F * 6500.0f / sRate);

        float buzzThreshold = 0.0008f * (1.0f - mBuzzKill * 0.7f);

        for (uint32_t s = 0; s < numSamples; ++s) {
            float original = in[s];
            float filtered = original;

            // 1. Pass through active 16-stage harmonic notch filters
            for (int i = 0; i < mHarmonicsCount; ++i) {
                filtered = mNotches[i].process(filtered);
            }

            // 2. High-Frequency Single-Coil Buzz Suppressor
            if (mBuzzKill > 0.01f) {
                mBuzzHp += (filtered - mBuzzHp) * buzzHpCoeff;
                float highBand = filtered - mBuzzHp;

                mBuzzLp += (highBand - mBuzzLp) * buzzLpCoeff;
                float buzzBand = mBuzzLp;

                float absBuzz = std::abs(buzzBand);
                if (absBuzz > mBuzzEnvelope) {
                    mBuzzEnvelope = mBuzzEnvelope * 0.7f + absBuzz * 0.3f;
                } else {
                    mBuzzEnvelope *= 0.998f;
                }

                // If signal is quiet, suppress the HF buzz band
                float buzzGate = std::clamp((mBuzzEnvelope - buzzThreshold) / (buzzThreshold * 2.0f + 1e-6f), 0.0f, 1.0f);
                float suppressedHigh = highBand * (buzzGate + (1.0f - buzzGate) * (1.0f - mBuzzKill * 0.85f));

                filtered = (filtered - highBand) + suppressedHigh;
            }

            // 3. Output Mode: Clean Guitar vs Hum Only (Difference)
            if (mListenHumOnly) {
                out[s] = (original - filtered) * 3.0f; // Boost difference for easy auditioning
            } else {
                out[s] = filtered;
            }
        }
    }

private:
    double mSampleRate = 48000.0;
    std::vector<PrecisionNotchFilter> mNotches;

    int   mGridMode = 0; // 0=50Hz, 1=60Hz
    float mFineTune = 0.0f;
    float mDepthDb = -40.0f;
    int   mHarmonicsCount = 10;
    float mQ = 45.0f;
    float mBuzzKill = 0.5f;
    bool  mListenHumOnly = false;

    float mBuzzEnvelope = 0.0f;
    float mBuzzLp = 0.0f;
    float mBuzzHp = 0.0f;
};

} // namespace AudioDSP

#endif // MAINS_HUM_ENGINE_HPP
