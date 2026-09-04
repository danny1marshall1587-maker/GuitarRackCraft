/*
 * Cyber Orange OR120 Pics Only (1972) - 120W British Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the 1972 Orange OR120 "Pics Only",
 * featuring 6-position F.A.C. rotary tone selector, Baxandall Bass & Treble,
 * H.F. Drive, 4x EL34 120W power section, Orange 4x12 Dynamic Speaker Stress,
 * and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-orange-or120"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_GAIN           = 3,
    PORT_FAC            = 4,
    PORT_BASS           = 5,
    PORT_TREBLE         = 6,
    PORT_HF_DRIVE       = 7,
    PORT_SPEAKER_CAB    = 8,
    PORT_SPEAKER_DRIVE  = 9,
    PORT_NOISE_GATE     = 10,
    PORT_OUTPUT_LEVEL   = 11,
    PORT_COUNT          = 11 + 1
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

class CyberOrangeOR120 {
public:
    CyberOrangeOR120(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();
        hfDriveFilter.setIdentity();

        cabOrLow.setIdentity();
        cabOrMid.setIdentity();
        cabOrLp1.setIdentity();
        cabOrLp2.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp1.setIdentity();
        cabGbLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapPre1 = gridCapPre2 = 0.0f;

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
        // Orange 4x12 (Massive 70Hz low wall of sound, aggressive 2.3kHz bark, 5.0kHz cutoff)
        cabOrLow.setPeaking(70.0f, 5.4f, 1.3f, sampleRate);
        cabOrMid.setPeaking(2300.0f, 4.4f, 1.2f, sampleRate);
        cabOrLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabOrLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 4x12 Greenbacks (Woody, chewy mids)
        cabGbLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabGbMid.setPeaking(2500.0f, 4.0f, 1.3f, sampleRate);
        cabGbLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabGbLp2.setLowPass(6800.0f, 0.707f, sampleRate);
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

    // 4x EL34 120W Colossal Power Stage (Massive headroom & raw saturated crunch)
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

            float coneStress = s * (1.0f + driveAmount * 1.4f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.045f * (t * t)) / (1.0f + driveAmount * 0.28f);

            float dampingFc = 6400.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.28f);
        }

        if (cabType == 1) {
            s = cabGbLow.process(s);
            s = cabGbMid.process(s);
            s = cabGbLp1.process(s);
            s = cabGbLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabOrLow.process(s);
            s = cabOrMid.process(s);
            s = cabOrLp1.process(s);
            s = cabOrLp2.process(s);
            return s * 1.06f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float treb, float hfDrive) {
        // Baxandall style active-acting passive EQ
        float bassDb = (bass - 5.0f) * 3.2f;
        toneBass.setLowShelf(110.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 3.0f;
        toneTreble.setHighShelf(3000.0f, trebDb, 0.7f, sampleRate);

        // H.F. Drive (Presence & high harmonic boost)
        float hfDb = (hfDrive / 10.0f) * 8.5f;
        hfDriveFilter.setHighShelf(3600.0f, hfDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pGain, float pFac, float pBass, float pTreb, float pHfDrive,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pBass, pTreb, pHfDrive);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        int facPos = (int)std::max(0.0f, std::min(5.0f, pFac));
        bool useGate = (pNoiseGate > 0.5f);

        // F.A.C. 6-position high-pass coupling cutoff:
        // Pos 0: 20 Hz (Massive sub-bass sludge)
        // Pos 1: 45 Hz
        // Pos 2: 90 Hz (Classic punchy rock)
        // Pos 3: 160 Hz
        // Pos 4: 280 Hz
        // Pos 5: 480 Hz (Tight razor-sharp bite)
        const float facCutoffs[6] = { 20.0f, 45.0f, 90.0f, 160.0f, 280.0f, 480.0f };
        float activeFacFc = facCutoffs[facPos];

        float preGain = audioTaper(pGain, 4.4f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            // F.A.C. Coupling High-Pass Filter
            s = facFilter.hp(s, activeFacFc, sampleRate);

            // V1 Preamp Stage
            s = triode12AX7(s, gridCapPre1, 1.55f, 0.08f);
            s = snubberPre.lp(s, 12000.0f, sampleRate);
            s *= preGain;

            // Baxandall Tone Stack
            s = toneBass.process(s);
            s = toneTreble.process(s);

            // V2 Driver Stage
            s = triode12AX7(s, gridCapPre2, 1.70f, 0.09f);

            // H.F. Drive
            s = hfDriveFilter.process(s);

            // Phase Inverter & 4x EL34 120W Power Stage
            float push = s * 3.4f;
            float pull = -s * 3.4f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // Huge solid-state rectifier sag (tight, punchy)
            float currentDraw = fabsf(pOut) * 0.032f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0035f;
            sagVoltage = std::max(0.70f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic Orange 4x12 Speaker Simulation
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
    Biquad toneBass, toneTreble, hfDriveFilter;
    Biquad cabOrLow, cabOrMid, cabOrLp1, cabOrLp2;
    Biquad cabGbLow, cabGbMid, cabGbLp1, cabGbLp2;
    OnePole facFilter;
    OnePole snubberPre, snubberPower;
    OnePole dcBlockerPost;

    float sagVoltage;
    float gridCapPre1, gridCapPre2;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberOrangeOR120LV2 {
    CyberOrangeOR120* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* gain;
    const float* fac;
    const float* bass;
    const float* treble;
    const float* hfDrive;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberOrangeOR120LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), gain(nullptr), fac(nullptr), bass(nullptr), treble(nullptr),
        hfDrive(nullptr), speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberOrangeOR120LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberOrangeOR120LV2* handle = new CyberOrangeOR120LV2();
    handle->dsp = new CyberOrangeOR120(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberOrangeOR120LV2* h = (CyberOrangeOR120LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_GAIN:           h->gain = (const float*)data; break;
        case PORT_FAC:            h->fac = (const float*)data; break;
        case PORT_BASS:           h->bass = (const float*)data; break;
        case PORT_TREBLE:         h->treble = (const float*)data; break;
        case PORT_HF_DRIVE:       h->hfDrive = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberOrangeOR120LV2* h = (CyberOrangeOR120LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->gain ? *h->gain : 6.5f,
        h->fac ? *h->fac : 2.0f,
        h->bass ? *h->bass : 5.0f,
        h->treble ? *h->treble : 5.5f,
        h->hfDrive ? *h->hfDrive : 5.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.0f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberOrangeOR120LV2* h = (CyberOrangeOR120LV2*)instance; if (h) delete h; }
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
