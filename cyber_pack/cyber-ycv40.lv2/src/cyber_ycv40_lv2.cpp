/*
 * Cyber YCV40 - Traynor Custom Valve 40 (2x10" Celestion Edition) LV2 Plugin
 * Authentic all-tube analog circuit emulation with swappable power tubes,
 * dynamic B+ sag, community circuit mods, Dynamic Speaker Drive & Compression,
 * and Smart Zero-Floor Clean-Input Sidechain Noise Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-ycv40"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_CLEAN_VOL      = 4,
    PORT_CLEAN_TREBLE   = 5,
    PORT_CLEAN_BASS     = 6,
    PORT_BRIGHT         = 7,
    PORT_LEAD_GAIN      = 8,
    PORT_LEAD_VOL       = 9,
    PORT_LEAD_BASS      = 10,
    PORT_LEAD_MID       = 11,
    PORT_LEAD_TREBLE    = 12,
    PORT_BOOST          = 13,
    PORT_SCOOP          = 14,
    PORT_MASTER         = 15,
    PORT_REVERB         = 16,
    PORT_PRESENCE       = 17,
    PORT_POWER_TUBES    = 18,
    PORT_MOD_C10        = 19,
    PORT_MOD_SMOOTH     = 20,
    PORT_MOD_CHIME      = 21,
    PORT_SPEAKER_CAB    = 22,
    PORT_SPEAKER_DRIVE  = 23,
    PORT_NOISE_GATE     = 24,
    PORT_OUTPUT_LEVEL   = 25,
    PORT_COUNT          = 25 + 1
};

// -----------------------------------------------------------------------------
// 1st-Order IIR Filter (Single-Pole for Plate Snubbers & DC Blockers)
// -----------------------------------------------------------------------------
struct OnePole {
    float z = 0.0f;

    inline float lp(float x, float fc, float sr) {
        float w = 2.0f * (float)M_PI * fc / sr;
        float a0 = w / (1.0f + w);
        z += a0 * (x - z);
        return z;
    }

    inline float hp(float x, float fc, float sr) {
        return x - lp(x, fc, sr);
    }

    inline void reset() { z = 0.0f; }
};

// -----------------------------------------------------------------------------
// Biquad Filter (Direct Form II Transposed)
// -----------------------------------------------------------------------------
struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float s1 = 0.0f, s2 = 0.0f;

    inline float process(float in) {
        float out = b0 * in + s1;
        s1 = b1 * in - a1 * out + s2;
        s2 = b2 * in - a2 * out;
        return out;
    }

    void reset() { s1 = s2 = 0.0f; }

    void setIdentity() {
        b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f;
    }

    void setLowPass(float fc, float Q, float sr) {
        float omega = 2.0f * (float)M_PI * fc / sr;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float alpha = sn / (2.0f * Q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f - cs) * 0.5f) / a0;
        b1 = (1.0f - cs) / a0;
        b2 = ((1.0f - cs) * 0.5f) / a0;
        a1 = (-2.0f * cs) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighPass(float fc, float Q, float sr) {
        float omega = 2.0f * (float)M_PI * fc / sr;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float alpha = sn / (2.0f * Q);

        float a0 = 1.0f + alpha;
        b0 = ((1.0f + cs) * 0.5f) / a0;
        b1 = (-(1.0f + cs)) / a0;
        b2 = ((1.0f + cs) * 0.5f) / a0;
        a1 = (-2.0f * cs) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setLowShelf(float fc, float gainDb, float Q, float sr) {
        float A = powf(10.0f, gainDb / 40.0f);
        float omega = 2.0f * (float)M_PI * fc / sr;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float beta = sqrtf(A + A);
        float a0 = (A + 1.0f) + (A - 1.0f) * cs + beta * sn;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cs + beta * sn)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cs - beta * sn)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cs - beta * sn) / a0;
    }

    void setHighShelf(float fc, float gainDb, float Q, float sr) {
        float A = powf(10.0f, gainDb / 40.0f);
        float omega = 2.0f * (float)M_PI * fc / sr;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float beta = sqrtf(A + A);
        float a0 = (A + 1.0f) - (A - 1.0f) * cs + beta * sn;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cs + beta * sn)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cs - beta * sn)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cs - beta * sn) / a0;
    }

    void setPeaking(float fc, float gainDb, float Q, float sr) {
        float A = powf(10.0f, gainDb / 40.0f);
        float omega = 2.0f * (float)M_PI * fc / sr;
        float sn = sinf(omega);
        float cs = cosf(omega);
        float alpha = sn / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cs) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cs) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }
};

// -----------------------------------------------------------------------------
// Accutronics Spring Reverb Emulation (Low-Noise Spring Tank)
// -----------------------------------------------------------------------------
class SpringReverb {
private:
    float delay1[2205];
    float delay2[2756];
    float delay3[3439];
    int idx1, idx2, idx3;
    float ap1[441], ap2[529];
    int ap_idx1, ap_idx2;
    float lpf, lpf_in;

public:
    void init() {
        memset(delay1, 0, sizeof(delay1));
        memset(delay2, 0, sizeof(delay2));
        memset(delay3, 0, sizeof(delay3));
        memset(ap1, 0, sizeof(ap1));
        memset(ap2, 0, sizeof(ap2));
        idx1 = idx2 = idx3 = 0;
        ap_idx1 = ap_idx2 = 0;
        lpf = lpf_in = 0.0f;
    }

    inline float process(float in, float amount) {
        if (amount <= 0.005f) return in;

        lpf_in += 0.35f * (in - lpf_in);
        float ap_in = lpf_in;

        float buf_val1 = ap1[ap_idx1];
        float ap_out1 = -0.55f * ap_in + buf_val1;
        ap1[ap_idx1] = ap_in + 0.55f * ap_out1;
        ap_idx1 = (ap_idx1 + 1) % 441;

        float buf_val2 = ap2[ap_idx2];
        float ap_out2 = -0.55f * ap_out1 + buf_val2;
        ap2[ap_idx2] = ap_out1 + 0.55f * ap_out2;
        ap_idx2 = (ap_idx2 + 1) % 529;

        float o1 = delay1[idx1];
        float o2 = delay2[idx2];
        float o3 = delay3[idx3];

        delay1[idx1] = ap_out2 + o1 * 0.68f - o2 * 0.12f;
        delay2[idx2] = ap_out2 + o2 * 0.65f + o3 * 0.10f;
        delay3[idx3] = ap_out2 + o3 * 0.62f - o1 * 0.15f;

        idx1 = (idx1 + 1) % 2205;
        idx2 = (idx2 + 1) % 2756;
        idx3 = (idx3 + 1) % 3439;

        float wet = (o1 + o2 + o3) * 0.33f;
        lpf += 0.20f * (wet - lpf);

        return in + lpf * (amount * 0.45f);
    }
};

// -----------------------------------------------------------------------------
// Cyber YCV40 Main Plugin Class
// -----------------------------------------------------------------------------
class CyberYCV40 {
public:
    CyberYCV40(double rate) : sampleRate(rate) {
        // Clean Channel Tone Stack
        cleanInputHp.setHighPass(65.0f, 0.707f, sampleRate);
        cleanBass.setIdentity();
        cleanTreble.setIdentity();
        cleanBright.setIdentity();

        // Lead Channel Tone Stack
        leadInputHp.setHighPass(75.0f, 0.707f, sampleRate);
        leadBass.setIdentity();
        leadMid.setIdentity();
        leadTreble.setIdentity();
        leadScoop.setIdentity();

        // Master & Presence
        presenceFilter.setIdentity();

        // Speaker Cabinet Filters
        cab210Low.setIdentity();
        cab210Mid.setIdentity();
        cab210Lp1.setIdentity();
        cab210Lp2.setIdentity();

        cab112Low.setIdentity();
        cab112Mid.setIdentity();
        cab112Lp1.setIdentity();
        cab112Lp2.setIdentity();

        reverb.init();

        sagVoltage = 1.0f;
        gridCap1 = gridCap2 = gridCap3 = 0.0f;

        // Dynamic Speaker State Initialization
        speakerEnv = 0.0f;
        speakerThermalEnv = 0.0f;
        speakerConeHistory = 0.0f;
        spkAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));
        spkRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.045f));
        spkThermalRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.400f));

        // Smart Zero-Floor Noise Suppressor State
        gateScHp.reset();
        gateScLp.reset();
        gateEnv = 0.0f;
        gateGain = 0.0f;
        gateIsOpen = false;
        gateAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));      // 1.5 ms attack
        gateRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.220f));       // 220 ms detector release
        gateAtkSmooth = 1.0f - expf(-1.0f / ((float)sampleRate * 0.001f)); // 1.0 ms gain opening
        gateRelSmooth = 1.0f - expf(-1.0f / ((float)sampleRate * 0.180f)); // 180 ms gain fadeout

        initCabFilters();
    }

    void initCabFilters() {
        // 2x10" Celestion Tube 10 (Tight punchy 95Hz resonance, 2.4kHz bite, steep 24dB/oct roll-off at 5.2kHz)
        cab210Low.setPeaking(95.0f, 4.5f, 1.2f, sampleRate);
        cab210Mid.setPeaking(2400.0f, 3.8f, 1.4f, sampleRate);
        cab210Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cab210Lp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 1x12" Celestion Vintage 30 (Deeper 75Hz thump, aggressive 3.0kHz spike, steep 24dB/oct roll-off at 4.8kHz)
        cab112Low.setPeaking(75.0f, 5.2f, 1.1f, sampleRate);
        cab112Mid.setPeaking(3000.0f, 4.8f, 1.6f, sampleRate);
        cab112Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cab112Lp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    // Smooth C-infinity 12AX7 Triode Non-Linear Transfer Function
    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.005f;
        } else {
            gridBias *= 0.9998f;
        }

        float effIn = (in - gridBias * 0.20f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // Swappable Power Tube Transfer Functions
    inline float powerTubeStage(float in, int tubeType, float sag) {
        float effectiveBPlus = std::max(0.65f, sag);
        float x = in / effectiveBPlus;

        switch (tubeType) {
            case 1: // EL34 (British crunch, punchy midrange forward bite)
            {
                float t = tanhf(x * 1.35f);
                return (t - 0.03f * (t * t)) * effectiveBPlus;
            }
            case 2: // 6V6GT (Vintage American lower-wattage sag and compression)
            {
                return tanhf(x * 1.45f) * 0.92f * effectiveBPlus;
            }
            case 3: // KT88 (Colossal clean headroom, ultra-tight dynamics)
            {
                return (tanhf(x * 0.85f) / 0.85f) * effectiveBPlus;
            }
            case 0: // 6L6GC (Stock: Punchy bass, scooped lower mids, sparkling top)
            default:
            {
                float t = tanhf(x * 1.15f);
                return (t - 0.06f * (t * t)) * effectiveBPlus;
            }
        }
    }

    // Dynamic Speaker Simulation (Voice coil stress, cone breakup, thermal compression & acoustic filtering)
    inline float processSpeaker(float in, int cabType, float driveVal) {
        if (cabType == 2) return in; // Bypass / Direct Out

        float s = in;

        // 1. Dynamic Speaker Drive / Voice Coil Stress & Cone Compression
        if (driveVal > 0.05f) {
            float driveAmount = driveVal / 10.0f;

            float rect = fabsf(s);
            if (rect > speakerEnv) {
                speakerEnv += spkAtk * (rect - speakerEnv);
            } else {
                speakerEnv += spkRel * (rect - speakerEnv);
            }

            speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.5f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.35f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.4f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.05f * (t * t)) / (1.0f + driveAmount * 0.35f);

            float dampingFc = 6200.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.30f);
        }

        // 2. Combo Cabinet Acoustic Filtering (Celestion 2x10 vs 1x12)
        if (cabType == 1) {
            s = cab112Low.process(s);
            s = cab112Mid.process(s);
            s = cab112Lp1.process(s);
            s = cab112Lp2.process(s);
            return s * 0.95f;
        } else {
            s = cab210Low.process(s);
            s = cab210Mid.process(s);
            s = cab210Lp1.process(s);
            s = cab210Lp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(
        float ch, float cVol, float cTreb, float cBass, float cBright,
        float lGain, float lVol, float lBass, float lMid, float lTreb, float lBoost, float lScoop,
        float master, float pres, float modC10, float modSmooth, float modChime
    ) {
        // Clean Tone Stack
        float cBassDb = (cBass - 5.0f) * 2.6f;
        cleanBass.setLowShelf(120.0f, cBassDb, 0.7f, sampleRate);

        float cTrebDb = (cTreb - 5.0f) * 2.4f;
        cleanTreble.setHighShelf(3200.0f, cTrebDb, 0.7f, sampleRate);

        // Bright Cap C10
        if (cBright > 0.5f) {
            float brightAtten = std::max(0.0f, (10.0f - cVol) / 10.0f);
            float freq = (modC10 > 0.5f) ? 2400.0f : 4200.0f;
            float boostDb = (modC10 > 0.5f) ? (brightAtten * 4.0f) : (brightAtten * 7.5f);
            cleanBright.setHighShelf(freq, boostDb, 0.7f, sampleRate);
        } else {
            cleanBright.setIdentity();
        }

        // Lead Tone Stack
        float lBassDb = (lBass - 5.0f) * 2.4f;
        leadBass.setLowShelf(140.0f, lBassDb, 0.7f, sampleRate);

        float lMidDb = (lMid - 5.0f) * 2.6f;
        leadMid.setPeaking(750.0f, lMidDb, 1.2f, sampleRate);

        float lTrebDb = (lTreb - 5.0f) * 2.4f;
        leadTreble.setHighShelf(3000.0f, lTrebDb, 0.7f, sampleRate);

        // Lead Scoop Switch
        if (lScoop > 0.5f) {
            leadScoop.setPeaking(650.0f, -5.5f, 1.5f, sampleRate);
        } else {
            leadScoop.setIdentity();
        }

        // Presence in Negative Feedback Loop (Computed once per buffer!)
        float gainDb = (pres / 10.0f) * 7.0f;
        if (modChime > 0.5f) gainDb += 2.0f;
        presenceFilter.setHighShelf(3800.0f, gainDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel,
        float pCleanVol, float pCleanTreble, float pCleanBass, float pBright,
        float pLeadGain, float pLeadVol, float pLeadBass, float pLeadMid, float pLeadTreble, float pBoost, float pScoop,
        float pMaster, float pReverb, float pPresence, float pPowerTubes,
        float pModC10, float pModSmooth, float pModChime, float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) {
                memcpy(output, input, n_samples * sizeof(float));
            }
            return;
        }

        updateParams(
            channel, pCleanVol, pCleanTreble, pCleanBass, pBright,
            pLeadGain, pLeadVol, pLeadBass, pLeadMid, pLeadTreble, pBoost, pScoop,
            pMaster, pPresence, pModC10, pModSmooth, pModChime
        );

        int tubeType = (int)std::max(0.0f, std::min(3.0f, pPowerTubes));
        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isLead = (channel > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float masterGain = audioTaper(pMaster, 3.2f);
        float cleanGain  = audioTaper(pCleanVol, 2.4f);
        float leadDrive  = audioTaper(pLeadGain, 3.8f);
        if (pBoost > 0.5f) leadDrive *= 1.8f;
        float leadLevel  = audioTaper(pLeadVol, 2.0f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            // 1. High-Pass DC Blocker at amp input
            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // 2. V1A Input Buffer Triode Stage (12AX7)
            s = triode12AX7(s, gridCap1, 1.6f, 0.10f);
            s = snubberV1A.lp(s, 11000.0f, sampleRate);

            if (!isLead) {
                // --- CLEAN CHANNEL ---
                s = cleanInputHp.process(s);
                s = cleanBass.process(s);
                s = cleanTreble.process(s);
                s = cleanBright.process(s);

                s *= cleanGain;

                s = triode12AX7(s, gridCap2, 1.4f, 0.10f);
                s = snubberV1B.lp(s, 10000.0f, sampleRate);
            } else {
                // --- LEAD CHANNEL ---
                s = leadInputHp.process(s);
                s *= leadDrive;

                s = triode12AX7(s, gridCap2, 2.0f, 0.12f);
                s = snubberV2A.lp(s, 12000.0f, sampleRate);

                s = leadBass.process(s);
                s = leadMid.process(s);
                s = leadTreble.process(s);
                s = leadScoop.process(s);

                float asym2 = (pModSmooth > 0.5f) ? 0.06f : 0.14f;
                float gain2 = (pModSmooth > 0.5f) ? 1.6f : 2.2f;
                s = triode12AX7(s, gridCap3, gain2, asym2);
                s = snubberV2B.lp(s, 7500.0f, sampleRate);

                s *= leadLevel;
            }

            // 3. V3 Long-Tailed Pair Phase Inverter & Master
            float push = s * masterGain;
            float pull = -s * masterGain * 0.97f;

            // 4. Push-Pull Power Stage with Swappable Tubes & Dynamic B+ Sag
            float pOut = (powerTubeStage(push, tubeType, sagVoltage) -
                          powerTubeStage(pull, tubeType, sagVoltage)) * 0.5f;

            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // Dynamic Power Supply Sag
            float currentDraw = fabsf(pOut) * 0.035f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0015f;
            sagVoltage = std::max(0.70f, std::min(1.0f, sagVoltage));

            // Presence in Negative Feedback Loop
            pOut = presenceFilter.process(pOut);

            // Post DC Blocker
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // 5. Accutronics Spring Reverb
            pOut = reverb.process(pOut, pReverb / 10.0f);

            // 6. Dynamic Speaker Simulation with Voice Coil Compression & Cone Breakup
            pOut = processSpeaker(pOut, cabType, pSpeakerDrive);

            // 7. Smart Zero-Floor Noise Suppressor (Clean-Input Keyed Downward Expander)
            if (useGate) {
                float sc = gateScLp.lp(gateScHp.hp(rawIn, 100.0f, sampleRate), 3200.0f, sampleRate);
                float absSc = fabsf(sc);
                if (absSc > gateEnv) gateEnv += gateAtk * (absSc - gateEnv);
                else gateEnv += gateRel * (absSc - gateEnv);

                const float threshOpen = 0.00100f;  // ~ -60.0 dBFS
                const float threshClose = 0.00045f; // ~ -66.9 dBFS

                if (!gateIsOpen) {
                    if (gateEnv >= threshOpen) gateIsOpen = true;
                } else {
                    if (gateEnv < threshClose) gateIsOpen = false;
                }

                float targetGain = gateIsOpen ? 1.0f : 0.0f;
                float smoothRate = (targetGain > gateGain) ? gateAtkSmooth : gateRelSmooth;
                gateGain += smoothRate * (targetGain - gateGain);
                pOut *= gateGain;
            }
            float outTrim = 1.0f;
            if (pOutputLevel <= 0.05f) {
                outTrim = 0.0f;
            } else {
                float db = (pOutputLevel - 7.0f) * 3.5f;
                outTrim = powf(10.0f, db / 20.0f);
            }
            pOut *= outTrim;


            output[i] = pOut;
        }
    }

private:
    double sampleRate;

    Biquad cleanInputHp, cleanBass, cleanTreble, cleanBright;
    Biquad leadInputHp, leadBass, leadMid, leadTreble, leadScoop;
    Biquad presenceFilter;

    Biquad cab210Low, cab210Mid, cab210Lp1, cab210Lp2;
    Biquad cab112Low, cab112Mid, cab112Lp1, cab112Lp2;

    OnePole snubberV1A, snubberV1B, snubberV2A, snubberV2B, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    SpringReverb reverb;

    float sagVoltage;
    float gridCap1, gridCap2, gridCap3;

    // Dynamic Speaker State
    float speakerEnv;
    float speakerThermalEnv;
    float speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    // Smart Zero-Floor Noise Suppressor State
    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

// -----------------------------------------------------------------------------
// LV2 C-Interface Wrapper
// -----------------------------------------------------------------------------
struct CyberYCV40LV2 {
    CyberYCV40* dsp;

    const float* audioIn;
    float* audioOut;

    const float* bypass;
    const float* channel;
    const float* cleanVol;
    const float* cleanTreble;
    const float* cleanBass;
    const float* bright;
    const float* leadGain;
    const float* leadVol;
    const float* leadBass;
    const float* leadMid;
    const float* leadTreble;
    const float* boost;
    const float* scoop;
    const float* master;
    const float* reverb;
    const float* presence;
    const float* powerTubes;
    const float* modC10;
    const float* modSmooth;
    const float* modChime;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberYCV40LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), cleanVol(nullptr), cleanTreble(nullptr), cleanBass(nullptr), bright(nullptr),
        leadGain(nullptr), leadVol(nullptr), leadBass(nullptr), leadMid(nullptr), leadTreble(nullptr),
        boost(nullptr), scoop(nullptr), master(nullptr), reverb(nullptr), presence(nullptr),
        powerTubes(nullptr), modC10(nullptr), modSmooth(nullptr), modChime(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}

    ~CyberYCV40LV2() {
        if (dsp) delete dsp;
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    CyberYCV40LV2* handle = new CyberYCV40LV2();
    handle->dsp = new CyberYCV40(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberYCV40LV2* h = (CyberYCV40LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_CHANNEL:      h->channel = (const float*)data; break;
        case PORT_CLEAN_VOL:    h->cleanVol = (const float*)data; break;
        case PORT_CLEAN_TREBLE: h->cleanTreble = (const float*)data; break;
        case PORT_CLEAN_BASS:   h->cleanBass = (const float*)data; break;
        case PORT_BRIGHT:       h->bright = (const float*)data; break;
        case PORT_LEAD_GAIN:    h->leadGain = (const float*)data; break;
        case PORT_LEAD_VOL:     h->leadVol = (const float*)data; break;
        case PORT_LEAD_BASS:    h->leadBass = (const float*)data; break;
        case PORT_LEAD_MID:     h->leadMid = (const float*)data; break;
        case PORT_LEAD_TREBLE:  h->leadTreble = (const float*)data; break;
        case PORT_BOOST:        h->boost = (const float*)data; break;
        case PORT_SCOOP:        h->scoop = (const float*)data; break;
        case PORT_MASTER:       h->master = (const float*)data; break;
        case PORT_REVERB:       h->reverb = (const float*)data; break;
        case PORT_PRESENCE:     h->presence = (const float*)data; break;
        case PORT_POWER_TUBES:  h->powerTubes = (const float*)data; break;
        case PORT_MOD_C10:      h->modC10 = (const float*)data; break;
        case PORT_MOD_SMOOTH:   h->modSmooth = (const float*)data; break;
        case PORT_MOD_CHIME:    h->modChime = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberYCV40LV2* h = (CyberYCV40LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 0.0f,
        h->cleanVol ? *h->cleanVol : 5.0f,
        h->cleanTreble ? *h->cleanTreble : 5.0f,
        h->cleanBass ? *h->cleanBass : 5.0f,
        h->bright ? *h->bright : 0.0f,
        h->leadGain ? *h->leadGain : 6.0f,
        h->leadVol ? *h->leadVol : 5.0f,
        h->leadBass ? *h->leadBass : 5.0f,
        h->leadMid ? *h->leadMid : 5.0f,
        h->leadTreble ? *h->leadTreble : 5.0f,
        h->boost ? *h->boost : 0.0f,
        h->scoop ? *h->scoop : 0.0f,
        h->master ? *h->master : 5.0f,
        h->reverb ? *h->reverb : 2.5f,
        h->presence ? *h->presence : 5.0f,
        h->powerTubes ? *h->powerTubes : 0.0f,
        h->modC10 ? *h->modC10 : 0.0f,
        h->modSmooth ? *h->modSmooth : 0.0f,
        h->modChime ? *h->modChime : 0.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    CyberYCV40LV2* h = (CyberYCV40LV2*)instance;
    if (h) delete h;
}

static const void* extension_data(const char* uri) {
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
