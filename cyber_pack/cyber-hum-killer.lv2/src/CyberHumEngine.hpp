#ifndef CYBER_HUM_ENGINE_HPP
#define CYBER_HUM_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float TWO_PI_F = 6.28318530717958647692f;
constexpr int NUM_HUM_BANDS = 8;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

/**
 * @brief 2nd-Order IIR Bandpass Filter for targeted hum harmonic isolation.
 */
struct HarmonicBandFilter {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    void reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void setupBandpass(float centerFreq, float sampleRate, float Q = 12.0f) {
        centerFreq = std::clamp(centerFreq, 10.0f, sampleRate * 0.49f);
        float w0 = TWO_PI_F * centerFreq / sampleRate;
        float alpha = std::sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;

        b0 = alpha / a0;
        b1 = 0.0f;
        b2 = -alpha / a0;
        a1 = (-2.0f * std::cos(w0)) / a0;
        a2 = (1.0f - alpha) / a0;
        reset();
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
 * @brief Cyber Hum Killer DSP Core.
 *
 * Uses the same spectral subtraction principle as the Cyber Denoiser VST,
 * but with narrow bandpass filters targeting mains hum harmonics (50/60 Hz).
 *
 * The threshold for each band represents the estimated HUM FLOOR level.
 * - When signal in a hum band is BELOW the threshold → it's pure hum → cancel fully.
 * - When guitar plays through a hum band (signal ABOVE threshold) → only the
 *   estimated hum amplitude is subtracted, guitar tone passes clean.
 *
 * IMPORTANT: Threshold 0 dB = "cancel everything" (strips guitar).
 *            Threshold -60 dB = "only cancel very quiet hum" (sensible default).
 */
class CyberHumEngine {
public:
    CyberHumEngine() = default;
    ~CyberHumEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

        mGridMode = 0;       // 0 = 50Hz, 1 = 60Hz
        mReduction = 1.0f;   // 100%
        mSmooth = 1.0f;      // 100%
        mListenHumOnly = false;
        mLowCut = true;
        mLearning = false;
        mLearnFrames = 0;

        // Sensible default: -60 dB hum floor estimate for single-coil pickups
        mManualThresholds.assign(NUM_HUM_BANDS, -60.0f);
        mLearnedNoiseFloor.assign(NUM_HUM_BANDS, 0.0015f);
        mBandEnvelopesL.assign(NUM_HUM_BANDS, 0.0f);
        mBandEnvelopesR.assign(NUM_HUM_BANDS, 0.0f);
        mCurrentGainsL.assign(NUM_HUM_BANDS, 1.0f);
        mCurrentGainsR.assign(NUM_HUM_BANDS, 1.0f);
        mOutputLevels.assign(NUM_HUM_BANDS, 0.0f);

        mDcX1_L = mDcY1_L = 0.0f;
        mDcX1_R = mDcY1_R = 0.0f;

        mFiltersL.resize(NUM_HUM_BANDS);
        mFiltersR.resize(NUM_HUM_BANDS);
        updateBandFrequencies();
        reset();
    }

    void reset() {
        for (auto& f : mFiltersL) f.reset();
        for (auto& f : mFiltersR) f.reset();
        std::fill(mBandEnvelopesL.begin(), mBandEnvelopesL.end(), 0.0f);
        std::fill(mBandEnvelopesR.begin(), mBandEnvelopesR.end(), 0.0f);
        std::fill(mCurrentGainsL.begin(), mCurrentGainsL.end(), 1.0f);
        std::fill(mCurrentGainsR.begin(), mCurrentGainsR.end(), 1.0f);
        std::fill(mOutputLevels.begin(), mOutputLevels.end(), 0.0f);
        mDcX1_L = mDcY1_L = 0.0f;
        mDcX1_R = mDcY1_R = 0.0f;
        mLearning = false;
        mLearnFrames = 0;
    }

    void setGridMode(int mode) {
        if (mGridMode != mode) {
            mGridMode = std::clamp(mode, 0, 1);
            updateBandFrequencies();
        }
    }

    void setThreshold(int band, float dbVal) {
        if (band >= 0 && band < NUM_HUM_BANDS) {
            mManualThresholds[band] = std::clamp(dbVal, -100.0f, 0.0f);
        }
    }

    void setReduction(float red01) { mReduction = std::clamp(red01, 0.0f, 1.0f); }
    void setSmooth(float smooth01) { mSmooth = std::clamp(smooth01, 0.0f, 1.0f); }
    void setListenHumOnly(bool listen) { mListenHumOnly = listen; }
    void setLowCut(bool lowCut) { mLowCut = lowCut; }

    void triggerLearn(bool learn) {
        if (learn && !mLearning) {
            mLearning = true;
            mLearnFrames = 0;
            std::fill(mLearnedNoiseFloor.begin(), mLearnedNoiseFloor.end(), 0.0f);
        }
    }

    bool isLearning() const { return mLearning; }

    float getBandLevel(int band) const {
        if (band >= 0 && band < NUM_HUM_BANDS) {
            return std::clamp(mOutputLevels[band] * 12.0f, 0.0f, 1.0f);
        }
        return 0.0f;
    }

    void updateBandFrequencies() {
        float base = (mGridMode == 0) ? 50.0f : 60.0f;
        float sRate = static_cast<float>(mSampleRate);

        float freqs[NUM_HUM_BANDS] = {
            25.0f,           // DC / Sub
            base,            // Fundamental
            base * 2.0f,     // 2nd Harmonic
            base * 3.0f,     // 3rd Harmonic
            base * 5.0f,     // 4th-6th Harmonic
            base * 10.0f,    // Mid Buzz
            1200.0f,         // High Buzz
            3200.0f          // EMI Hash
        };

        float qVals[NUM_HUM_BANDS] = {
            2.5f,   // Wide sub-filter
            18.0f,  // Very narrow fundamental
            16.0f,  // Narrow 2nd harmonic
            14.0f,  // Narrow 3rd harmonic
            8.0f,   // Mid cluster
            6.0f,   // Upper buzz
            4.0f,   // HF hash
            3.0f    // Top EMI
        };

        for (int i = 0; i < NUM_HUM_BANDS; ++i) {
            mFiltersL[i].setupBandpass(freqs[i], sRate, qVals[i]);
            mFiltersR[i].setupBandpass(freqs[i], sRate, qVals[i]);
        }
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t numSamples) {
        if (!outL || numSamples == 0) return;

        const float sRate = static_cast<float>(mSampleRate);
        const bool isStereo = (inR && outR && inR != inL);

        // Envelope coefficients (matched to VST denoiser)
        const float levelAtt = 1.0f - std::exp(-1.0f / (2.0f * sRate / 1000.0f));   // 2ms attack
        const float levelRel = 1.0f - std::exp(-1.0f / (60.0f * sRate / 1000.0f));  // 60ms decay
        const float gainAtt  = 1.0f - std::exp(-1.0f / (0.1f * sRate / 1000.0f));   // 0.1ms gain attack

        // Learning: capture peak hum levels over 1.2 seconds
        const int totalLearnSamples = static_cast<int>(mSampleRate * 1.2);
        if (mLearning) {
            for (uint32_t s = 0; s < numSamples; ++s) {
                float xL = inL ? inL[s] : 0.0f;
                float xR = isStereo ? inR[s] : xL;
                float xMono = 0.5f * (xL + xR);

                for (int b = 0; b < NUM_HUM_BANDS; ++b) {
                    float bandSignal = mFiltersL[b].process(xMono);
                    float absBand = std::abs(bandSignal);
                    if (absBand > mLearnedNoiseFloor[b]) {
                        mLearnedNoiseFloor[b] = absBand;
                    }
                }
            }
            mLearnFrames += numSamples;
            if (mLearnFrames >= totalLearnSamples) {
                mLearning = false;
                for (int b = 0; b < NUM_HUM_BANDS; ++b) {
                    float peak = mLearnedNoiseFloor[b];
                    if (peak < 0.0001f) peak = 0.0001f;
                    // Set threshold at peak + 3dB headroom (same as VST learn)
                    mManualThresholds[b] = std::clamp(
                        20.0f * std::log10(peak) + 3.0f, -100.0f, 0.0f);
                }
            }
            // Pass audio through during learning (don't mute)
            if (outL && inL) std::memcpy(outL, inL, numSamples * sizeof(float));
            if (outR && inR && isStereo) std::memcpy(outR, inR, numSamples * sizeof(float));
            else if (outR && outL) std::memcpy(outR, outL, numSamples * sizeof(float));
            return;
        }

        // Precompute per-band linear thresholds
        float tLinear[NUM_HUM_BANDS];
        for (int b = 0; b < NUM_HUM_BANDS; ++b) {
            tLinear[b] = dbToGain(mManualThresholds[b]);
        }

        for (uint32_t s = 0; s < numSamples; ++s) {
            float xL = inL ? inL[s] : 0.0f;
            float xR = isStereo ? inR[s] : xL;

            // 1. DC Power Supply Offset Blocker (< 10 Hz)
            float dcCleanL = xL;
            float dcCleanR = xR;
            if (mLowCut) {
                dcCleanL = xL - mDcX1_L + 0.9985f * mDcY1_L;
                mDcX1_L = xL;
                mDcY1_L = dcCleanL;

                dcCleanR = xR - mDcX1_R + 0.9985f * mDcY1_R;
                mDcX1_R = xR;
                mDcY1_R = dcCleanR;
            }

            float totalCancelledHumL = 0.0f;
            float totalCancelledHumR = 0.0f;

            // 2. Per-band spectral subtraction (same formula as VST denoiser)
            for (int b = 0; b < NUM_HUM_BANDS; ++b) {
                float bandSigL = mFiltersL[b].process(dcCleanL);
                float bandSigR = isStereo ? mFiltersR[b].process(dcCleanR) : bandSigL;

                float absBandL = std::abs(bandSigL);
                float absBandR = std::abs(bandSigR);

                // Envelope followers
                float coeffL = (absBandL > mBandEnvelopesL[b]) ? levelAtt : levelRel;
                mBandEnvelopesL[b] += (absBandL - mBandEnvelopesL[b]) * coeffL;

                float coeffR = (absBandR > mBandEnvelopesR[b]) ? levelAtt : levelRel;
                mBandEnvelopesR[b] += (absBandR - mBandEnvelopesR[b]) * coeffR;

                mOutputLevels[b] = 0.5f * (mBandEnvelopesL[b] + mBandEnvelopesR[b]);

                // Spectral subtraction gain: gain = 1 - noise/signal
                // This is the same proven formula from the Cyber Denoiser VST.
                float targetGainL = 1.0f - (tLinear[b] / (mBandEnvelopesL[b] + 1e-9f));
                targetGainL = std::max(0.0f, std::min(1.0f, targetGainL));

                float targetGainR = 1.0f - (tLinear[b] / (mBandEnvelopesR[b] + 1e-9f));
                targetGainR = std::max(0.0f, std::min(1.0f, targetGainR));

                // Scale by reduction amount
                float scaledTargetL = 1.0f - mReduction * (1.0f - targetGainL);
                float scaledTargetR = 1.0f - mReduction * (1.0f - targetGainR);

                // Adaptive release with tail stabilization
                float gainCoeffL, gainCoeffR;
                if (scaledTargetL > mCurrentGainsL[b]) {
                    gainCoeffL = gainAtt;
                } else {
                    float tailDist = mCurrentGainsL[b] / 1.01f;
                    float tailStab = 1.0f + (1.0f - std::min(1.0f, tailDist)) * 8.0f;
                    float adaptRel = 10.0f * tailStab; // 10-90ms
                    gainCoeffL = 1.0f - std::exp(-1.0f / (adaptRel * sRate / 1000.0f));
                }
                mCurrentGainsL[b] += (scaledTargetL - mCurrentGainsL[b]) * gainCoeffL;

                if (scaledTargetR > mCurrentGainsR[b]) {
                    gainCoeffR = gainAtt;
                } else {
                    float tailDist = mCurrentGainsR[b] / 1.01f;
                    float tailStab = 1.0f + (1.0f - std::min(1.0f, tailDist)) * 8.0f;
                    float adaptRel = 10.0f * tailStab;
                    gainCoeffR = 1.0f - std::exp(-1.0f / (adaptRel * sRate / 1000.0f));
                }
                mCurrentGainsR[b] += (scaledTargetR - mCurrentGainsR[b]) * gainCoeffR;

                // The hum we cancel = bandpass signal * (1 - gain)
                totalCancelledHumL += bandSigL * (1.0f - mCurrentGainsL[b]);
                totalCancelledHumR += bandSigR * (1.0f - mCurrentGainsR[b]);
            }

            // 3. Subtract cancelled hum from full signal
            float cleanL = dcCleanL - totalCancelledHumL;
            float cleanR = dcCleanR - totalCancelledHumR;

            // 4. Listen mode: hear what's being removed
            if (mListenHumOnly) {
                outL[s] = totalCancelledHumL * 3.0f;
                if (outR) outR[s] = totalCancelledHumR * 3.0f;
            } else {
                outL[s] = cleanL;
                if (outR) outR[s] = cleanR;
            }
        }
    }

private:
    double mSampleRate = 48000.0;
    std::vector<HarmonicBandFilter> mFiltersL;
    std::vector<HarmonicBandFilter> mFiltersR;

    int   mGridMode = 0;
    float mReduction = 1.0f;
    float mSmooth = 1.0f;
    bool  mListenHumOnly = false;
    bool  mLowCut = true;
    bool  mLearning = false;
    int   mLearnFrames = 0;

    std::vector<float> mManualThresholds;
    std::vector<float> mLearnedNoiseFloor;
    std::vector<float> mBandEnvelopesL;
    std::vector<float> mBandEnvelopesR;
    std::vector<float> mCurrentGainsL;
    std::vector<float> mCurrentGainsR;
    std::vector<float> mOutputLevels;

    float mDcX1_L = 0.0f, mDcY1_L = 0.0f;
    float mDcX1_R = 0.0f, mDcY1_R = 0.0f;
};

} // namespace AudioDSP

#endif // CYBER_HUM_ENGINE_HPP
