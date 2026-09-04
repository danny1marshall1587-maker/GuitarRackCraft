/*
 * Cyber Vox AC30 PA (1964) - 4-Channel British PA Head for Guitar LV2 Plugin
 * Authentic analog circuit emulation of the 1964 Vox AC30 PA amplifier head,
 * featuring 4 individual pre-amp volume channels with parallel jumpering,
 * master tilt tone, passive Cut control, 4x EL84 cathode-biased Class A power section,
 * 2x12 Column Alnico Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-vox-ac30pa"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CH1_VOL        = 3,
    PORT_CH2_VOL        = 4,
    PORT_CH3_VOL        = 5,
    PORT_CH4_VOL        = 6,
    PORT_MASTER_LINK    = 7,
    PORT_TONE           = 8,
    PORT_CUT            = 9,
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

class CyberVoxAC30PA {
public:
    CyberVoxAC30PA(double rate) : sampleRate(rate) {
        toneTiltLow.setIdentity();
        toneTiltHigh.setIdentity();
        cutFilter.setIdentity();

        ch1BrightFilter.setIdentity();
        ch3WarmFilter.setIdentity();
        ch4GainFilter.setIdentity();

        cabColLow.setIdentity();
        cabColMid.setIdentity();
        cabColLp1.setIdentity();
        cabColLp2.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp1.setIdentity();
        cabGbLp2.setIdentity();

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
initFilters();
    }

    void initFilters() {
        // Channel voicings
        ch1BrightFilter.setHighShelf(3200.0f, 6.0f, 0.7f, sampleRate);
        ch3WarmFilter.setLowShelf(150.0f, 5.0f, 0.7f, sampleRate);
        ch4GainFilter.setPeaking(2200.0f, 4.5f, 1.3f, sampleRate);

        // 2x12 Column Alnico (Bright open projection, 85Hz bass, 3.2kHz forward chime, 5.5kHz cutoff)
        cabColLow.setPeaking(85.0f, 4.4f, 1.1f, sampleRate);
        cabColMid.setPeaking(3200.0f, 4.8f, 1.4f, sampleRate);
        cabColLp1.setLowPass(5500.0f, 0.707f, sampleRate);
        cabColLp2.setLowPass(5500.0f, 0.707f, sampleRate);

        // 4x12 Greenback
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

    // 4x EL84 Cathode-Biased Class A Power Section
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
            s = cabColLow.process(s);
            s = cabColMid.process(s);
            s = cabColLp1.process(s);
            s = cabColLp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float tone, float cut) {
        // Master Tilt Tone Control
        float tiltNorm = (tone - 5.0f) / 5.0f;
        float bassGain = -tiltNorm * 4.5f;
        float trebGain = tiltNorm * 4.5f;
        toneTiltLow.setLowShelf(250.0f, bassGain, 0.7f, sampleRate);
        toneTiltHigh.setHighShelf(2400.0f, trebGain, 0.7f, sampleRate);

        // Passive Cut in Phase Inverter
        float cutFc = 9200.0f - (cut / 10.0f) * 6400.0f;
        cutFilter.setLowPass(cutFc, 0.707f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pCh1Vol, float pCh2Vol, float pCh3Vol, float pCh4Vol,
        float pMasterLink, float pTone, float pCut,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pTone, pCut);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isLinked = (pMasterLink > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float g1 = audioTaper(pCh1Vol, 3.8f);
        float g2 = audioTaper(pCh2Vol, 3.4f);
        float g3 = audioTaper(pCh3Vol, 3.4f);
        float g4 = audioTaper(pCh4Vol, 4.4f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            float mix = 0.0f;

            if (isLinked) {
                // All 4 channels driven in parallel
                float c1 = triode12AX7(s, gridCap1, 1.55f, 0.08f);
                c1 = ch1BrightFilter.process(c1) * g1;

                float c2 = triode12AX7(s, gridCap2, 1.50f, 0.08f) * g2;

                float c3 = triode12AX7(s, gridCap3, 1.50f, 0.08f);
                c3 = ch3WarmFilter.process(c3) * g3;

                float c4 = triode12AX7(s, gridCap4, 1.65f, 0.09f);
                c4 = ch4GainFilter.process(c4) * g4;

                mix = (c1 + c2 + c3 + c4) * 0.55f;
            } else {
                if (g1 > 0.01f) {
                    float c1 = triode12AX7(s, gridCap1, 1.55f, 0.08f);
                    mix += ch1BrightFilter.process(c1) * g1;
                }
                if (g2 > 0.01f) {
                    mix += triode12AX7(s, gridCap2, 1.50f, 0.08f) * g2;
                }
                if (g3 > 0.01f) {
                    float c3 = triode12AX7(s, gridCap3, 1.50f, 0.08f);
                    mix += ch3WarmFilter.process(c3) * g3;
                }
                if (g4 > 0.01f) {
                    float c4 = triode12AX7(s, gridCap4, 1.65f, 0.09f);
                    mix += ch4GainFilter.process(c4) * g4;
                }
            }

            // Master Tilt Tone Control
            mix = toneTiltLow.process(mix);
            mix = toneTiltHigh.process(mix);

            // Phase Inverter Cut Control
            mix = cutFilter.process(mix);

            // 4x EL84 Class A Power Section
            float push = mix * 3.2f;
            float pull = -mix * 3.2f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // Power Sag
            float currentDraw = fabsf(pOut) * 0.040f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.66f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 2x12 Column Alnico Speaker Simulation
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
    Biquad toneTiltLow, toneTiltHigh, cutFilter;
    Biquad ch1BrightFilter, ch3WarmFilter, ch4GainFilter;
    Biquad cabColLow, cabColMid, cabColLp1, cabColLp2;
    Biquad cabGbLow, cabGbMid, cabGbLp1, cabGbLp2;
    OnePole snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCap1, gridCap2, gridCap3, gridCap4;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberVoxAC30PALV2 {
    CyberVoxAC30PA* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* ch1Vol;
    const float* ch2Vol;
    const float* ch3Vol;
    const float* ch4Vol;
    const float* masterLink;
    const float* tone;
    const float* cut;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberVoxAC30PALV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), ch1Vol(nullptr), ch2Vol(nullptr), ch3Vol(nullptr),
        ch4Vol(nullptr), masterLink(nullptr), tone(nullptr), cut(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberVoxAC30PALV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberVoxAC30PALV2* handle = new CyberVoxAC30PALV2();
    handle->dsp = new CyberVoxAC30PA(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberVoxAC30PALV2* h = (CyberVoxAC30PALV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_CH1_VOL:        h->ch1Vol = (const float*)data; break;
        case PORT_CH2_VOL:        h->ch2Vol = (const float*)data; break;
        case PORT_CH3_VOL:        h->ch3Vol = (const float*)data; break;
        case PORT_CH4_VOL:        h->ch4Vol = (const float*)data; break;
        case PORT_MASTER_LINK:    h->masterLink = (const float*)data; break;
        case PORT_TONE:           h->tone = (const float*)data; break;
        case PORT_CUT:            h->cut = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberVoxAC30PALV2* h = (CyberVoxAC30PALV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->ch1Vol ? *h->ch1Vol : 6.0f,
        h->ch2Vol ? *h->ch2Vol : 5.0f,
        h->ch3Vol ? *h->ch3Vol : 4.0f,
        h->ch4Vol ? *h->ch4Vol : 5.5f,
        h->masterLink ? *h->masterLink : 1.0f,
        h->tone ? *h->tone : 5.0f,
        h->cut ? *h->cut : 3.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.0f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberVoxAC30PALV2* h = (CyberVoxAC30PALV2*)instance; if (h) delete h; }
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
