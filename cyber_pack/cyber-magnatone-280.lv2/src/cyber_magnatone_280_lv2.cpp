/*
 * Cyber Magnatone 280 (1957) - True Pitch-Shifting Varistor Vibrato Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the 1957 Magnatone 280,
 * featuring Normal & Vibrato channels, true silicone-carbide Varistor pitch-shifting vibrato,
 * 6973 tube power stage with 5U4 rectifier sag, dual 12" Oxford speakers,
 * Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-magnatone-280"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_NORM_VOL       = 4,
    PORT_NORM_TREBLE    = 5,
    PORT_NORM_BASS      = 6,
    PORT_VIB_VOL        = 7,
    PORT_VIB_TREBLE     = 8,
    PORT_VIB_BASS       = 9,
    PORT_VIB_SPEED      = 10,
    PORT_VIB_DEPTH      = 11,
    PORT_SPEAKER_CAB    = 12,
    PORT_SPEAKER_DRIVE  = 13,
    PORT_NOISE_GATE     = 14,
    PORT_OUTPUT_LEVEL   = 15,
    PORT_COUNT          = 15 + 1
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

struct AllpassVaristor {
    float z = 0.0f;
    inline float process(float in, float coeff) {
        // First-order all-pass filter representing a varistor phase-shift stage
        float out = -coeff * in + z;
        z = in + coeff * out;
        return out;
    }
    void reset() { z = 0.0f; }
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

class CyberMagnatone280 {
public:
    CyberMagnatone280(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();

        cabMagLow.setIdentity();
        cabMagMid.setIdentity();
        cabMagLp1.setIdentity();
        cabMagLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapNorm = gridCapVib = 0.0f;
        vibPhase = 0.0f;

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
        // Dual 12" Oxford Magnatone (Warm vintage 80Hz resonant bloom, liquid 3.0kHz top, 5.2kHz cutoff)
        cabMagLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabMagMid.setPeaking(3000.0f, 4.2f, 1.4f, sampleRate);
        cabMagLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabMagLp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.006f;
        } else {
            gridBias *= 0.9997f;
        }
        float effIn = (in - gridBias * 0.20f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 6973 Tube Power Stage with 5U4 Rectifier Sag
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
        if (cabType == 1) return in;

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
            float coneOut = (t - 0.045f * (t * t)) / (1.0f + driveAmount * 0.28f);

            float dampingFc = 6400.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.28f);
        }

        s = cabMagLow.process(s);
        s = cabMagMid.process(s);
        s = cabMagLp1.process(s);
        s = cabMagLp2.process(s);
        return s * 1.05f;
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.22f * norm + 0.78f * norm * norm * norm) * (maxGain * 1.55f);
    }

    void updateParams(float bass, float treb) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(120.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pNormVol, float pNormTreb, float pNormBass,
        float pVibVol, float pVibTreb, float pVibBass, float pVibSpeed, float pVibDepth,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        bool isVibCh = (channel > 0.5f);
        float activeVol = isVibCh ? pVibVol : pNormVol;
        float activeTreb = isVibCh ? pVibTreb : pNormTreb;
        float activeBass = isVibCh ? pVibBass : pNormBass;

        updateParams(activeBass, activeTreb);

        int cabType = (int)std::max(0.0f, std::min(1.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        float preGain = audioTaper(activeVol, 3.2f);
        float depthNorm = (isVibCh && pVibDepth > 1.0f) ? (pVibDepth / 100.0f) : 0.0f;
        float lfoInc = (2.0f * (float)M_PI * pVibSpeed) / (float)sampleRate;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            // 12AX7 Preamp Stage
            s = triode12AX7(s, isVibCh ? gridCapVib : gridCapNorm, 1.55f, 0.08f);
            s = snubberPre.lp(s, 11500.0f, sampleRate);
            s *= preGain;

            // Tone Stack
            s = toneBass.process(s);
            s = toneTreble.process(s);

            // True Silicone-Carbide Varistor Pitch-Shift Phase Ladder (4-stage all-pass network)
            if (depthNorm > 0.01f) {
                float lfo = sinf(vibPhase);
                vibPhase += lfoInc;
                if (vibPhase > 2.0f * (float)M_PI) vibPhase -= 2.0f * (float)M_PI;

                // Center allpass coeff ~ 0.5, dynamically swept by Varistor impedance
                float coeff = 0.5f + 0.38f * lfo * depthNorm;
                coeff = std::max(0.05f, std::min(0.92f, coeff));

                float v1 = apVaristor1.process(s, coeff);
                float v2 = apVaristor2.process(v1, coeff);
                float v3 = apVaristor3.process(v2, coeff);
                float v4 = apVaristor4.process(v3, coeff);

                // Pure 100% pitch-modulated signal (not phase cancellation mix)
                s = v4;
            }

            // Phase Inverter & 6973 Power Stage
            float push = s * 3.2f;
            float pull = -s * 3.2f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // 5U4 Tube Rectifier Dynamic Sag
            float currentDraw = fabsf(pOut) * 0.042f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.65f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 2x12 Oxford Magnatone Speaker Simulation
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
    Biquad toneBass, toneTreble;
    Biquad cabMagLow, cabMagMid, cabMagLp1, cabMagLp2;
    AllpassVaristor apVaristor1, apVaristor2, apVaristor3, apVaristor4;
    OnePole snubberPre, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCapNorm, gridCapVib;
    float vibPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberMagnatone280LV2 {
    CyberMagnatone280* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* normVol;
    const float* normTreble;
    const float* normBass;
    const float* vibVol;
    const float* vibTreble;
    const float* vibBass;
    const float* vibSpeed;
    const float* vibDepth;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberMagnatone280LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), normVol(nullptr), normTreble(nullptr),
        normBass(nullptr), vibVol(nullptr), vibTreble(nullptr), vibBass(nullptr),
        vibSpeed(nullptr), vibDepth(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberMagnatone280LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberMagnatone280LV2* handle = new CyberMagnatone280LV2();
    handle->dsp = new CyberMagnatone280(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberMagnatone280LV2* h = (CyberMagnatone280LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_CHANNEL:        h->channel = (const float*)data; break;
        case PORT_NORM_VOL:       h->normVol = (const float*)data; break;
        case PORT_NORM_TREBLE:    h->normTreble = (const float*)data; break;
        case PORT_NORM_BASS:      h->normBass = (const float*)data; break;
        case PORT_VIB_VOL:        h->vibVol = (const float*)data; break;
        case PORT_VIB_TREBLE:     h->vibTreble = (const float*)data; break;
        case PORT_VIB_BASS:       h->vibBass = (const float*)data; break;
        case PORT_VIB_SPEED:      h->vibSpeed = (const float*)data; break;
        case PORT_VIB_DEPTH:      h->vibDepth = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberMagnatone280LV2* h = (CyberMagnatone280LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 1.0f,
        h->normVol ? *h->normVol : 5.0f,
        h->normTreble ? *h->normTreble : 5.5f,
        h->normBass ? *h->normBass : 5.0f,
        h->vibVol ? *h->vibVol : 5.5f,
        h->vibTreble ? *h->vibTreble : 5.5f,
        h->vibBass ? *h->vibBass : 5.0f,
        h->vibSpeed ? *h->vibSpeed : 4.5f,
        h->vibDepth ? *h->vibDepth : 75.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberMagnatone280LV2* h = (CyberMagnatone280LV2*)instance; if (h) delete h; }
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
