/*
 * Cyber Tone King Imperial - Boutique Dual-Channel 20W 6V6 Tube Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Mark Bartel's acclaimed Tone King Imperial,
 * featuring Blackface Rhythm channel with Mid-Bite control, Tweed Lead channel,
 * Ironman II reactive power attenuator, 2x 6V6GT power stage,
 * Tone King 33 1x12 Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-toneking-imperial"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_RHYTHM_VOL     = 4,
    PORT_RHYTHM_TREBLE  = 5,
    PORT_RHYTHM_BASS    = 6,
    PORT_MID_BITE       = 7,
    PORT_LEAD_VOL       = 8,
    PORT_LEAD_TONE      = 9,
    PORT_ATTENUATOR     = 10,
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

class CyberToneKingImperial {
public:
    CyberToneKingImperial(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();
        midBiteFilter.setIdentity();
        leadToneFilter.setIdentity();

        cabTkLow.setIdentity();
        cabTkMid.setIdentity();
        cabTkLp1.setIdentity();
        cabTkLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapRhy1 = gridCapRhy2 = gridCapLead1 = gridCapLead2 = 0.0f;

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
        // Tone King 33 1x12 Custom Driver (Rich 80Hz woody low bloom, singing 2.8kHz presence, smooth 5.4kHz cutoff)
        cabTkLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabTkMid.setPeaking(2800.0f, 4.0f, 1.4f, sampleRate);
        cabTkLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabTkLp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.006f;
        } else {
            gridBias *= 0.9997f;
        }
        float effIn = (in - gridBias * 0.22f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 2x 6V6GT 20W Cathode-Biased Power Section (Singing compression, warm sag)
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

        s = cabTkLow.process(s);
        s = cabTkMid.process(s);
        s = cabTkLp1.process(s);
        s = cabTkLp2.process(s);
        return s * 1.05f;
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float treb, float midBite, float leadTone) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(120.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        // Mid-Bite: transforms blackface scooped mid into punchy tweed growl (550 Hz - 900 Hz boost)
        float biteDb = (midBite / 10.0f) * 7.5f;
        midBiteFilter.setPeaking(750.0f, biteDb, 1.3f, sampleRate);

        // Lead Channel Tone Control (Tweed 5E3 style low-pass cut)
        float leadCutFc = 1500.0f + (leadTone / 10.0f) * 5500.0f;
        leadToneFilter.setLowPass(leadCutFc, 0.707f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pRhyVol, float pRhyTreb, float pRhyBass,
        float pMidBite, float pLeadVol, float pLeadTone, float pAttenuator,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pRhyBass, pRhyTreb, pMidBite, pLeadTone);

        bool isLead = (channel > 0.5f);
        int cabType = (int)std::max(0.0f, std::min(1.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        // Ironman II Reactive Attenuation steps: 0dB, -3dB, -9dB, -15dB, -24dB, -36dB
        int attStep = (int)std::max(0.0f, std::min(5.0f, pAttenuator));
        float attenFactors[6] = { 1.0f, 0.707f, 0.355f, 0.178f, 0.063f, 0.015f };
        float ironmanFactor = attenFactors[attStep];

        float rhyGain = audioTaper(pRhyVol, 2.6f);
        float leadGain = audioTaper(pLeadVol, 4.2f);

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            if (!isLead) {
                // --- RHYTHM CHANNEL (BLACKFACE CHIME & MID-BITE) ---
                s = triode12AX7(s, gridCapRhy1, 1.85f, 0.08f);
                s = snubberRhy.lp(s, 11500.0f, sampleRate);
                s *= rhyGain;

                s = toneBass.process(s);
                s = toneTreble.process(s);
                s = midBiteFilter.process(s);
                s = couplingCapTone.hp(s, 18.0f, sampleRate);

                s = triode12AX7(s, gridCapRhy2, 1.75f, 0.08f);
            } else {
                // --- LEAD CHANNEL (RAW TWEED DELUXE CRUNCH) ---
                s = triode12AX7(s, gridCapLead1, 2.3f, 0.12f);
                s = snubberLead.lp(s, 9500.0f, sampleRate);
                s *= leadGain;

                s = leadToneFilter.process(s);
                s = triode12AX7(s, gridCapLead2, 2.5f, 0.14f);
            }

            // Phase Inverter & 2x 6V6GT Power Stage
            float push = s * 3.0f;
            float pull = -s * 3.0f * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // 6V6 cathode bias dynamic sag
            float currentDraw = fabsf(pOut) * 0.040f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0025f;
            sagVoltage = std::max(0.66f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Ironman II Attenuator output scaling
            pOut *= ironmanFactor;

            // Dynamic Tone King 33 Speaker Simulation
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
    Biquad toneBass, toneTreble, midBiteFilter, leadToneFilter;
    Biquad cabTkLow, cabTkMid, cabTkLp1, cabTkLp2;
    OnePole snubberRhy, snubberLead, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost, couplingCapRhy, couplingCapLead, couplingCapTone;

    float sagVoltage;
    float gridCapRhy1, gridCapRhy2, gridCapLead1, gridCapLead2;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberToneKingImperialLV2 {
    CyberToneKingImperial* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* rhyVol;
    const float* rhyTreble;
    const float* rhyBass;
    const float* midBite;
    const float* leadVol;
    const float* leadTone;
    const float* attenuator;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberToneKingImperialLV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), rhyVol(nullptr), rhyTreble(nullptr), rhyBass(nullptr),
        midBite(nullptr), leadVol(nullptr), leadTone(nullptr), attenuator(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberToneKingImperialLV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberToneKingImperialLV2* handle = new CyberToneKingImperialLV2();
    handle->dsp = new CyberToneKingImperial(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberToneKingImperialLV2* h = (CyberToneKingImperialLV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_CHANNEL:      h->channel = (const float*)data; break;
        case PORT_RHYTHM_VOL:   h->rhyVol = (const float*)data; break;
        case PORT_RHYTHM_TREBLE:h->rhyTreble = (const float*)data; break;
        case PORT_RHYTHM_BASS:  h->rhyBass = (const float*)data; break;
        case PORT_MID_BITE:     h->midBite = (const float*)data; break;
        case PORT_LEAD_VOL:     h->leadVol = (const float*)data; break;
        case PORT_LEAD_TONE:    h->leadTone = (const float*)data; break;
        case PORT_ATTENUATOR:   h->attenuator = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberToneKingImperialLV2* h = (CyberToneKingImperialLV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 0.0f,
        h->rhyVol ? *h->rhyVol : 5.0f,
        h->rhyTreble ? *h->rhyTreble : 5.5f,
        h->rhyBass ? *h->rhyBass : 5.0f,
        h->midBite ? *h->midBite : 3.0f,
        h->leadVol ? *h->leadVol : 6.0f,
        h->leadTone ? *h->leadTone : 5.5f,
        h->attenuator ? *h->attenuator : 0.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberToneKingImperialLV2* h = (CyberToneKingImperialLV2*)instance; if (h) delete h; }
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
