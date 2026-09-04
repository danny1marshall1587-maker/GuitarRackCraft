/*
 * Cyber Two-Rock Classic Reverb Signature - Boutique 100W Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Two-Rock's flagship Classic Reverb Signature,
 * featuring 3-position Gain Structure (Traditional, Two-Rock, Schofield),
 * active Contour filter, Bright/Mid/Deep switches, 6L6GC power stage,
 * Two-Rock 2x12 TR-1265B Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-tworock-crs"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_GAIN           = 3,
    PORT_TREBLE         = 4,
    PORT_MIDDLE         = 5,
    PORT_BASS           = 6,
    PORT_CONTOUR        = 7,
    PORT_BRIGHT         = 8,
    PORT_MID_BOOST      = 9,
    PORT_DEEP           = 10,
    PORT_GAIN_STRUCT    = 11,
    PORT_MASTER         = 12,
    PORT_PRESENCE       = 13,
    PORT_SPEAKER_CAB    = 14,
    PORT_SPEAKER_DRIVE  = 15,
    PORT_NOISE_GATE     = 16,
    PORT_OUTPUT_LEVEL   = 17,
    PORT_COUNT          = 17 + 1
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

class CyberTwoRockCRS {
public:
    CyberTwoRockCRS(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        brightFilter.setIdentity();
        contourLow.setIdentity();
        contourHigh.setIdentity();
        presenceFilter.setIdentity();

        cabTrLow.setIdentity();
        cabTrMid.setIdentity();
        cabTrLp1.setIdentity();
        cabTrLp2.setIdentity();

        cabEvLow.setIdentity();
        cabEvMid.setIdentity();
        cabEvLp1.setIdentity();
        cabEvLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCap1 = gridCap2 = 0.0f;

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
        // Two-Rock 2x12 TR-1265B Oval Open-Back (Lush 3D dispersion, 75Hz low bloom, silky 3.2kHz top)
        cabTrLow.setPeaking(76.0f, 5.2f, 1.1f, sampleRate);
        cabTrMid.setPeaking(2600.0f, 3.6f, 1.4f, sampleRate);
        cabTrLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabTrLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 1x12 EVM-12L Oval Open-Back (Hyper-linear, massive transient punch, transparent top)
        cabEvLow.setPeaking(80.0f, 4.5f, 1.2f, sampleRate);
        cabEvMid.setPeaking(2900.0f, 3.8f, 1.5f, sampleRate);
        cabEvLp1.setLowPass(5600.0f, 0.707f, sampleRate);
        cabEvLp2.setLowPass(5600.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.005f;
        } else {
            gridBias *= 0.9998f;
        }
        float effIn = (in - gridBias * 0.18f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 2x 6L6GC High-Headroom Power Stage
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

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.3f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.25f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.2f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.035f * (t * t)) / (1.0f + driveAmount * 0.25f);

            float dampingFc = 6600.0f - driveAmount * 1500.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.25f);
        }

        if (cabType == 1) {
            s = cabEvLow.process(s);
            s = cabEvMid.process(s);
            s = cabEvLp1.process(s);
            s = cabEvLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabTrLow.process(s);
            s = cabTrMid.process(s);
            s = cabTrLp1.process(s);
            s = cabTrLp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(
        float gain, float treb, float mid, float bass, float contour,
        float bright, float midBoost, float deep, float pres
    ) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        if (deep > 0.5f) bassDb += 3.8f;
        toneBass.setLowShelf(110.0f, bassDb, 0.7f, sampleRate);

        float midDb = (mid - 5.0f) * 2.5f;
        if (midBoost > 0.5f) midDb += 4.5f;
        toneMid.setPeaking(680.0f, midDb, 1.2f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.5f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        if (bright > 0.5f) {
            float atten = std::max(0.0f, (10.0f - gain) / 10.0f);
            brightFilter.setHighShelf(4000.0f, atten * 6.5f, 0.7f, sampleRate);
        } else {
            brightFilter.setIdentity();
        }

        // Two-Rock Active Contour Control
        // Center (5.0) is flat; < 5 boosts lows and tames highs; > 5 scoops lower mids and adds open air
        float contDelta = (contour - 5.0f) / 5.0f; // -1.0 to +1.0
        contourLow.setLowShelf(120.0f, -contDelta * 3.5f, 0.7f, sampleRate);
        contourHigh.setHighShelf(4500.0f, contDelta * 4.0f, 0.7f, sampleRate);

        float presDb = (pres / 10.0f) * 7.5f;
        presenceFilter.setHighShelf(4200.0f, presDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pGain, float pTreble, float pMiddle, float pBass,
        float pContour, float pBright, float pMidBoost, float pDeep,
        float pGainStruct, float pMaster, float pPresence,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pGain, pTreble, pMiddle, pBass, pContour, pBright, pMidBoost, pDeep, pPresence);

        int structMode = (int)std::max(0.0f, std::min(2.0f, pGainStruct));
        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        // Gain structure multiplier: Traditional (1.0), Two-Rock (1.3), Schofield (1.8)
        float structMult = (structMode == 2) ? 1.8f : ((structMode == 1) ? 1.3f : 1.0f);
        float preGain = audioTaper(pGain, 2.8f * structMult);
        float masterGain = audioTaper(pMaster, 3.4f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            // V1A First Preamp Stage (Pristine 3D clarity)
            s = triode12AX7(s, gridCap1, 1.5f, 0.06f);
            s = snubberV1A.lp(s, 12000.0f, sampleRate);
            s = brightFilter.process(s);
            s *= preGain;

            // Two-Rock 3-Band Tone Stack
            s = toneBass.process(s);
            s = toneMid.process(s);
            s = toneTreble.process(s);

            // V1B Second Preamp Stage
            s = triode12AX7(s, gridCap2, 1.4f * structMult, 0.08f);
            s = snubberV1B.lp(s, 11000.0f, sampleRate);

            // Active Contour Shaping
            s = contourLow.process(s);
            s = contourHigh.process(s);

            // Phase Inverter & 6L6GC Power Stage
            float push = s * masterGain;
            float pull = -s * masterGain * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 12000.0f, sampleRate);

            float currentDraw = fabsf(pOut) * 0.030f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0016f;
            sagVoltage = std::max(0.74f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 18.0f, sampleRate);

            // Dynamic Two-Rock Speaker Cabinet Simulation
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
    Biquad toneBass, toneMid, toneTreble, brightFilter, presenceFilter;
    Biquad contourLow, contourHigh;
    Biquad cabTrLow, cabTrMid, cabTrLp1, cabTrLp2;
    Biquad cabEvLow, cabEvMid, cabEvLp1, cabEvLp2;
    OnePole snubberV1A, snubberV1B, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCap1, gridCap2;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberTwoRockCRSLV2 {
    CyberTwoRockCRS* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* gain;
    const float* treble;
    const float* middle;
    const float* bass;
    const float* contour;
    const float* bright;
    const float* midBoost;
    const float* deep;
    const float* gainStruct;
    const float* master;
    const float* presence;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberTwoRockCRSLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), gain(nullptr), treble(nullptr), middle(nullptr), bass(nullptr),
        contour(nullptr), bright(nullptr), midBoost(nullptr), deep(nullptr), gainStruct(nullptr),
        master(nullptr), presence(nullptr), speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberTwoRockCRSLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberTwoRockCRSLV2* handle = new CyberTwoRockCRSLV2();
    handle->dsp = new CyberTwoRockCRS(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberTwoRockCRSLV2* h = (CyberTwoRockCRSLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_GAIN:         h->gain = (const float*)data; break;
        case PORT_TREBLE:       h->treble = (const float*)data; break;
        case PORT_MIDDLE:       h->middle = (const float*)data; break;
        case PORT_BASS:         h->bass = (const float*)data; break;
        case PORT_CONTOUR:      h->contour = (const float*)data; break;
        case PORT_BRIGHT:       h->bright = (const float*)data; break;
        case PORT_MID_BOOST:    h->midBoost = (const float*)data; break;
        case PORT_DEEP:         h->deep = (const float*)data; break;
        case PORT_GAIN_STRUCT:  h->gainStruct = (const float*)data; break;
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
    CyberTwoRockCRSLV2* h = (CyberTwoRockCRSLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->gain ? *h->gain : 5.0f,
        h->treble ? *h->treble : 5.5f,
        h->middle ? *h->middle : 5.0f,
        h->bass ? *h->bass : 5.0f,
        h->contour ? *h->contour : 5.0f,
        h->bright ? *h->bright : 0.0f,
        h->midBoost ? *h->midBoost : 0.0f,
        h->deep ? *h->deep : 0.0f,
        h->gainStruct ? *h->gainStruct : 1.0f,
        h->master ? *h->master : 5.5f,
        h->presence ? *h->presence : 5.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberTwoRockCRSLV2* h = (CyberTwoRockCRSLV2*)instance; if (h) delete h; }
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
