#ifndef STROBE_TUNER_CORE_HPP
#define STROBE_TUNER_CORE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

namespace HeadRushDSP {

struct TuningProfile {
    const char* name;
    float offsets[12]; // Cent offsets for [C, C#, D, D#, E, F, F#, G, G#, A, A#, B]
};

class StrobeTunerCore {
public:
    StrobeTunerCore() = default;
    ~StrobeTunerCore() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        
        mHistorySize = 4096;
        mHistoryBuffer.assign(mHistorySize, 0.0f);
        mDecimatedBuffer.assign(mHistorySize / 2, 0.0f);
        
        // 35Hz floor on decimated stream
        mDecSampleRate = mSampleRate * 0.5;
        mDecMinLag = static_cast<int>(mDecSampleRate / 1300.0);
        mDecMaxLag = static_cast<int>(mDecSampleRate / 35.0);
        if (mDecMaxLag > 750) mDecMaxLag = 750;

        mCorrs.assign(mDecMaxLag - mDecMinLag + 16, 0.0f);

        mInputGainMultiplier = 1.0f;
        mAutoGain = 1.0f;
        mDcY1 = 0.0f;
        mDcX1 = 0.0f;
        mDetectedFrequency = 0.0f;
        mCentsDeviation = 0.0f;
        mSmoothedCents = 0.0f;
        mDetectedNoteIndex = -1;
        mInTune = false;
        mSignalLevel = 0.0f;
        mSilenceFrames = 0;
        mSampleAccumulator = 0;
        
        // Target ~30Hz analysis refresh rate (e.g. 1536 samples @ 48kHz = 32ms)
        mAnalysisInterval = static_cast<int>(mSampleRate / 30.0);
        if (mAnalysisInterval < 512) mAnalysisInterval = 512;

        initProfiles();
    }

    void reset() {
        std::fill(mHistoryBuffer.begin(), mHistoryBuffer.end(), 0.0f);
        std::fill(mDecimatedBuffer.begin(), mDecimatedBuffer.end(), 0.0f);
        mAutoGain = 1.0f;
        mDcY1 = 0.0f;
        mDcX1 = 0.0f;
        mDetectedFrequency = 0.0f;
        mCentsDeviation = 0.0f;
        mSmoothedCents = 0.0f;
        mDetectedNoteIndex = -1;
        mInTune = false;
        mSignalLevel = 0.0f;
        mSilenceFrames = 0;
        mSampleAccumulator = 0;
    }

    void setInputGain(float gainMultiplier) {
        mInputGainMultiplier = std::clamp(gainMultiplier, 0.1f, 10.0f);
    }

    float getInputGain() const {
        return mInputGainMultiplier;
    }

    void pushSamples(const float* inputSamples, int numSamples) {
        if (!inputSamples || numSamples <= 0) return;

        // 1. Shift history buffer and append incoming samples scaled by input gain multiplier
        int shift = std::min(numSamples, (int)mHistorySize);
        std::memmove(mHistoryBuffer.data(), mHistoryBuffer.data() + shift, (mHistorySize - shift) * sizeof(float));
        
        int copyOffset = mHistorySize - shift;
        int srcOffset = numSamples - shift;

        float blockPeak = 0.0f;
        for (int i = 0; i < shift; ++i) {
            float s = inputSamples[srcOffset + i] * mInputGainMultiplier;
            mHistoryBuffer[copyOffset + i] = s;
            float absVal = std::abs(s);
            if (absVal > blockPeak) blockPeak = absVal;
        }

        mSignalLevel = mSignalLevel * 0.7f + std::min(1.0f, blockPeak * 3.5f) * 0.3f;

        // 2. Rate-limited Pitch Analysis (~30 Hz update rate to minimize CPU)
        mSampleAccumulator += numSamples;
        if (mSampleAccumulator >= mAnalysisInterval) {
            mSampleAccumulator = 0;
            analyzeOptimized();
        }
    }

    void updateTuningCalculations(float refA, int profileIdx, int capo, float stability = 0.99f) {
        if (mDetectedFrequency <= 20.0f) {
            mSilenceFrames++;
            if (mSilenceFrames > 10) { // Clear display smoothly when silent
                mCentsDeviation = 0.0f;
                mSmoothedCents = 0.0f;
                mDetectedNoteIndex = -1;
                mInTune = false;
            }
            return;
        }

        mSilenceFrames = 0;
        float refPitch = (refA >= 400.0f && refA <= 480.0f) ? refA : 440.0f;
        float midi = 12.0f * std::log2(mDetectedFrequency / refPitch) + 69.0f - static_cast<float>(capo);
        int roundedNote = static_cast<int>(std::round(midi));
        
        int noteIndex = (roundedNote % 12 + 12) % 12;

        float profileOffset = 0.0f;
        if (profileIdx >= 0 && profileIdx < static_cast<int>(mProfiles.size())) {
            profileOffset = mProfiles[profileIdx].offsets[noteIndex];
        }

        float rawCents = (midi - static_cast<float>(roundedNote)) * 100.0f;
        float targetCents = rawCents - profileOffset;

        // Stage Stability Filter: Default 0.99 for rock-solid stage stability
        float alpha = std::clamp(stability, 0.0f, 0.995f);
        if (mDetectedNoteIndex == noteIndex) {
            mSmoothedCents = alpha * mSmoothedCents + (1.0f - alpha) * targetCents;
        } else {
            mSmoothedCents = targetCents; // Instant response on string change
        }

        mDetectedNoteIndex = noteIndex;
        mCentsDeviation = mSmoothedCents;
        mInTune = (std::abs(mCentsDeviation) < 1.0f);
    }

    float getDetectedFrequency() const { return mDetectedFrequency; }
    float getCentsDeviation() const { return mCentsDeviation; }
    int getDetectedNoteIndex() const { return mDetectedNoteIndex; }
    bool isInTune() const { return mInTune; }
    float getSignalLevel() const { return mSignalLevel; }

private:
    void initProfiles() {
        mProfiles.clear();
        mProfiles.push_back({ "Equal Temperament", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} });
        mProfiles.push_back({ "James Taylor", {0, 0, -8.0f, 0, -12.0f, 0, 0, -4.0f, 0, -10.0f, 0, -6.0f} });
        mProfiles.push_back({ "Buzz Feiten", {0, 0, -2.0f, 0, -2.0f, 0, 0, -2.0f, 0, -2.0f, 0, 1.0f} });
        mProfiles.push_back({ "Peterson GTR", {0, 0, -0.4f, 0, -2.3f, 0, 0, 0.0f, 0, -2.1f, 0, -0.6f} });
        mProfiles.push_back({ "Open D", {0, 0, -2.0f, 0, 0, 0, -4.0f, 0, 0, -1.5f, 0, 0} });
        mProfiles.push_back({ "Open G", {0, 0, -1.5f, 0, 0, 0, 0, -2.0f, 0, 0, 0, -4.0f} });
    }

    void analyzeOptimized() {
        if (mSignalLevel < 0.005f) {
            mDetectedFrequency = 0.0f;
            return;
        }

        const int rawLength = 3072;
        int rawStart = mHistorySize - rawLength;
        const float* raw = mHistoryBuffer.data() + rawStart;

        float peak = 0.0f;
        const float R = 0.995f; // DC Blocker

        // 1. DC Blocking, Peak Detection & 2x Decimation (Downsampling to 24 kHz)
        int decLength = rawLength / 2; // 1536 samples
        for (int i = 0; i < decLength; ++i) {
            float x1 = raw[2 * i];
            float y1 = x1 - mDcX1 + R * mDcY1;
            mDcX1 = x1;
            mDcY1 = y1;
            if (std::abs(y1) > peak) peak = std::abs(y1);

            float x2 = raw[2 * i + 1];
            float y2 = x2 - mDcX1 + R * mDcY1;
            mDcX1 = x2;
            mDcY1 = y2;
            if (std::abs(y2) > peak) peak = std::abs(y2);

            mDecimatedBuffer[i] = 0.5f * (y1 + y2);
        }

        if (peak < 0.00005f) {
            mDetectedFrequency = 0.0f;
            return;
        }

        // 2. Intelligent Auto-Gain Staging (target 0.6 amplitude)
        float targetGain = 0.6f / peak;
        mAutoGain = mAutoGain * 0.7f + targetGain * 0.3f;
        mAutoGain = std::clamp(mAutoGain, 0.1f, 5000.0f);

        // 3. Center Clipping
        float clipThreshold = (peak * mAutoGain) * 0.20f;
        for (int i = 0; i < decLength; ++i) {
            float val = mDecimatedBuffer[i] * mAutoGain;
            if (val > clipThreshold) mDecimatedBuffer[i] = val - clipThreshold;
            else if (val < -clipThreshold) mDecimatedBuffer[i] = val + clipThreshold;
            else mDecimatedBuffer[i] = 0.0f;
        }

        // 4. Ultra-Fast Coarse-to-Fine Normalized Autocorrelation
        const int corrWindow = 512;
        const int minLag = mDecMinLag;
        const int maxLag = std::min(mDecMaxLag, decLength - corrWindow - 2);

        if (maxLag <= minLag) {
            mDetectedFrequency = 0.0f;
            return;
        }

        float e0 = 0.0f;
        const float* p1 = mDecimatedBuffer.data();
        for (int j = 0; j < corrWindow; ++j) {
            e0 += p1[j] * p1[j];
        }

        if (e0 <= 1e-12f) {
            mDetectedFrequency = 0.0f;
            return;
        }

        // Phase 1: Stride-2 Coarse Search
        float globalMax = -1.0f;
        int bestCoarseLag = minLag;

        for (int lag = minLag; lag < maxLag; lag += 2) {
            float dot = 0.0f;
            float eLag = 0.0f;
            const float* p2 = p1 + lag;

            for (int j = 0; j < corrWindow; j += 2) {
                dot += p1[j] * p2[j] + p1[j+1] * p2[j+1];
                eLag += p2[j] * p2[j] + p2[j+1] * p2[j+1];
            }

            float normCorr = dot / std::sqrt(e0 * eLag + 1e-12f);
            int idx = lag - minLag;
            mCorrs[idx] = normCorr;

            if (normCorr > globalMax) {
                globalMax = normCorr;
            }
        }

        if (globalMax < 0.20f) {
            mDetectedFrequency = 0.0f;
            return;
        }

        // First peak picking in coarse space
        for (int lag = minLag + 2; lag < maxLag - 2; lag += 2) {
            int idx = lag - minLag;
            if (mCorrs[idx] > mCorrs[idx - 2] && mCorrs[idx] >= mCorrs[idx + 2]) {
                if (mCorrs[idx] >= 0.82f * globalMax) {
                    bestCoarseLag = lag;
                    break;
                }
            }
        }

        // Phase 2: Fine 3-point search around best lag
        int fineLags[3] = { bestCoarseLag - 1, bestCoarseLag, bestCoarseLag + 1 };
        float fineCorrs[3] = { 0.0f, 0.0f, 0.0f };

        for (int k = 0; k < 3; ++k) {
            int lag = fineLags[k];
            if (lag < minLag || lag >= maxLag) continue;
            float dot = 0.0f;
            float eLag = 0.0f;
            const float* p2 = p1 + lag;

            for (int j = 0; j < corrWindow; ++j) {
                dot += p1[j] * p2[j];
                eLag += p2[j] * p2[j];
            }
            fineCorrs[k] = dot / std::sqrt(e0 * eLag + 1e-12f);
        }

        // 5. Parabolic Interpolation for Sub-Sample Pitch Precision
        float alpha = fineCorrs[0];
        float beta  = fineCorrs[1];
        float gamma = fineCorrs[2];
        float denom = alpha - 2.0f * beta + gamma;
        float delta = (std::abs(denom) > 1e-9f) ? 0.5f * (alpha - gamma) / denom : 0.0f;
        float refinedDecLag = static_cast<float>(bestCoarseLag) + delta;

        if (refinedDecLag > 1.0f) {
            mDetectedFrequency = static_cast<float>(mDecSampleRate / refinedDecLag);
        } else {
            mDetectedFrequency = 0.0f;
        }
    }

    double mSampleRate = 48000.0;
    double mDecSampleRate = 24000.0;
    size_t mHistorySize = 4096;
    int mDecMinLag = 18;
    int mDecMaxLag = 685;
    int mAnalysisInterval = 1536;
    int mSampleAccumulator = 0;

    std::vector<float> mHistoryBuffer;
    std::vector<float> mDecimatedBuffer;
    std::vector<float> mCorrs;

    float mInputGainMultiplier = 1.0f;
    float mAutoGain = 1.0f;
    float mDcY1 = 0.0f;
    float mDcX1 = 0.0f;
    float mDetectedFrequency = 0.0f;
    float mCentsDeviation = 0.0f;
    float mSmoothedCents = 0.0f;
    int mDetectedNoteIndex = -1;
    bool mInTune = false;
    float mSignalLevel = 0.0f;
    int mSilenceFrames = 0;

    std::vector<TuningProfile> mProfiles;
};

} // namespace HeadRushDSP

#endif // STROBE_TUNER_CORE_HPP
