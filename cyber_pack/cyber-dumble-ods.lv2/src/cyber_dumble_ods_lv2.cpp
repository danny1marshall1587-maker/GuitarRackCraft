/*
 * Cyber Dumble Overdrive Special (ODS) - Boutique Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the legendary Dumble ODS with Skyliner
 * tone stack, Rock/Jazz voicing, Overdrive Ratio, Dynamic Speaker Stress,
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-dumble-ods"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_CLEAN_VOL      = 4,
    PORT_CLEAN_TREBLE   = 5,
    PORT_CLEAN_MID      = 6,
    PORT_CLEAN_BASS     = 7,
    PORT_BRIGHT         = 8,
    PORT_MID_BOOST      = 9,
    PORT_ROCK_JAZZ      = 10,
    PORT_OD_DRIVE       = 11,
    PORT_OD_LEVEL       = 12,
    PORT_MASTER         = 13,
    PORT_PRESENCE       = 14,
    PORT_SPEAKER_CAB    = 15,
    PORT_SPEAKER_DRIVE  = 16,
    PORT_NOISE_GATE     = 17,
    PORT_OUTPUT_LEVEL   = 18,
    PORT_COUNT          = 18 + 1
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

class CyberDumbleODS {
public:
    CyberDumbleODS(double rate) : sampleRate(rate) {
        cleanInputHp.setHighPass(60.0f, 0.707f, sampleRate);
        cleanBass.setIdentity();
        cleanMid.setIdentity();
        cleanTreble.setIdentity();
        brightFilter.setIdentity();
        presenceFilter.setIdentity();

        cabEvmLow.setIdentity();
        cabEvmMid.setIdentity();
        cabEvmLp1.setIdentity();
        cabEvmLp2.setIdentity();

        cab65Low.setIdentity();
        cab65Mid.setIdentity();
        cab65Lp1.setIdentity();
        cab65Lp2.setIdentity();

        sagVoltage = 1.0f;
        gridCap1 = gridCap2 = gridCap3 = gridCap4 = 0.0f;

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
        // 1x12 Electro-Voice EVM-12L in Oval Open-Back Cab (Huge tight 80Hz bass, neutral mids, extended 5.5kHz top)
        cabEvmLow.setPeaking(82.0f, 4.2f, 1.3f, sampleRate);
        cabEvmMid.setPeaking(2200.0f, 2.5f, 1.2f, sampleRate);
        cabEvmLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabEvmLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 2x12 Celestion G12-65 (Robben Ford tone: warm woody 90Hz punch, scooped 1.8kHz, smooth vocal roll-off)
        cab65Low.setPeaking(90.0f, 4.8f, 1.1f, sampleRate);
        cab65Mid.setPeaking(2600.0f, 3.4f, 1.4f, sampleRate);
        cab65Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cab65Lp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.005f;
        } else {
            gridBias *= 0.9998f;
        }
        float effIn = (in - gridBias * 0.22f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 6L6GC Dumble Power Stage with Dynamic B+ Sag
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
            if (rect > speakerEnv) {
                speakerEnv += spkAtk * (rect - speakerEnv);
            } else {
                speakerEnv += spkRel * (rect - speakerEnv);
            }
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
            s = cab65Low.process(s);
            s = cab65Mid.process(s);
            s = cab65Lp1.process(s);
            s = cab65Lp2.process(s);
            return s * 1.02f;
        } else {
            s = cabEvmLow.process(s);
            s = cabEvmMid.process(s);
            s = cabEvmLp1.process(s);
            s = cabEvmLp2.process(s);
            return s * 1.0f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(
        float ch, float cVol, float cTreb, float cMid, float cBass,
        float bright, float midBoost, float rockJazz,
        float odDrive, float odLevel, float master, float pres
    ) {
        // Dumble Skyliner Tone Stack
        // Rock mode: Punchy mid-forward with tight bottom. Jazz mode: Darker, flatter hi-fi with warmer bass.
        bool isJazz = (rockJazz > 0.5f);

        float bassFc = isJazz ? 110.0f : 140.0f;
        float bassDb = (cBass - 5.0f) * (isJazz ? 2.8f : 2.4f);
        cleanBass.setLowShelf(bassFc, bassDb, 0.7f, sampleRate);

        float midFc = (midBoost > 0.5f) ? 550.0f : (isJazz ? 700.0f : 850.0f);
        float midDb = (cMid - 5.0f) * 2.8f;
        if (midBoost > 0.5f) midDb += 4.5f;
        cleanMid.setPeaking(midFc, midDb, 1.1f, sampleRate);

        float trebFc = isJazz ? 2800.0f : 3400.0f;
        float trebDb = (cTreb - 5.0f) * (isJazz ? 2.0f : 2.6f);
        cleanTreble.setHighShelf(trebFc, trebDb, 0.7f, sampleRate);

        // Bright Switch
        if (bright > 0.5f) {
            float atten = std::max(0.0f, (10.0f - cVol) / 10.0f);
            brightFilter.setHighShelf(4200.0f, atten * 6.5f, 0.7f, sampleRate);
        } else {
            brightFilter.setIdentity();
        }

        // Presence
        float presDb = (pres / 10.0f) * 6.5f;
        presenceFilter.setHighShelf(3800.0f, presDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel,
        float pCleanVol, float pCleanTreble, float pCleanMid, float pCleanBass,
        float pBright, float pMidBoost, float pRockJazz,
        float pOdDrive, float pOdLevel, float pMaster, float pPresence,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(
            channel, pCleanVol, pCleanTreble, pCleanMid, pCleanBass,
            pBright, pMidBoost, pRockJazz,
            pOdDrive, pOdLevel, pMaster, pPresence
        );

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isOd = (channel > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float cleanGain = audioTaper(pCleanVol, 2.6f);
        float masterGain = audioTaper(pMaster, 3.2f);
        float odGain = audioTaper(pOdDrive, 4.5f);
        float odLvl = audioTaper(pOdLevel, 2.2f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // V1A First Gain Stage
            s = triode12AX7(s, gridCap1, 1.55f, 0.08f);
            s = snubberV1A.lp(s, 11500.0f, sampleRate);

            // Clean Skyliner Tone Stack
            s = cleanInputHp.process(s);
            s = cleanBass.process(s);
            s = cleanMid.process(s);
            s = cleanTreble.process(s);
            s = brightFilter.process(s);
            s *= cleanGain;

            // V1B Second Gain Stage (Clean recovery & buffer)
            s = triode12AX7(s, gridCap2, 1.45f, 0.08f);
            s = snubberV1B.lp(s, 10500.0f, sampleRate);

            if (isOd) {
                // Dumble Overdrive Circuit (V2A & V2B with Singing Vocal Midrange)
                float odSignal = s * odGain;

                // V2A Overdrive Stage
                odSignal = triode12AX7(odSignal, gridCap4, 2.2f, 0.12f);
                // Dumble 220pF Plate Snubber (Smooth vocal liquid highs)
                odSignal = snubberV2A.lp(odSignal, 7800.0f, sampleRate);

                // Interstage shaping (High-cut & low-cut to focus vocal mids)
                odSignal = odMidShaper.lp(odSignal, 6500.0f, sampleRate);

                // V2B Overdrive Recovery Stage
                odSignal = triode12AX7(odSignal, gridCap3, 1.85f, 0.10f);
                odSignal = snubberV2B.lp(odSignal, 7200.0f, sampleRate);

                s = odSignal * odLvl;
            }

            // Phase Inverter & Master
            float push = s * masterGain;
            float pull = -s * masterGain * 0.97f;

            // 6L6GC Push-Pull Power Stage with Sag
            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11800.0f, sampleRate);

            float currentDraw = fabsf(pOut) * 0.032f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0018f;
            sagVoltage = std::max(0.70f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic Speaker Simulation (EVM-12L vs G12-65 with Cone Stress)
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
    Biquad cleanInputHp, cleanBass, cleanMid, cleanTreble, brightFilter, presenceFilter;
    Biquad cabEvmLow, cabEvmMid, cabEvmLp1, cabEvmLp2;
    Biquad cab65Low, cab65Mid, cab65Lp1, cab65Lp2;
    OnePole snubberV1A, snubberV1B, snubberV2A, snubberV2B, snubberPower, odMidShaper;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCap1, gridCap2, gridCap3, gridCap4;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberDumbleODSLV2 {
    CyberDumbleODS* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* cleanVol;
    const float* cleanTreble;
    const float* cleanMid;
    const float* cleanBass;
    const float* bright;
    const float* midBoost;
    const float* rockJazz;
    const float* odDrive;
    const float* odLevel;
    const float* master;
    const float* presence;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberDumbleODSLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), cleanVol(nullptr), cleanTreble(nullptr), cleanMid(nullptr), cleanBass(nullptr),
        bright(nullptr), midBoost(nullptr), rockJazz(nullptr), odDrive(nullptr), odLevel(nullptr),
        master(nullptr), presence(nullptr), speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberDumbleODSLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberDumbleODSLV2* handle = new CyberDumbleODSLV2();
    handle->dsp = new CyberDumbleODS(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberDumbleODSLV2* h = (CyberDumbleODSLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_CHANNEL:      h->channel = (const float*)data; break;
        case PORT_CLEAN_VOL:    h->cleanVol = (const float*)data; break;
        case PORT_CLEAN_TREBLE: h->cleanTreble = (const float*)data; break;
        case PORT_CLEAN_MID:    h->cleanMid = (const float*)data; break;
        case PORT_CLEAN_BASS:   h->cleanBass = (const float*)data; break;
        case PORT_BRIGHT:       h->bright = (const float*)data; break;
        case PORT_MID_BOOST:    h->midBoost = (const float*)data; break;
        case PORT_ROCK_JAZZ:    h->rockJazz = (const float*)data; break;
        case PORT_OD_DRIVE:     h->odDrive = (const float*)data; break;
        case PORT_OD_LEVEL:     h->odLevel = (const float*)data; break;
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
    CyberDumbleODSLV2* h = (CyberDumbleODSLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 0.0f,
        h->cleanVol ? *h->cleanVol : 5.0f,
        h->cleanTreble ? *h->cleanTreble : 5.5f,
        h->cleanMid ? *h->cleanMid : 5.0f,
        h->cleanBass ? *h->cleanBass : 5.0f,
        h->bright ? *h->bright : 0.0f,
        h->midBoost ? *h->midBoost : 0.0f,
        h->rockJazz ? *h->rockJazz : 0.0f,
        h->odDrive ? *h->odDrive : 6.0f,
        h->odLevel ? *h->odLevel : 5.0f,
        h->master ? *h->master : 5.0f,
        h->presence ? *h->presence : 4.5f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberDumbleODSLV2* h = (CyberDumbleODSLV2*)instance; if (h) delete h; }
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
