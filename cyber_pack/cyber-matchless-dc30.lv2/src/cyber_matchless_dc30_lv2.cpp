/*
 * Cyber Matchless DC-30 - Boutique Class A Dual-Channel Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Mark Sampson's legendary Matchless DC-30,
 * featuring 12AX7 Ch1, EF86 pentode Ch2 with 6-position tone switch,
 * passive Cut control, 4x EL84 cathode-biased power section,
 * mismatched G12M/G12H Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-matchless-dc30"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_CH1_VOL        = 4,
    PORT_CH1_BASS       = 5,
    PORT_CH1_TREBLE     = 6,
    PORT_CH2_VOL        = 7,
    PORT_CH2_TONE       = 8,
    PORT_CUT            = 9,
    PORT_MASTER         = 10,
    PORT_SPEAKER_CAB    = 11,
    PORT_SPEAKER_DRIVE  = 12,
    PORT_NOISE_GATE     = 13,
    PORT_OUTPUT_LEVEL   = 14,
    PORT_COUNT          = 14 + 1
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

class CyberMatchlessDC30 {
public:
    CyberMatchlessDC30(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();
        cutFilter.setIdentity();
        ef86ToneFilter.setIdentity();

        cabMisLow.setIdentity();
        cabMisMid.setIdentity();
        cabMisLp1.setIdentity();
        cabMisLp2.setIdentity();

        cabAlnLow.setIdentity();
        cabAlnMid.setIdentity();
        cabAlnLp1.setIdentity();
        cabAlnLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCap1A = gridCap1B = gridCapEf86 = 0.0f;

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
        // Matchless Mismatched 2x12 (1x Celestion G12M Greenback + 1x G12H-30: woody chewy breakup + punchy bass, 2.8kHz chime)
        cabMisLow.setPeaking(82.0f, 5.0f, 1.2f, sampleRate);
        cabMisMid.setPeaking(2800.0f, 4.2f, 1.4f, sampleRate);
        cabMisLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabMisLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 2x12 Celestion Alnico Silver (Deep Vox bell chime, 3.4kHz sparkling top)
        cabAlnLow.setPeaking(76.0f, 4.5f, 1.1f, sampleRate);
        cabAlnMid.setPeaking(3400.0f, 4.0f, 1.5f, sampleRate);
        cabAlnLp1.setLowPass(5500.0f, 0.707f, sampleRate);
        cabAlnLp2.setLowPass(5500.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.006f;
        } else {
            gridBias *= 0.9998f;
        }
        float effIn = (in - gridBias * 0.20f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // EF86 Audio Pentode Stage (Higher gain, sharp compression, rich 3rd harmonics)
    inline float pentodeEF86(float in, float &gridBias, float gainFactor) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.008f;
        } else {
            gridBias *= 0.9996f;
        }
        float effIn = (in - gridBias * 0.25f) * gainFactor;
        // Pentode transfer with sharper knee and asymmetrical saturation
        float x = effIn * 1.35f;
        float t = tanhf(x);
        return (t - 0.08f * (t * t) + 0.03f * (t * t * t)) * 0.85f;
    }

    // 4x EL84 Cathode-Biased Hot Class A Power Stage
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
            float coneOut = (t - 0.045f * (t * t)) / (1.0f + driveAmount * 0.30f);

            float dampingFc = 6400.0f - driveAmount * 1600.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.28f);
        }

        if (cabType == 1) {
            s = cabAlnLow.process(s);
            s = cabAlnMid.process(s);
            s = cabAlnLp1.process(s);
            s = cabAlnLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabMisLow.process(s);
            s = cabMisMid.process(s);
            s = cabMisLp1.process(s);
            s = cabMisLp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float treb, float cut, float ef86Tone) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(120.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        // Passive Cut control in phase inverter (0 is wide open, 10 rolls off top end down to 2.8kHz)
        float cutFc = 9000.0f - (cut / 10.0f) * 6200.0f;
        cutFilter.setLowPass(cutFc, 0.707f, sampleRate);

        // EF86 6-position rotary tone switch: shapes low-end coupling capacitors
        // Position 0 = fullest bass (33nF), Position 5 = extreme bass cut for tight saturated overdrive
        int tIdx = (int)std::max(0.0f, std::min(5.0f, ef86Tone));
        float hpFreqs[6] = { 45.0f, 80.0f, 130.0f, 210.0f, 320.0f, 480.0f };
        ef86ToneFilter.setLowShelf(hpFreqs[tIdx], -((float)tIdx * 2.5f), 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pCh1Vol, float pCh1Bass, float pCh1Treb,
        float pCh2Vol, float pCh2Tone, float pCut, float pMaster,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pCh1Bass, pCh1Treb, pCut, pCh2Tone);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isEf86 = (channel > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float ch1Gain = audioTaper(pCh1Vol, 2.6f);
        float ch2Gain = audioTaper(pCh2Vol, 3.8f);
        float masterGain = audioTaper(pMaster, 3.5f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            if (!isEf86) {
                // --- CHANNEL 1: 12AX7 CLASS A CHIME ---
                s = triode12AX7(s, gridCap1A, 1.85f, 0.08f);
                s = snubberV1A.lp(s, 12000.0f, sampleRate);
                s = couplingCap1A.hp(s, 18.0f, sampleRate);
                s *= ch1Gain;
                s = toneBass.process(s);
                s = toneTreble.process(s);
                s = couplingCapTone.hp(s, 18.0f, sampleRate);
                s = triode12AX7(s, gridCap1B, 1.75f, 0.08f);
            } else {
                // --- CHANNEL 2: EF86 PENTODE ROAR ---
                s = ef86ToneFilter.process(s);
                s *= ch2Gain;
                s = pentodeEF86(s, gridCapEf86, 2.4f);
                s = snubberEf86.lp(s, 12000.0f, sampleRate);
            }

            // Phase Inverter Cut Control
            s = cutFilter.process(s);
            s = couplingCapPi.hp(s, 18.0f, sampleRate);

            // 4x EL84 Cathode-Biased Power Stage
            float push = s * masterGain;
            float pull = -s * masterGain * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // Rich cathode bias power sag
            float currentDraw = fabsf(pOut) * 0.038f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0025f;
            sagVoltage = std::max(0.66f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic Matchless Mismatched Speaker Cabinet Simulation
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
    Biquad toneBass, toneTreble, cutFilter, ef86ToneFilter;
    Biquad cabMisLow, cabMisMid, cabMisLp1, cabMisLp2;
    Biquad cabAlnLow, cabAlnMid, cabAlnLp1, cabAlnLp2;
    OnePole snubberV1A, snubberEf86, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost, couplingCap1A, couplingCapTone, couplingCapPi;

    float sagVoltage;
    float gridCap1A, gridCap1B, gridCapEf86;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberMatchlessDC30LV2 {
    CyberMatchlessDC30* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* ch1Vol;
    const float* ch1Bass;
    const float* ch1Treble;
    const float* ch2Vol;
    const float* ch2Tone;
    const float* cut;
    const float* master;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberMatchlessDC30LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), ch1Vol(nullptr), ch1Bass(nullptr), ch1Treble(nullptr),
        ch2Vol(nullptr), ch2Tone(nullptr), cut(nullptr), master(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberMatchlessDC30LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberMatchlessDC30LV2* handle = new CyberMatchlessDC30LV2();
    handle->dsp = new CyberMatchlessDC30(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberMatchlessDC30LV2* h = (CyberMatchlessDC30LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_CHANNEL:      h->channel = (const float*)data; break;
        case PORT_CH1_VOL:      h->ch1Vol = (const float*)data; break;
        case PORT_CH1_BASS:     h->ch1Bass = (const float*)data; break;
        case PORT_CH1_TREBLE:   h->ch1Treble = (const float*)data; break;
        case PORT_CH2_VOL:      h->ch2Vol = (const float*)data; break;
        case PORT_CH2_TONE:     h->ch2Tone = (const float*)data; break;
        case PORT_CUT:          h->cut = (const float*)data; break;
        case PORT_MASTER:       h->master = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberMatchlessDC30LV2* h = (CyberMatchlessDC30LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 0.0f,
        h->ch1Vol ? *h->ch1Vol : 5.0f,
        h->ch1Bass ? *h->ch1Bass : 5.0f,
        h->ch1Treble ? *h->ch1Treble : 5.5f,
        h->ch2Vol ? *h->ch2Vol : 5.0f,
        h->ch2Tone ? *h->ch2Tone : 2.0f,
        h->cut ? *h->cut : 4.0f,
        h->master ? *h->master : 5.5f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.0f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberMatchlessDC30LV2* h = (CyberMatchlessDC30LV2*)instance; if (h) delete h; }
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
