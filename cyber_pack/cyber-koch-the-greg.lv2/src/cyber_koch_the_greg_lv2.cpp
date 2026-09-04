/*
 * Cyber Koch "The Greg" Signature - Boutique 50W Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Koch's Greg Koch signature amp,
 * featuring 3-channel topology, internal Output Tube Saturation (OTS),
 * dual-band Harmonic Tube Tremolo, 2x EL34 power stage, Dynamic Speaker Stress,
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-koch-the-greg"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_GAIN           = 4,
    PORT_VOLUME         = 5,
    PORT_BASS           = 6,
    PORT_MID            = 7,
    PORT_TREBLE         = 8,
    PORT_OTS_DRIVE      = 9,
    PORT_OTS_LEVEL      = 10,
    PORT_OTS_SWITCH     = 11,
    PORT_TREM_SPEED     = 12,
    PORT_TREM_DEPTH     = 13,
    PORT_TREM_MODE      = 14,
    PORT_MASTER         = 15,
    PORT_PRESENCE       = 16,
    PORT_SPEAKER_CAB    = 17,
    PORT_SPEAKER_DRIVE  = 18,
    PORT_NOISE_GATE     = 19,
    PORT_OUTPUT_LEVEL   = 20,
    PORT_COUNT          = 20 + 1
};

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
    void setIdentity() { b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f; }

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

class CyberKochTheGreg {
public:
    CyberKochTheGreg(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        presenceFilter.setIdentity();

        tremLp.setIdentity();

        cab112Low.setIdentity();
        cab112Mid.setIdentity();
        cab112Lp1.setIdentity();
        cab112Lp2.setIdentity();

        cab212Low.setIdentity();
        cab212Mid.setIdentity();
        cab212Lp1.setIdentity();
        cab212Lp2.setIdentity();

        sagVoltage = 1.0f;
        gridCap1 = gridCap2 = gridCap3 = gridCapOts = 0.0f;
        lfoPhase = 0.0f;

        speakerEnv = speakerThermalEnv = speakerConeHistory = 0.0f;
        spkAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0015f));
        spkRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.045f));
        spkThermalRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.400f));

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
        // Koch VG12-90 1x12 (Tight 85Hz thump, vocal 2.4kHz bite, smooth 5.2kHz roll-off)
        cab112Low.setPeaking(85.0f, 4.2f, 1.2f, sampleRate);
        cab112Mid.setPeaking(2400.0f, 3.5f, 1.4f, sampleRate);
        cab112Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cab112Lp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // Koch Custom 2x12 Open-Back (Deeper 75Hz low-end, rich 2.8kHz presence, 5.0kHz steep cut)
        cab212Low.setPeaking(75.0f, 5.0f, 1.1f, sampleRate);
        cab212Mid.setPeaking(2800.0f, 4.0f, 1.5f, sampleRate);
        cab212Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cab212Lp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

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

    // 2x EL34 50W Power Stage
    inline float powerTubeStage(float in, float sag) {
        float effectiveBPlus = std::max(0.66f, sag);
        float x = in / effectiveBPlus;
        if (x < -0.70f) {
            return (-0.70f - 0.20f * tanhf((x + 0.70f) * 1.4f)) * effectiveBPlus;
        }
        float t = tanhf(x * 1.30f);
        return (t - 0.05f * (t * t)) * effectiveBPlus;
    }

    inline float processSpeaker(float in, int cabType, float driveVal) {
        if (cabType == 2) return in;

        float s = in;
        if (driveVal > 0.05f) {
            float driveAmount = driveVal / 10.0f;
            float rect = fabsf(s);
            if (rect > speakerEnv) speakerEnv += spkAtk * (rect - speakerEnv);
            else speakerEnv += spkRel * (rect - speakerEnv);
            speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.4f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.30f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.3f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.04f * (t * t)) / (1.0f + driveAmount * 0.30f);

            float dampingFc = 6400.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.28f);
        }

        if (cabType == 1) {
            s = cab212Low.process(s);
            s = cab212Mid.process(s);
            s = cab212Lp1.process(s);
            s = cab212Lp2.process(s);
            return s * 1.05f;
        } else {
            s = cab112Low.process(s);
            s = cab112Mid.process(s);
            s = cab112Lp1.process(s);
            s = cab112Lp2.process(s);
            return s * 1.0f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.22f * norm + 0.78f * norm * norm * norm) * (maxGain * 1.55f);
    }

    void updateParams(float bass, float mid, float treb, float pres) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(125.0f, bassDb, 0.7f, sampleRate);

        float midDb = (mid - 5.0f) * 2.6f;
        toneMid.setPeaking(720.0f, midDb, 1.2f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.5f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        float presDb = (pres / 10.0f) * 7.0f;
        presenceFilter.setHighShelf(3800.0f, presDb, 0.7f, sampleRate);

        // Harmonic tremolo crossover split at 750 Hz
        tremLp.setLowPass(750.0f, 0.707f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pGain, float pVolume,
        float pBass, float pMid, float pTreble,
        float pOtsDrive, float pOtsLevel, float pOtsSwitch,
        float pTremSpeed, float pTremDepth, float pTremMode,
        float pMaster, float pPresence,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pBass, pMid, pTreble, pPresence);

        int ch = (int)std::max(0.0f, std::min(2.0f, channel));
        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool useOts = (pOtsSwitch > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float chanGain = audioTaper(pGain, (ch == 2) ? 4.5f : ((ch == 1) ? 3.2f : 1.8f));
        float chanVol = audioTaper(pVolume, 2.5f);
        float masterGain = audioTaper(pMaster, 3.2f);
        float otsGain = audioTaper(pOtsDrive, 3.5f);
        float otsVol = audioTaper(pOtsLevel, 2.2f);

        float lfoInc = (2.0f * (float)M_PI * pTremSpeed) / (float)sampleRate;
        float tremAmt = pTremDepth / 100.0f;
        bool isHarmonic = (pTremMode > 0.5f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // V1A First Preamp Stage
            s = triode12AX7(s, gridCap1, 1.6f, 0.08f);
            s = snubberV1A.lp(s, 11000.0f, sampleRate);

            // Channel Voicing
            if (ch == 0) {
                // Clean Channel
                s *= chanVol;
                s = triode12AX7(s, gridCap2, 1.35f, 0.08f);
            } else if (ch == 1) {
                // Crunch Channel
                s *= chanGain;
                s = triode12AX7(s, gridCap2, 2.0f, 0.12f);
                s = snubberV1B.lp(s, 12000.0f, sampleRate);
                s *= chanVol;
            } else {
                // Lead Channel (High Gain 3-Stage Cascade)
                s *= chanGain;
                s = triode12AX7(s, gridCap2, 2.2f, 0.12f);
                s = snubberV1B.lp(s, 12000.0f, sampleRate);
                s = triode12AX7(s, gridCap3, 2.2f, 0.14f);
                s = snubberV2A.lp(s, 12000.0f, sampleRate);
                s *= chanVol;
            }

            // 3-Band Tone Stack
            s = toneBass.process(s);
            s = toneMid.process(s);
            s = toneTreble.process(s);

            // Output Tube Saturation (OTS) Circuit
            if (useOts) {
                float otsSig = s * otsGain;
                // Mini-tube power saturation model
                float t = tanhf(otsSig * 1.3f);
                otsSig = (t - 0.06f * (t * t)) * otsVol;
                s = s * 0.5f + otsSig * 0.7f;
            }

            // Tube Tremolo Section
            if (tremAmt > 0.01f) {
                float lfo = sinf(lfoPhase);
                lfoPhase += lfoInc;
                if (lfoPhase > 2.0f * (float)M_PI) lfoPhase -= 2.0f * (float)M_PI;

                if (isHarmonic) {
                    // Harmonic tremolo: split into low and high bands, modulate anti-phase
                    float sLow = tremLp.process(s);
                    float sHigh = s - sLow;
                    float modL = 1.0f + (lfo * 0.6f * tremAmt);
                    float modH = 1.0f - (lfo * 0.6f * tremAmt);
                    s = (sLow * modL + sHigh * modH) * 0.85f;
                } else {
                    // Standard amplitude tremolo
                    float mod = 1.0f - (tremAmt * 0.5f * (1.0f + lfo));
                    s *= mod;
                }
            }

            // Phase Inverter & 2x EL34 Power Stage
            float push = s * masterGain;
            float pull = -s * masterGain * 0.98f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            float currentDraw = fabsf(pOut) * 0.035f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0018f;
            sagVoltage = std::max(0.70f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic Speaker Simulation (VG12-90 1x12 vs 2x12)
            pOut = processSpeaker(pOut, cabType, pSpeakerDrive);

            // Smart Zero-Floor Noise Suppressor
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
    Biquad toneBass, toneMid, toneTreble, presenceFilter;
    Biquad tremLp;
    Biquad cab112Low, cab112Mid, cab112Lp1, cab112Lp2;
    Biquad cab212Low, cab212Mid, cab212Lp1, cab212Lp2;
    OnePole snubberV1A, snubberV1B, snubberV2A, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCap1, gridCap2, gridCap3, gridCapOts;
    float lfoPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberKochTheGregLV2 {
    CyberKochTheGreg* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* gain;
    const float* volume;
    const float* bass;
    const float* mid;
    const float* treble;
    const float* otsDrive;
    const float* otsLevel;
    const float* otsSwitch;
    const float* tremSpeed;
    const float* tremDepth;
    const float* tremMode;
    const float* master;
    const float* presence;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberKochTheGregLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), gain(nullptr), volume(nullptr), bass(nullptr),
        mid(nullptr), treble(nullptr), otsDrive(nullptr), otsLevel(nullptr), otsSwitch(nullptr),
        tremSpeed(nullptr), tremDepth(nullptr), tremMode(nullptr), master(nullptr), presence(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberKochTheGregLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberKochTheGregLV2* handle = new CyberKochTheGregLV2();
    handle->dsp = new CyberKochTheGreg(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberKochTheGregLV2* h = (CyberKochTheGregLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_CHANNEL:      h->channel = (const float*)data; break;
        case PORT_GAIN:         h->gain = (const float*)data; break;
        case PORT_VOLUME:       h->volume = (const float*)data; break;
        case PORT_BASS:         h->bass = (const float*)data; break;
        case PORT_MID:          h->mid = (const float*)data; break;
        case PORT_TREBLE:       h->treble = (const float*)data; break;
        case PORT_OTS_DRIVE:    h->otsDrive = (const float*)data; break;
        case PORT_OTS_LEVEL:    h->otsLevel = (const float*)data; break;
        case PORT_OTS_SWITCH:   h->otsSwitch = (const float*)data; break;
        case PORT_TREM_SPEED:   h->tremSpeed = (const float*)data; break;
        case PORT_TREM_DEPTH:   h->tremDepth = (const float*)data; break;
        case PORT_TREM_MODE:    h->tremMode = (const float*)data; break;
        case PORT_MASTER:       h->master = (const float*)data; break;
        case PORT_PRESENCE:     h->presence = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberKochTheGregLV2* h = (CyberKochTheGregLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 0.0f,
        h->gain ? *h->gain : 6.0f,
        h->volume ? *h->volume : 5.0f,
        h->bass ? *h->bass : 5.0f,
        h->mid ? *h->mid : 5.5f,
        h->treble ? *h->treble : 5.5f,
        h->otsDrive ? *h->otsDrive : 4.0f,
        h->otsLevel ? *h->otsLevel : 5.0f,
        h->otsSwitch ? *h->otsSwitch : 1.0f,
        h->tremSpeed ? *h->tremSpeed : 4.0f,
        h->tremDepth ? *h->tremDepth : 0.0f,
        h->tremMode ? *h->tremMode : 1.0f,
        h->master ? *h->master : 5.0f,
        h->presence ? *h->presence : 5.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberKochTheGregLV2* h = (CyberKochTheGregLV2*)instance; if (h) delete h; }
static const void* extension_data(const char* uri) { return nullptr; }

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
