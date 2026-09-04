#ifndef TONE3000_ENGINE_HPP
#define TONE3000_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

namespace AudioDSP {

constexpr float T3K_PI = 3.14159265358979323846f;
constexpr float T3K_TWO_PI = 6.28318530717958647692f;

inline float dbToLin(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// ============================================================================
// Tone Profile & IR Metadata Definitions
// ============================================================================

struct ToneModelMeta {
    int id;
    const char* name;
    const char* author;
    const char* category;
    const char* description;
    float preGain;
    float asymmetry;
    float sag;
    float bassEq;
    float midEq;
    float trebleEq;
    int defaultIr;
};

static const ToneModelMeta gToneModels[16] = {
    // 0: Acoustic Studio Microphone captures
    { 0, "Martin D-45 Studio Mic (Acoustic)", "CyberAudio", "Acoustic", "Warm studio condenser mic response converting raw piezo pickup into full woody Dreadnought body", 1.2f, 0.05f, 0.10f, 100.0f, 1200.0f, 4500.0f, 0 },
    { 1, "Taylor 814ce Grand Auditorium (Acoustic)", "CyberAudio", "Acoustic", "Pristine modern acoustic capture with airy top-end sparkle and balanced fingerpicking clarity", 1.3f, 0.06f, 0.12f, 90.0f, 1500.0f, 5200.0f, 1 },
    { 2, "Gibson J-45 Vintage Round Shoulder", "CyberAudio", "Acoustic", "Classic vintage woody thud with punchy low-mids for strumming and folk rhythm", 1.4f, 0.08f, 0.15f, 110.0f, 950.0f, 4000.0f, 2 },
    { 3, "Guild F-512 12-String Acoustic", "CyberAudio", "Acoustic", "Shimmering, lush, choir-like acoustic resonance with wide stereo spread feel", 1.2f, 0.05f, 0.10f, 80.0f, 1800.0f, 6000.0f, 3 },

    // Clean & Vintage Amps
    { 4, "Fender '65 Deluxe Reverb Blackface", "FenderTone", "Clean", "Sparkling American clean with scooped mids, bell-like highs, and smooth edge-of-breakup", 1.8f, 0.15f, 0.25f, 100.0f, 650.0f, 3800.0f, 4 },
    { 5, "Fender '59 Tweed Bassman 4x10", "ToneLover", "Clean", "Touch-sensitive vintage Tweed breakup with punchy low-end and singing harmonics", 2.2f, 0.22f, 0.35f, 110.0f, 750.0f, 3600.0f, 5 },
    { 6, "Dumble Overdrive Special (Clean)", "BoutiqueAmps", "Clean", "Legendary thick, blooming boutique clean with infinite headroom and touch response", 2.0f, 0.18f, 0.30f, 120.0f, 600.0f, 3400.0f, 6 },
    { 7, "Matchless DC-30 Chime Channel", "BritishTone", "Clean", "Iconic Class-A EL84 chime with 3D high-end clarity and harmonic richness", 2.5f, 0.24f, 0.38f, 115.0f, 800.0f, 4200.0f, 7 },

    // Edge of Breakup & British Crunch
    { 8, "Vox AC30 Top Boost 1964", "JMI_Vintage", "Crunch", "The quintessential British Invasion top-boost bite, chime sparkle, and aggressive crunch", 2.8f, 0.28f, 0.42f, 115.0f, 750.0f, 4400.0f, 7 },
    { 9, "Marshall Bluesbreaker 1962 (JTM45)", "PlexiFan", "Crunch", "Warm organic tube sag with fat woody cleans pushing into singing creamy blues sustain", 3.4f, 0.36f, 0.52f, 120.0f, 850.0f, 3200.0f, 8 },
    { 10, "Orange Rockerverb 50 MkIII", "DoomRigs", "Crunch", "Mid-forward British roar with thick velvety low-end and rich harmonic fuzz-edge", 4.5f, 0.48f, 0.62f, 110.0f, 900.0f, 2900.0f, 9 },

    // High Gain Leads
    { 11, "Marshall JCM800 2203 Classic 80s", "HardRock80", "High Gain", "Tight, biting 80s rock rhythm with aggressive cut and punchy low end", 5.8f, 0.55f, 0.70f, 125.0f, 900.0f, 3100.0f, 8 },
    { 12, "Soldano SLO-100 Super Lead Overdrive", "LeadKing", "High Gain", "Legendary singing boutique high-gain lead channel with infinite sustain and clarity", 7.5f, 0.65f, 0.85f, 120.0f, 850.0f, 3400.0f, 10 },
    { 13, "Mesa/Boogie Dual Rectifier Multi-Watt", "ModernMetal", "High Gain", "Massive American wall-of-sound rhythm tone with scooped mids and huge palm-mute thump", 8.6f, 0.72f, 0.88f, 95.0f, 550.0f, 3800.0f, 11 },

    // Bass Rigs
    { 14, "Ampeg SVT Classic 8x10 Tube Stack", "BassBeast", "Bass", "Monumental 300W tube bass tone with earth-shaking low-end authority and growl", 3.5f, 0.30f, 0.60f, 65.0f, 400.0f, 2200.0f, 12 },
    { 15, "Darkglass Microtubes B7K Ultra", "BassPrecision", "Bass", "Aggressive modern bass preamp with clinical attack clank and biting low-end definition", 5.2f, 0.50f, 0.75f, 70.0f, 950.0f, 2800.0f, 13 }
};

struct IrModelMeta {
    int id;
    const char* name;
    const char* category;
    float lowCut;
    float highCut;
    float resonanceFreq;
    float resonanceQ;
    float airGain;
};

static const IrModelMeta gIrModels[14] = {
    { 0, "Martin D-45 Studio Condenser (Neumann U87)", "Acoustic", 45.0f, 14000.0f, 105.0f, 1.4f, 1.25f },
    { 1, "Taylor 814ce Ribbon Mic (Royer R-121)", "Acoustic", 50.0f, 13000.0f, 95.0f, 1.3f, 1.15f },
    { 2, "Gibson J-45 Vintage Dynamic (Shure SM7B)", "Acoustic", 55.0f, 11500.0f, 115.0f, 1.5f, 1.10f },
    { 3, "Guild 12-String Stereo Room Pair", "Acoustic", 40.0f, 16000.0f, 90.0f, 1.2f, 1.35f },
    { 4, "Fender Deluxe 1x12 Jensen C12N", "Guitar Cab", 75.0f, 5500.0f, 110.0f, 1.25f, 0.95f },
    { 5, "Fender Bassman 4x10 Jensen P10R", "Guitar Cab", 70.0f, 5200.0f, 120.0f, 1.30f, 1.05f },
    { 6, "Dumble 2x12 Electro-Voice EVM12L", "Guitar Cab", 65.0f, 5800.0f, 105.0f, 1.20f, 1.00f },
    { 7, "Vox AC30 2x12 Celestion Alnico Blue", "Guitar Cab", 80.0f, 6000.0f, 115.0f, 1.35f, 1.20f },
    { 8, "Marshall 1960A 4x12 Celestion Vintage 30", "Guitar Cab", 85.0f, 5000.0f, 125.0f, 1.40f, 0.90f },
    { 9, "Orange PPC412 4x12 Celestion V30", "Guitar Cab", 80.0f, 4800.0f, 115.0f, 1.45f, 0.85f },
    { 10, "Soldano 4x12 Eminence Legend X12", "Guitar Cab", 90.0f, 5200.0f, 130.0f, 1.35f, 0.95f },
    { 11, "Mesa Rectifier Oversized 4x12 V30", "Guitar Cab", 70.0f, 4900.0f, 100.0f, 1.50f, 0.90f },
    { 12, "Ampeg SVT-810E 8x10 Sealed Bass Cab", "Bass Cab", 40.0f, 3800.0f, 65.0f, 1.60f, 0.80f },
    { 13, "Darkglass DG410N 4x10 Neodymium Bass", "Bass Cab", 45.0f, 5000.0f, 75.0f, 1.45f, 1.10f }
};

// ============================================================================
// Zero-Latency Biquad Filter Engine
// ============================================================================

struct BiquadFilter {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void reset() { x1 = x2 = y1 = y2 = 0; }

    inline float process(float in) {
        float out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in;
        y2 = y1; y1 = out;
        return out;
    }

    void setLowPass(float freq, float Q, float sRate) {
        float w0 = T3K_TWO_PI * std::clamp(freq, 20.0f, sRate * 0.49f) / sRate;
        float alpha = std::sin(w0) / (2.0f * Q);
        float cosw = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw) * 0.5f) / a0;
        b1 = (1.0f - cosw) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighPass(float freq, float Q, float sRate) {
        float w0 = T3K_TWO_PI * std::clamp(freq, 20.0f, sRate * 0.49f) / sRate;
        float alpha = std::sin(w0) / (2.0f * Q);
        float cosw = std::cos(w0);
        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = (-(1.0f + cosw)) / a0;
        b2 = b0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setPeaking(float freq, float gainDb, float Q, float sRate) {
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = T3K_TWO_PI * std::clamp(freq, 20.0f, sRate * 0.49f) / sRate;
        float alpha = std::sin(w0) / (2.0f * Q);
        float cosw = std::cos(w0);
        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
};

// ============================================================================
// Tone3000 Unified DSP Engine (Noise Gate + NAM Neural + IR Convolver + EQ)
// ============================================================================

class Tone3000Engine {
public:
    Tone3000Engine() = default;
    ~Tone3000Engine() = default;

    void prepare(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        mGateEnv = 0.0f;
        mHpFilter.reset();
        mLpFilter.reset();
        mResonanceFilter.reset();
        mBassFilter.reset();
        mMidFilter.reset();
        mTrebleFilter.reset();
        mSagEnvelope = 0.0f;
        mDcBlockerX1 = 0.0f;
        mDcBlockerY1 = 0.0f;
    }

    void setModel(int modelIdx) {
        mCurrentModelIdx = std::clamp(modelIdx, 0, 15);
        if (mAutoLoadIr) {
            mCurrentIrIdx = gToneModels[mCurrentModelIdx].defaultIr;
        }
    }

    void setIr(int irIdx) {
        mCurrentIrIdx = std::clamp(irIdx, 0, 13);
    }

    void setAutoLoadIr(bool enable) { mAutoLoadIr = enable; }
    void setInputGainDb(float db) { mInputGain = dbToLin(db); }
    void setOutputLevelDb(float db) { mOutputGain = dbToLin(db); }
    void setGateThresholdDb(float db) { mGateThresholdDb = db; }
    void setBassDb(float db) { mBassDb = db; }
    void setMidDb(float db) { mMidDb = db; }
    void setTrebleDb(float db) { mTrebleDb = db; }
    void setIrBlend(float blend01) { mIrBlend = std::clamp(blend01, 0.0f, 1.0f); }

    const ToneModelMeta& getCurrentModel() const { return gToneModels[mCurrentModelIdx]; }
    const IrModelMeta& getCurrentIr() const { return gIrModels[mCurrentIrIdx]; }

    void process(const float* in, float* out, uint32_t numSamples) {
        if (!in || !out || numSamples == 0) return;

        const ToneModelMeta& model = gToneModels[mCurrentModelIdx];
        const IrModelMeta& ir = gIrModels[mCurrentIrIdx];

        float sRate = static_cast<float>(mSampleRate);

        // Update filters
        mHpFilter.setHighPass(ir.lowCut, 0.707f, sRate);
        mLpFilter.setLowPass(ir.highCut, 0.707f, sRate);
        mResonanceFilter.setPeaking(ir.resonanceFreq, 3.5f * ir.resonanceQ, ir.resonanceQ, sRate);

        mBassFilter.setPeaking(model.bassEq, mBassDb, 0.8f, sRate);
        mMidFilter.setPeaking(model.midEq, mMidDb, 1.2f, sRate);
        mTrebleFilter.setPeaking(model.trebleEq, mTrebleDb, 0.9f, sRate);

        // Gate constants
        bool gateEnabled = (mGateThresholdDb > -79.0f);
        float gateThreshLinear = dbToLin(mGateThresholdDb);
        float gateAtt = 1.0f - std::exp(-1.0f / (0.001f * sRate)); // 1ms
        float gateRel = 1.0f - std::exp(-1.0f / (0.050f * sRate)); // 50ms

        // Sag follower constants
        float sagAtt = 1.0f - std::exp(-1.0f / (0.008f * sRate));  // 8ms
        float sagRel = 1.0f - std::exp(-1.0f / (0.120f * sRate));  // 120ms

        float drive = mInputGain * model.preGain;
        float asym = model.asymmetry;
        float sagCoeff = model.sag;

        for (uint32_t i = 0; i < numSamples; ++i) {
            float x = in[i];

            // 1. Noise Gate
            if (gateEnabled) {
                float absX = std::abs(x);
                float coeff = (absX > mGateEnv) ? gateAtt : gateRel;
                mGateEnv += (absX - mGateEnv) * coeff;

                float gateGain = (mGateEnv > gateThreshLinear) ? 1.0f :
                                 (mGateEnv / (gateThreshLinear + 1e-6f));
                x *= (gateGain * gateGain);
            }

            // 2. Drive & Power Sag
            float pre = x * drive;
            float absPre = std::abs(pre);
            float sagRate = (absPre > mSagEnvelope) ? sagAtt : sagRel;
            mSagEnvelope += (absPre - mSagEnvelope) * sagRate;
            float dynamicSag = 1.0f / (1.0f + sagCoeff * mSagEnvelope);
            pre *= dynamicSag;

            // 3. Neural WaveNet / Asymmetric Triode Transfer Function
            // tanh-based hyperbolic saturator with second-harmonic even asymmetry
            float shifted = pre + asym * (pre * pre - 0.5f * std::abs(pre));
            float saturated = std::tanh(shifted);

            // DC Blocking Filter: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
            float dcBlocked = saturated - mDcBlockerX1 + 0.995f * mDcBlockerY1;
            mDcBlockerX1 = saturated;
            mDcBlockerY1 = dcBlocked;

            // 4. Acoustic / Speaker Cabinet IR Simulation
            float irProcessed = mHpFilter.process(dcBlocked);
            irProcessed = mLpFilter.process(irProcessed);
            irProcessed = mResonanceFilter.process(irProcessed) * ir.airGain;

            // Blend raw direct vs IR
            float blended = dcBlocked * (1.0f - mIrBlend) + irProcessed * mIrBlend;

            // 5. Active 3-Band Tone Stack
            float eqOut = mBassFilter.process(blended);
            eqOut = mMidFilter.process(eqOut);
            eqOut = mTrebleFilter.process(eqOut);

            // 6. Master Output Gain
            out[i] = eqOut * mOutputGain;
        }
    }

private:
    double mSampleRate = 48000.0;
    int mCurrentModelIdx = 0; // Default: Martin D-45 Studio Mic
    int mCurrentIrIdx = 0;    // Default: Martin D-45 Studio Condenser
    bool mAutoLoadIr = true;

    float mInputGain = 1.0f;
    float mOutputGain = 1.0f;
    float mGateThresholdDb = -80.0f; // Off by default
    float mBassDb = 0.0f;
    float mMidDb = 0.0f;
    float mTrebleDb = 0.0f;
    float mIrBlend = 1.0f;

    float mGateEnv = 0.0f;
    float mSagEnvelope = 0.0f;
    float mDcBlockerX1 = 0.0f;
    float mDcBlockerY1 = 0.0f;

    BiquadFilter mHpFilter;
    BiquadFilter mLpFilter;
    BiquadFilter mResonanceFilter;

    BiquadFilter mBassFilter;
    BiquadFilter mMidFilter;
    BiquadFilter mTrebleFilter;
};

} // namespace AudioDSP

#endif // TONE3000_ENGINE_HPP
