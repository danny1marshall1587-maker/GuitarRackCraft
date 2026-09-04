/*
 * Cyber Fender 5F6-A Tweed Bassman (1959) - Legendary Tweed Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the 1959 Fender 5F6-A Bassman,
 * featuring interactive Normal & Bright channels with input jumpering,
 * cathode-follower driven tone stack, tube rectifier sag,
 * 4x10 Jensen P10R Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-fender-bassman59"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_NORMAL_VOL     = 3,
    PORT_BRIGHT_VOL     = 4,
    PORT_CHANNEL_LINK   = 5,
    PORT_BASS           = 6,
    PORT_MIDDLE         = 7,
    PORT_TREBLE         = 8,
    PORT_PRESENCE       = 9,
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

class CyberFenderBassman59 {
public:
    CyberFenderBassman59(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        presenceFilter.setIdentity();
        brightFilter.setIdentity();

        cabP10Low.setIdentity();
        cabP10Mid.setIdentity();
        cabP10Lp1.setIdentity();
        cabP10Lp2.setIdentity();

        cabTwinLow.setIdentity();
        cabTwinMid.setIdentity();
        cabTwinLp1.setIdentity();
        cabTwinLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapNorm = gridCapBrill = gridCapCf = 0.0f;

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
        // 4x10 Jensen P10R Alnico in Solid Pine Tweed Cab (Bell chime, 90Hz tight thump, singing 3.2kHz presence)
        cabP10Low.setLowShelf(90.0f, 5.5f, 0.7f, sampleRate);
        cabP10Mid.setPeaking(3100.0f, 4.0f, 1.4f, sampleRate);
        cabP10Lp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabP10Lp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 2x12 Tweed Twin Jensen P12N (Deep 78Hz warmth, punchy 2.6kHz mid-bite)
        cabTwinLow.setLowShelf(85.0f, 5.2f, 0.7f, sampleRate);
        cabTwinMid.setPeaking(2600.0f, 3.8f, 1.3f, sampleRate);
        cabTwinLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabTwinLp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AY7(float in, float &gridBias, float gainFactor, float asym) {
        // 5F6-A used a 12AY7 in V1 for lower gain and warmer clean headroom
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.005f;
        } else {
            gridBias *= 0.9998f;
        }
        float effIn = (in - gridBias * 0.18f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 12AX7 Direct-Coupled Cathode Follower (compresses positive peaks, warms low mids)
    inline float cathodeFollower(float in, float &gridBias) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.009f;
        } else {
            gridBias *= 0.9994f;
        }
        float x = in - gridBias * 0.32f;
        float t = tanhf(x * 1.25f);
        return t - 0.12f * (t * t);
    }

    // 2x 5881 / 6L6WGB Tube Rectifier Power Stage (Warm sag and rich bloom)
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
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.32f);
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

        if (cabType == 1) {
            s = cabTwinLow.process(s);
            s = cabTwinMid.process(s);
            s = cabTwinLp1.process(s);
            s = cabTwinLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabP10Low.process(s);
            s = cabP10Mid.process(s);
            s = cabP10Lp1.process(s);
            s = cabP10Lp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float mid, float treb, float pres) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(125.0f, bassDb, 0.7f, sampleRate);

        float midDb = (mid - 5.0f) * 2.8f;
        toneMid.setPeaking(720.0f, midDb, 1.2f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        float presDb = (pres / 10.0f) * 8.0f;
        presenceFilter.setHighShelf(4000.0f, presDb, 0.7f, sampleRate);

        // Bright channel capacitor (100pF across volume pot)
        brightFilter.setHighShelf(3600.0f, 6.0f, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pNormVol, float pBrillVol, float pLink,
        float pBass, float pMid, float pTreble, float pPresence,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pBass, pMid, pTreble, pPresence);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isLinked = (pLink > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float normGain = audioTaper(pNormVol, 3.2f);
        float brillGain = audioTaper(pBrillVol, 3.5f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            // V1 12AY7 Preamp Stages
            float sNorm = 0.0f;
            float sBrill = 0.0f;

            if (isLinked) {
                sNorm = triode12AY7(s, gridCapNorm, 1.35f, 0.06f);
                sNorm = snubberNorm.lp(sNorm, 12000.0f, sampleRate);
                sNorm *= normGain;

                sBrill = triode12AY7(s, gridCapBrill, 1.45f, 0.06f);
                sBrill = snubberBrill.lp(sBrill, 12000.0f, sampleRate);
                sBrill = brightFilter.process(sBrill);
                sBrill *= brillGain;

                s = (sNorm + sBrill) * 0.7f;
            } else {
                if (normGain > 0.01f) {
                    sNorm = triode12AY7(s, gridCapNorm, 1.35f, 0.06f);
                    sNorm = snubberNorm.lp(sNorm, 12000.0f, sampleRate);
                    sNorm *= normGain;
                }
                if (brillGain > 0.01f) {
                    sBrill = triode12AY7(s, gridCapBrill, 1.45f, 0.06f);
                    sBrill = snubberBrill.lp(sBrill, 12000.0f, sampleRate);
                    sBrill = brightFilter.process(sBrill);
                    sBrill *= brillGain;
                }
                s = sNorm + sBrill;
            }

            s = couplingCapV1.hp(s, 18.0f, sampleRate);
            // V2 Cathode Follower Driver
            s = cathodeFollower(s, gridCapCf);
            s = couplingCapCf.hp(s, 16.0f, sampleRate);

            // 5F6-A Interactive Tone Stack
            s = toneBass.process(s);
            s = toneMid.process(s);
            s = toneTreble.process(s);
            s = couplingCapTone.hp(s, 18.0f, sampleRate);

            // Long-Tail Pair Phase Inverter & 5881 / 6L6WGB Power Stage
            float push = s * 3.2f;
            float pull = -s * 3.2f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // 5AR4 Tube Rectifier Dynamic Sag
            float currentDraw = fabsf(pOut) * 0.045f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.65f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 4x10 Jensen P10R Speaker Simulation
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
    Biquad toneBass, toneMid, toneTreble, presenceFilter, brightFilter;
    Biquad cabP10Low, cabP10Mid, cabP10Lp1, cabP10Lp2;
    Biquad cabTwinLow, cabTwinMid, cabTwinLp1, cabTwinLp2;
    OnePole snubberNorm, snubberBrill, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost, couplingCapV1, couplingCapCf, couplingCapTone;

    float sagVoltage;
    float gridCapNorm, gridCapBrill, gridCapCf;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberFenderBassman59LV2 {
    CyberFenderBassman59* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* normVol;
    const float* brillVol;
    const float* channelLink;
    const float* bass;
    const float* middle;
    const float* treble;
    const float* presence;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberFenderBassman59LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), normVol(nullptr), brillVol(nullptr), channelLink(nullptr),
        bass(nullptr), middle(nullptr), treble(nullptr), presence(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberFenderBassman59LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberFenderBassman59LV2* handle = new CyberFenderBassman59LV2();
    handle->dsp = new CyberFenderBassman59(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberFenderBassman59LV2* h = (CyberFenderBassman59LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_NORMAL_VOL:   h->normVol = (const float*)data; break;
        case PORT_BRIGHT_VOL:   h->brillVol = (const float*)data; break;
        case PORT_CHANNEL_LINK: h->channelLink = (const float*)data; break;
        case PORT_BASS:         h->bass = (const float*)data; break;
        case PORT_MIDDLE:       h->middle = (const float*)data; break;
        case PORT_TREBLE:       h->treble = (const float*)data; break;
        case PORT_PRESENCE:     h->presence = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberFenderBassman59LV2* h = (CyberFenderBassman59LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->normVol ? *h->normVol : 5.0f,
        h->brillVol ? *h->brillVol : 5.0f,
        h->channelLink ? *h->channelLink : 1.0f,
        h->bass ? *h->bass : 5.5f,
        h->middle ? *h->middle : 6.0f,
        h->treble ? *h->treble : 6.0f,
        h->presence ? *h->presence : 5.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 5.0f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberFenderBassman59LV2* h = (CyberFenderBassman59LV2*)instance; if (h) delete h; }
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
