/*
 * Cyber Vox AC30 Top Boost (1963) - 30W Class A British Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the 1963 Vox AC30 Top Boost,
 * featuring Top Boost & Normal channels with input jumpering,
 * interactive Treble/Bass stack, passive Cut control, 4x EL84 cathode-biased power section,
 * 2x12 Celestion Alnico Blue Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-vox-ac30tb"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_TB_VOL         = 3,
    PORT_TB_TREBLE      = 4,
    PORT_TB_BASS        = 5,
    PORT_NORM_VOL       = 6,
    PORT_CHANNEL_LINK   = 7,
    PORT_CUT            = 8,
    PORT_SPEAKER_CAB    = 9,
    PORT_SPEAKER_DRIVE  = 10,
    PORT_NOISE_GATE     = 11,
    PORT_OUTPUT_LEVEL   = 12,
    PORT_COUNT          = 12 + 1
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

class CyberVoxAC30TB {
public:
    CyberVoxAC30TB(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();
        cutFilter.setIdentity();
        topBoostPeak.setIdentity();

        cabBlueLow.setIdentity();
        cabBlueMid.setIdentity();
        cabBlueLp1.setIdentity();
        cabBlueLp2.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp1.setIdentity();
        cabGbLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapNorm = gridCapTb = gridCapTb2 = 0.0f;

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
        // 2x12 Celestion Alnico Blue (Famous bell chime, 75Hz bass resonance, 3.4kHz sparkling chime, 5.4kHz cutoff)
        cabBlueLow.setPeaking(75.0f, 4.8f, 1.1f, sampleRate);
        cabBlueMid.setPeaking(3400.0f, 4.4f, 1.5f, sampleRate);
        cabBlueLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabBlueLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 2x12 Celestion Greenback (Woody chewy mids, 80Hz punch, 5.1kHz cutoff)
        cabGbLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabGbMid.setPeaking(2600.0f, 4.0f, 1.3f, sampleRate);
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

    // 4x EL84 Cathode-Biased Hot Class A Power Stage (Immediate bloom, rich compression)
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
            s = cabBlueLow.process(s);
            s = cabBlueMid.process(s);
            s = cabBlueLp1.process(s);
            s = cabBlueLp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float treb, float cut) {
        // Vox Top Boost interactive tone stack
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(110.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        // Top Boost signature 2.8kHz resonant chime
        topBoostPeak.setPeaking(2850.0f, 3.5f, 1.4f, sampleRate);

        // Passive Cut control in phase inverter (0 = open, 10 = low-pass down to 2.8kHz)
        float cutFc = 9200.0f - (cut / 10.0f) * 6400.0f;
        cutFilter.setLowPass(cutFc, 0.707f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pTbVol, float pTbTreb, float pTbBass,
        float pNormVol, float pLink, float pCut,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pTbBass, pTbTreb, pCut);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isLinked = (pLink > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float tbGain = audioTaper(pTbVol, 3.8f);
        float normGain = audioTaper(pNormVol, 3.2f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            float sTb = 0.0f;
            float sNorm = 0.0f;

            if (isLinked) {
                // Top Boost Channel
                sTb = triode12AX7(s, gridCapTb, 1.6f, 0.08f);
                sTb = snubberTb.lp(sTb, 12000.0f, sampleRate);
                sTb = toneBass.process(sTb);
                sTb = toneTreble.process(sTb);
                sTb = topBoostPeak.process(sTb);
                sTb = triode12AX7(sTb, gridCapTb2, 1.5f, 0.08f);
                sTb *= tbGain;

                // Normal Channel
                sNorm = triode12AX7(s, gridCapNorm, 1.4f, 0.08f);
                sNorm = snubberNorm.lp(sNorm, 11000.0f, sampleRate);
                sNorm *= normGain;

                s = (sTb + sNorm) * 0.7f;
            } else {
                if (tbGain > 0.01f) {
                    sTb = triode12AX7(s, gridCapTb, 1.6f, 0.08f);
                    sTb = snubberTb.lp(sTb, 12000.0f, sampleRate);
                    sTb = toneBass.process(sTb);
                    sTb = toneTreble.process(sTb);
                    sTb = topBoostPeak.process(sTb);
                    sTb = triode12AX7(sTb, gridCapTb2, 1.5f, 0.08f);
                    sTb *= tbGain;
                }
                if (normGain > 0.01f) {
                    sNorm = triode12AX7(s, gridCapNorm, 1.4f, 0.08f);
                    sNorm = snubberNorm.lp(sNorm, 11000.0f, sampleRate);
                    sNorm *= normGain;
                }
                s = sTb + sNorm;
            }

            // Phase Inverter Cut Control
            s = cutFilter.process(s);

            // 4x EL84 Cathode-Biased Class A Power Section
            float push = s * 3.2f;
            float pull = -s * 3.2f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // Hot cathode bias power sag
            float currentDraw = fabsf(pOut) * 0.040f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.66f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 2x12 Celestion Alnico Blue Speaker Simulation
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
    Biquad toneBass, toneTreble, cutFilter, topBoostPeak;
    Biquad cabBlueLow, cabBlueMid, cabBlueLp1, cabBlueLp2;
    Biquad cabGbLow, cabGbMid, cabGbLp1, cabGbLp2;
    OnePole snubberTb, snubberNorm, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost;

    float sagVoltage;
    float gridCapNorm, gridCapTb, gridCapTb2;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberVoxAC30TBLV2 {
    CyberVoxAC30TB* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* tbVol;
    const float* tbTreble;
    const float* tbBass;
    const float* normVol;
    const float* channelLink;
    const float* cut;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberVoxAC30TBLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), tbVol(nullptr), tbTreble(nullptr), tbBass(nullptr),
        normVol(nullptr), channelLink(nullptr), cut(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberVoxAC30TBLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberVoxAC30TBLV2* handle = new CyberVoxAC30TBLV2();
    handle->dsp = new CyberVoxAC30TB(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberVoxAC30TBLV2* h = (CyberVoxAC30TBLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_TB_VOL:         h->tbVol = (const float*)data; break;
        case PORT_TB_TREBLE:      h->tbTreble = (const float*)data; break;
        case PORT_TB_BASS:        h->tbBass = (const float*)data; break;
        case PORT_NORM_VOL:       h->normVol = (const float*)data; break;
        case PORT_CHANNEL_LINK:   h->channelLink = (const float*)data; break;
        case PORT_CUT:            h->cut = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberVoxAC30TBLV2* h = (CyberVoxAC30TBLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->tbVol ? *h->tbVol : 6.0f,
        h->tbTreble ? *h->tbTreble : 5.5f,
        h->tbBass ? *h->tbBass : 5.0f,
        h->normVol ? *h->normVol : 5.0f,
        h->channelLink ? *h->channelLink : 1.0f,
        h->cut ? *h->cut : 3.5f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.0f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberVoxAC30TBLV2* h = (CyberVoxAC30TBLV2*)instance; if (h) delete h; }
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
