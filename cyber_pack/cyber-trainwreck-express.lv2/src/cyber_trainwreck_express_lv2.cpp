/*
 * Cyber Trainwreck Express - Boutique Touch-Dynamic Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Ken Fischer's legendary Trainwreck Express,
 * featuring extreme dynamic touch responsiveness, 3-position bright switch,
 * Wild Harmonic Bloom mode, 2x EL34 power stage, Dynamic Speaker Stress,
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-trainwreck-express"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_VOLUME         = 3,
    PORT_TREBLE         = 4,
    PORT_MIDDLE         = 5,
    PORT_BASS           = 6,
    PORT_PRESENCE       = 7,
    PORT_BRIGHT         = 8,
    PORT_EDGE_MOD       = 9,
    PORT_SPEAKER_CAB    = 10,
    PORT_SPEAKER_DRIVE  = 11,
    PORT_NOISE_GATE     = 12,
    PORT_OUTPUT_LEVEL   = 13,
    PORT_COUNT          = 13 + 1
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

class CyberTrainwreckExpress {
public:
    CyberTrainwreckExpress(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        brightFilter.setIdentity();
        presenceFilter.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp1.setIdentity();
        cabGbLp2.setIdentity();

        cabAlnLow.setIdentity();
        cabAlnMid.setIdentity();
        cabAlnLp1.setIdentity();
        cabAlnLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCap1 = gridCap2 = gridCap3 = 0.0f;

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
        // 4x12 Vintage Celestion Greenbacks in Solid Pine Cab (Woody, chewy compression, 2.5kHz bite)
        cabGbLow.setLowShelf(90.0f, 5.2f, 0.7f, sampleRate);
        cabGbMid.setPeaking(2500.0f, 3.8f, 1.4f, sampleRate);
        cabGbLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabGbLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 2x12 Celestion Alnico Gold (Bell chime, deep 75Hz warmth, 3.2kHz sparkling top)
        cabAlnLow.setLowShelf(82.0f, 5.0f, 0.7f, sampleRate);
        cabAlnMid.setPeaking(3200.0f, 4.2f, 1.5f, sampleRate);
        cabAlnLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabAlnLp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.007f;
        } else {
            gridBias *= 0.9997f;
        }
        float effIn = (in - gridBias * 0.24f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 2x EL34 35W Push-Pull Power Stage running on the edge of oscillation
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

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.5f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.35f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.35f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.05f * (t * t)) / (1.0f + driveAmount * 0.32f);

            float dampingFc = 6300.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.30f);
        }

        if (cabType == 1) {
            s = cabAlnLow.process(s);
            s = cabAlnMid.process(s);
            s = cabAlnLp1.process(s);
            s = cabAlnLp2.process(s);
            return s * 1.05f;
        } else {
            s = cabGbLow.process(s);
            s = cabGbMid.process(s);
            s = cabGbLp1.process(s);
            s = cabGbLp2.process(s);
            return s * 1.0f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 1.85f);
    }

    void updateParams(float vol, float treb, float mid, float bass, float pres, float bright) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(135.0f, bassDb, 0.7f, sampleRate);

        float midDb = (mid - 5.0f) * 2.6f;
        toneMid.setPeaking(700.0f, midDb, 1.2f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3100.0f, trebDb, 0.7f, sampleRate);

        // 3-way Bright switch (0 = Off, 1 = Subtle 100pF chime, 2 = Aggressive 500pF sparkle)
        int bMode = (int)std::max(0.0f, std::min(2.0f, bright));
        if (bMode == 1) {
            float atten = std::max(0.0f, (10.0f - vol) / 10.0f);
            brightFilter.setHighShelf(4500.0f, atten * 5.0f, 0.7f, sampleRate);
        } else if (bMode == 2) {
            float atten = std::max(0.0f, (10.0f - vol) / 10.0f);
            brightFilter.setHighShelf(3600.0f, atten * 8.5f, 0.7f, sampleRate);
        } else {
            brightFilter.setIdentity();
        }

        float presDb = (pres / 10.0f) * 8.0f;
        presenceFilter.setHighShelf(4000.0f, presDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pVolume, float pTreble, float pMiddle, float pBass,
        float pPresence, float pBright, float pEdgeMod,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pVolume, pTreble, pMiddle, pBass, pPresence, pBright);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isWild = (pEdgeMod > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float driveGain = audioTaper(pVolume, isWild ? 5.8f : 4.6f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // Stage 1 (V1A)
            s = triode12AX7(s, gridCap1, 1.8f, 0.08f);
            s = snubberV1A.lp(s, 11500.0f, sampleRate);
            s = brightFilter.process(s);
            s *= driveGain;
            s = couplingCap1.hp(s, 18.0f, sampleRate);

            // Stage 2 (V1B)
            s = triode12AX7(s, gridCap2, 2.1f, 0.12f);
            s = snubberV1B.lp(s, 9500.0f, sampleRate);
            s = couplingCap2.hp(s, 18.0f, sampleRate);

            // Tone Stack
            s = toneBass.process(s);
            s = toneMid.process(s);
            s = toneTreble.process(s);
            s = couplingCapTone.hp(s, 18.0f, sampleRate);

            // Stage 3 (V2A)
            s = triode12AX7(s, gridCap3, isWild ? 2.6f : 2.2f, isWild ? 0.16f : 0.12f);
            s = snubberV2A.lp(s, 12000.0f, sampleRate);
            s = couplingCap3.hp(s, 18.0f, sampleRate);

            // Phase Inverter & Power Stage
            float push = s * 2.8f;
            float pull = -s * 2.8f * 0.98f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            float currentDraw = fabsf(pOut) * 0.042f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0022f;
            sagVoltage = std::max(0.68f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

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
    Biquad cabGbLow, cabGbMid, cabGbLp1, cabGbLp2;
    Biquad cabAlnLow, cabAlnMid, cabAlnLp1, cabAlnLp2;
    OnePole snubberV1A, snubberV1B, snubberV2A, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost, couplingCap1, couplingCap2, couplingCapTone, couplingCap3;

    float sagVoltage;
    float gridCap1, gridCap2, gridCap3;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberTrainwreckExpressLV2 {
    CyberTrainwreckExpress* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* volume;
    const float* treble;
    const float* middle;
    const float* bass;
    const float* presence;
    const float* bright;
    const float* edgeMod;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberTrainwreckExpressLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), volume(nullptr), treble(nullptr), middle(nullptr), bass(nullptr),
        presence(nullptr), bright(nullptr), edgeMod(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberTrainwreckExpressLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberTrainwreckExpressLV2* handle = new CyberTrainwreckExpressLV2();
    handle->dsp = new CyberTrainwreckExpress(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberTrainwreckExpressLV2* h = (CyberTrainwreckExpressLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_VOLUME:       h->volume = (const float*)data; break;
        case PORT_TREBLE:       h->treble = (const float*)data; break;
        case PORT_MIDDLE:       h->middle = (const float*)data; break;
        case PORT_BASS:         h->bass = (const float*)data; break;
        case PORT_PRESENCE:     h->presence = (const float*)data; break;
        case PORT_BRIGHT:       h->bright = (const float*)data; break;
        case PORT_EDGE_MOD:     h->edgeMod = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberTrainwreckExpressLV2* h = (CyberTrainwreckExpressLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->volume ? *h->volume : 5.0f,
        h->treble ? *h->treble : 6.0f,
        h->middle ? *h->middle : 5.0f,
        h->bass ? *h->bass : 4.5f,
        h->presence ? *h->presence : 5.5f,
        h->bright ? *h->bright : 1.0f,
        h->edgeMod ? *h->edgeMod : 0.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberTrainwreckExpressLV2* h = (CyberTrainwreckExpressLV2*)instance; if (h) delete h; }
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
