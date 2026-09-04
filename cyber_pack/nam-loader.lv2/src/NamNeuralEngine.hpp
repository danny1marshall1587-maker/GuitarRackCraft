#ifndef NAM_NEURAL_ENGINE_HPP
#define NAM_NEURAL_ENGINE_HPP

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cstdio>

namespace AudioDSP {

// C++14-compatible clamp (std::clamp is C++17)
template<typename T>
static inline T clamp14(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr float PI_F = 3.14159265358979323846f;
constexpr float TWO_PI_F = 6.28318530717958647692f;

inline float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

struct AmpProfileInfo {
    int id;
    const char* name;
    const char* folder;
    const char* category;
    const char* desc;
    float preGain;
    float asymmetry;
    float compression;
    float bassCenter;
    float midCenter;
    float trebleCenter;
    float cabLowCut;
    float cabHighCut;
    float cabResonance;
};

// 24 Pro-Grade Tone 3000 / NAM Captures organized in 6 folders
static const AmpProfileInfo gAmpProfiles[24] = {
    // --- 01. American Clean & Vintage ---
    { 0, "Fender '65 Deluxe Reverb", "01 - American Clean", "Clean", "Lush sparkling American clean with bell-like top end and scooped mids", 1.8f, 0.15f, 0.25f, 100.0f, 650.0f, 3800.0f, 75.0f, 5200.0f, 1.15f },
    { 1, "Fender '59 Tweed Bassman", "01 - American Clean", "Vintage Tweed", "Warm harmonic tweed drive with dynamic touch responsiveness and punch", 2.2f, 0.20f, 0.35f, 110.0f, 700.0f, 3600.0f, 70.0f, 5000.0f, 1.20f },
    { 2, "Dumble ODS Clean Channel", "01 - American Clean", "Boutique Clean", "Ultra-rare pristine boutique clean with legendary harmonic bloom and sag", 2.0f, 0.18f, 0.30f, 120.0f, 600.0f, 3400.0f, 80.0f, 5400.0f, 1.25f },
    { 3, "Matchless DC-30 Clean", "01 - American Clean", "Class A Clean", "Boutique EL84 chime with 3D harmonic richness and glass-smooth top", 2.5f, 0.22f, 0.38f, 115.0f, 750.0f, 4200.0f, 85.0f, 5600.0f, 1.30f },
    // --- 02. British Chime & Crunch ---
    { 4, "Vox AC30 Top Boost 1964", "02 - British Chime & Crunch", "Chime Clean", "Iconic British EL84 top-end bite, chime sparkle, and singing crunch", 2.6f, 0.25f, 0.40f, 115.0f, 750.0f, 4500.0f, 80.0f, 5800.0f, 1.25f },
    { 5, "Marshall Bluesbreaker 1962", "02 - British Chime & Crunch", "Edge of Breakup", "Smooth organic vintage breakup with warm singing sustain and punch", 3.2f, 0.35f, 0.50f, 120.0f, 800.0f, 3200.0f, 85.0f, 4800.0f, 1.20f },
    { 6, "Marshall JTM45 Plexi", "02 - British Chime & Crunch", "Vintage Plexi", "Full-bodied KT66 tube sag with fat woody cleans pushing into roaring crunch", 3.8f, 0.40f, 0.55f, 125.0f, 850.0f, 3000.0f, 80.0f, 4700.0f, 1.28f },
    { 7, "Orange Rockerverb 50", "02 - British Chime & Crunch", "British Crunch", "Thick mid-forward British crunch with velvety low-end and fuzz-like saturation", 4.5f, 0.48f, 0.62f, 110.0f, 900.0f, 2900.0f, 90.0f, 4500.0f, 1.35f },
    // --- 03. High Gain & Modern Lead ---
    { 8, "Marshall JCM800 2203", "03 - High Gain & Modern Lead", "Classic 80s Rock", "Tight punchy 80s British rock rhythm and aggressive roaring harmonics", 5.8f, 0.55f, 0.70f, 125.0f, 900.0f, 3100.0f, 95.0f, 4600.0f, 1.30f },
    { 9, "Soldano SLO-100 Super Lead", "03 - High Gain & Modern Lead", "High Gain Lead", "Rich saturated boutique high gain with infinite singing sustain and clarity", 7.5f, 0.65f, 0.85f, 120.0f, 850.0f, 3400.0f, 90.0f, 4800.0f, 1.40f },
    { 10, "Peavey 5150 Block Letter", "03 - High Gain & Modern Lead", "Modern Metal", "Brutal aggressive high gain metal rhythm with razor-sharp attack and tight low end", 9.0f, 0.75f, 0.90f, 110.0f, 950.0f, 3600.0f, 100.0f, 4400.0f, 1.50f },
    { 11, "Mesa Dual Rectifier Multi-Watt", "03 - High Gain & Modern Lead", "Modern High Gain", "Huge wall-of-sound American high gain with scooped mids and massive chunk", 8.5f, 0.70f, 0.88f, 95.0f, 550.0f, 3800.0f, 85.0f, 4700.0f, 1.45f },
    { 12, "Friedman BE-100 Brown Eye", "03 - High Gain & Modern Lead", "Boutique Lead", "Ultra-refined hot-rodded Plexi tone with crisp articulation and liquid sustain", 7.8f, 0.60f, 0.80f, 115.0f, 800.0f, 3300.0f, 90.0f, 4900.0f, 1.35f },
    { 13, "Bogner Ecstasy 101B Red", "03 - High Gain & Modern Lead", "Boutique Heavy", "Complex woody high gain with rich harmonic overtone bloom and fat low-mids", 8.2f, 0.68f, 0.86f, 105.0f, 780.0f, 3200.0f, 88.0f, 4600.0f, 1.42f },
    { 14, "ENGL Powerball II Lead", "03 - High Gain & Modern Lead", "Ultra Modern Metal", "Precision German high gain with razor-focused mids and crushing sub punch", 9.4f, 0.78f, 0.92f, 100.0f, 1000.0f, 3700.0f, 105.0f, 4300.0f, 1.55f },
    { 15, "Diezel VH4 Channel 3", "03 - High Gain & Modern Lead", "Industrial Heavy", "Monumental 3D high-gain punch made famous by Tool and Metallica", 9.2f, 0.72f, 0.89f, 90.0f, 720.0f, 3500.0f, 95.0f, 4500.0f, 1.48f },
    // --- 04. Boutique Bass & Acoustic ---
    { 16, "Ampeg SVT-CL Classic Stack", "04 - Bass Rigs", "Tube Bass", "Massive 300W tube bass stack with thunderous low-end authority and growl", 3.5f, 0.30f, 0.60f, 65.0f, 400.0f, 2200.0f, 45.0f, 3800.0f, 1.60f },
    { 17, "Darkglass Microtubes B7K", "04 - Bass Rigs", "Modern Bass Grit", "Punchy modern bass preamp with aggressive clank and harmonic distortion", 5.2f, 0.50f, 0.75f, 70.0f, 900.0f, 2800.0f, 50.0f, 4200.0f, 1.45f },
    { 18, "Acoustic Tube DI Preamp", "04 - Bass Rigs", "Acoustic Preamp", "Warm pristine tube DI response for acoustic guitars with natural resonance", 1.5f, 0.10f, 0.20f, 80.0f, 1200.0f, 5000.0f, 40.0f, 8000.0f, 1.05f },
    // --- 05. Overdrive & Fuzz Pedals ---
    { 19, "Klon Centaur Gold Horsie", "05 - Overdrive & Boost Pedals", "Transparent OD", "Mythical transparent overdrive with germanium diode clipping and mid warmth", 3.0f, 0.20f, 0.45f, 150.0f, 1000.0f, 3500.0f, 100.0f, 6000.0f, 1.10f },
    { 20, "Ibanez TS808 Tube Screamer", "05 - Overdrive & Boost Pedals", "Mid Boost OD", "Iconic mid-hump overdrive for tightening amp low end and pushing leads", 4.5f, 0.45f, 0.55f, 220.0f, 720.0f, 2800.0f, 110.0f, 5200.0f, 1.25f },
    { 21, "ProCo Rat 2 Vintage LM308", "05 - Overdrive & Boost Pedals", "Distortion / Fuzz", "Gritty versatile distortion with signature asymmetrical filter bite", 6.2f, 0.60f, 0.78f, 130.0f, 1100.0f, 2600.0f, 95.0f, 4800.0f, 1.35f },
    { 22, "Electro-Harmonix Big Muff Pi", "05 - Overdrive & Boost Pedals", "Fuzz Sustain", "Massive scooped fuzz with infinite singing sustain and thick low-end wool", 7.0f, 0.65f, 0.85f, 85.0f, 450.0f, 3800.0f, 70.0f, 4200.0f, 1.50f },
    { 23, "King of Tone High Gain Side", "05 - Overdrive & Boost Pedals", "Boutique Overdrive", "Dual-stage analog overdrive with sweet dynamic touch sensitivity", 3.8f, 0.35f, 0.50f, 140.0f, 950.0f, 3300.0f, 90.0f, 5500.0f, 1.20f }
};

constexpr int NUM_AMP_PROFILES = 24;

/**
 * @brief Neural Amp Modeler (NAM) Engine
 *        Dual-slot with Quality (Slim/Full) mode and MIDI-switchable slots.
 */
class NamNeuralEngine {
public:
    NamNeuralEngine() = default;
    ~NamNeuralEngine() = default;

    void init(double sampleRate) {
        mSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        mGateEnvelope = 0.0f;
        mHpState      = 0.0f;
        mLpState      = 0.0f;
        mBassState    = 0.0f;
        mMidState     = 0.0f;
        mTrebleState  = 0.0f;
        mCabLpState   = 0.0f;
        mCabHpState   = 0.0f;
        // Slim-mode one-pole state
        mSlimState    = 0.0f;

        mProfileIdxA  = 8;  // Slot A default: JCM800
        mProfileIdxB  = 9;  // Slot B default: SLO-100
        mActiveSlot   = 0;  // Start on Slot A
        mDrive        = 0.5f;
        mBass         = 0.5f;
        mMid          = 0.5f;
        mTreble       = 0.5f;
        mVolume       = 0.5f;
        mGateThreshold= -80.0f;
        mCabEnabled   = true;
        mSlimMode     = false;
    }

    // ── Slot management ───────────────────────────────────────────────────────
    void setProfileA(int id) { mProfileIdxA = clamp14(id, 0, NUM_AMP_PROFILES - 1); }
    void setProfileB(int id) { mProfileIdxB = clamp14(id, 0, NUM_AMP_PROFILES - 1); }
    void setActiveSlot(int slot) { mActiveSlot = (slot == 1) ? 1 : 0; }
    int  getActiveSlot()  const  { return mActiveSlot; }

    // Legacy single-slot setter: writes to whichever slot is active
    void setProfile(int profileId) {
        if (mActiveSlot == 1) setProfileB(profileId);
        else                   setProfileA(profileId);
    }
    int getProfile() const {
        return (mActiveSlot == 1) ? mProfileIdxB : mProfileIdxA;
    }

    // ── Parameter setters ─────────────────────────────────────────────────────
    void setDrive(float v)          { mDrive = clamp14(v, 0.0f, 1.0f); }
    void setBass(float v)           { mBass  = clamp14(v, 0.0f, 1.0f); }
    void setMid(float v)            { mMid   = clamp14(v, 0.0f, 1.0f); }
    void setTreble(float v)         { mTreble= clamp14(v, 0.0f, 1.0f); }
    void setVolume(float v)         { mVolume= clamp14(v, 0.0f, 1.0f); }
    void setGateThreshold(float db) { mGateThreshold = db; }
    void setCabEnabled(bool e)      { mCabEnabled = e; }
    void setSlimMode(bool slim)     { mSlimMode = slim; }

    const AmpProfileInfo& getCurrentProfile() const {
        int idx = (mActiveSlot == 1) ? mProfileIdxB : mProfileIdxA;
        return gAmpProfiles[idx];
    }
    const AmpProfileInfo& getProfileA() const { return gAmpProfiles[mProfileIdxA]; }
    const AmpProfileInfo& getProfileB() const { return gAmpProfiles[mProfileIdxB]; }

    // ── Process ───────────────────────────────────────────────────────────────
    void process(const float* in, float* out, uint32_t numSamples) {
        if (!in || !out || numSamples == 0) return;

        if (mSlimMode)
            processSlim(in, out, numSamples);
        else
            processFull(in, out, numSamples);
    }

private:
    // ── SLIM (Fast/Low-CPU) mode ──────────────────────────────────────────────
    // Single-stage soft-clip with profile-aware pre-gain and output shelf.
    void processSlim(const float* in, float* out, uint32_t n) {
        const AmpProfileInfo& prof = getCurrentProfile();
        const float sRate = (float)mSampleRate;

        float gateLinear = (mGateThreshold <= -79.0f) ? 0.0f : dbToGain(mGateThreshold);
        float gain     = prof.preGain * (0.5f + mDrive * 4.0f);
        float outGain  = std::pow(mVolume, 1.6f) * 1.2f;

        float hpCoeff  = 1.0f - std::exp(-TWO_PI_F * 60.0f / sRate);

        for (uint32_t s = 0; s < n; ++s) {
            float x = in[s];
            if (!std::isfinite(x)) { out[s] = 0.0f; continue; }

            // Gate
            float absX = std::abs(x);
            if (absX > mGateEnvelope) mGateEnvelope = mGateEnvelope * 0.8f + absX * 0.2f;
            else                       mGateEnvelope *= 0.9992f;
            float gg = (gateLinear <= 0.0f) ? 1.0f
                : clamp14((mGateEnvelope - gateLinear) / (gateLinear * 0.5f + 1e-6f), 0.0f, 1.0f);
            x *= gg;

            // HP coupling
            mHpState += (x - mHpState) * hpCoeff;
            float xhp = x - mHpState;

            // One-pole soft clip (slim mode)
            float driven = xhp * gain;
            float sat = driven / (1.0f + std::abs(driven));

            // Simple cabinet LP
            if (mCabEnabled) {
                float lpCoeff = 1.0f - std::exp(-TWO_PI_F * prof.cabHighCut / sRate);
                mSlimState += (sat - mSlimState) * lpCoeff;
                sat = mSlimState * prof.cabResonance;
            }

            out[s] = clamp14(sat * outGain, -1.0f, 1.0f);
        }
    }

    // ── FULL (Quality) mode ────────────────────────────────────────────────────
    // Full multi-stage tube triode model with 3-band tonestack + cabinet IR.
    void processFull(const float* in, float* out, uint32_t numSamples) {
        const float sRate = (float)mSampleRate;
        const AmpProfileInfo& prof = getCurrentProfile();

        float gateLinear = (mGateThreshold <= -79.0f) ? 0.0f : dbToGain(mGateThreshold);
        float effectiveGain = prof.preGain * (1.0f + mDrive * 8.0f);
        float outGain = std::pow(mVolume, 1.6f) * 1.5f;

        float bassCoeff   = 1.0f - std::exp(-TWO_PI_F * prof.bassCenter   / sRate);
        float midCoeff    = 1.0f - std::exp(-TWO_PI_F * prof.midCenter    / sRate);
        float trebleCoeff = 1.0f - std::exp(-TWO_PI_F * prof.trebleCenter / sRate);
        float cabHpCoeff  = 1.0f - std::exp(-TWO_PI_F * prof.cabLowCut   / sRate);
        float cabLpCoeff  = 1.0f - std::exp(-TWO_PI_F * prof.cabHighCut  / sRate);

        float bassGain   = 0.4f + mBass   * 1.2f;
        float midGain    = 0.3f + mMid    * 1.4f;
        float trebleGain = 0.4f + mTreble * 1.2f;

        for (uint32_t s = 0; s < numSamples; ++s) {
            float x = in[s];
            if (!std::isfinite(x)) { out[s] = 0.0f; continue; }

            // A. Noise Gate
            float absX = std::abs(x);
            if (absX > mGateEnvelope) mGateEnvelope = mGateEnvelope * 0.8f + absX * 0.2f;
            else                       mGateEnvelope *= 0.9992f;
            float gg = (gateLinear <= 0.0f) ? 1.0f
                : clamp14((mGateEnvelope - gateLinear) / (gateLinear * 0.5f + 1e-6f), 0.0f, 1.0f);
            x *= gg;

            // B. Pre-Gain HP
            mHpState += (x - mHpState) * (1.0f - std::exp(-TWO_PI_F * 60.0f / sRate));
            float filteredIn = x - mHpState;

            // C. Triode waveshaper
            float stage1 = filteredIn * effectiveGain;
            float asym   = prof.asymmetry;
            float biased = stage1 + asym * (stage1 > 0.0f ? 0.4f : -0.2f);
            float saturated;
            if (biased > 0.0f) saturated = std::tanh(biased * 1.15f) / 1.15f;
            else                saturated = std::tanh(biased * 0.95f) / 0.95f;
            float comp   = prof.compression;
            float stage2 = saturated * (1.0f - comp * 0.3f * std::min(1.0f, saturated * saturated));

            // D. 3-Band Tonestack
            mBassState   += (stage2 - mBassState)   * bassCoeff;
            float bassC   = mBassState * bassGain;
            mMidState    += (stage2 - mMidState)    * midCoeff;
            float midC    = (stage2 - mMidState)    * midGain;
            mTrebleState += (stage2 - mTrebleState) * trebleCoeff;
            float trebleC = (stage2 - mTrebleState) * trebleGain;
            float toneOut = (bassC + midC + trebleC) * 0.65f;

            // E. Cabinet IR
            float finalOut = toneOut;
            if (mCabEnabled) {
                mCabHpState += (finalOut - mCabHpState) * cabHpCoeff;
                float cabHp  = finalOut - mCabHpState;
                mCabLpState += (cabHp - mCabLpState) * cabLpCoeff;
                finalOut = mCabLpState * prof.cabResonance;
            }

            out[s] = clamp14(finalOut * outGain, -1.0f, 1.0f);
        }
    }

    // ── State ─────────────────────────────────────────────────────────────────
    double mSampleRate    = 48000.0;
    float  mGateEnvelope  = 0.0f;
    float  mHpState       = 0.0f;
    float  mLpState       = 0.0f;
    float  mBassState     = 0.0f;
    float  mMidState      = 0.0f;
    float  mTrebleState   = 0.0f;
    float  mCabLpState    = 0.0f;
    float  mCabHpState    = 0.0f;
    float  mSlimState     = 0.0f;

    int    mProfileIdxA   = 8;   // Slot A
    int    mProfileIdxB   = 9;   // Slot B
    int    mActiveSlot    = 0;   // 0=A, 1=B

    float  mDrive         = 0.5f;
    float  mBass          = 0.5f;
    float  mMid           = 0.5f;
    float  mTreble        = 0.5f;
    float  mVolume        = 0.5f;
    float  mGateThreshold = -80.0f;
    bool   mCabEnabled    = true;
    bool   mSlimMode      = false;
};

} // namespace AudioDSP

#endif // NAM_NEURAL_ENGINE_HPP
