/*
 * Cyber Fender Brownface Vibrolux 6G11 (1961) - 30W 6L6 Tube Combo LV2 Plugin
 * Authentic analog circuit emulation of the 1961 Fender 6G11 Vibrolux,
 * featuring Normal & Bright channels, 2x 6L6GC power stage with GZ34 rectifier sag,
 * bias-wiggle tube tremolo, 1x12 Oxford 12L6 Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-fender-vibrolux6g11"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_NORM_VOL       = 4,
    PORT_NORM_BASS      = 5,
    PORT_NORM_TREBLE    = 6,
    PORT_BRIGHT_VOL     = 7,
    PORT_BRIGHT_BASS    = 8,
    PORT_BRIGHT_TREBLE  = 9,
    PORT_TREM_SPEED     = 10,
    PORT_TREM_INTENSITY = 11,
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

class CyberFenderVibrolux6G11 {
public:
    CyberFenderVibrolux6G11(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneTreble.setIdentity();
        brightCapFilter.setIdentity();

        cabOxfLow.setIdentity();
        cabOxfMid.setIdentity();
        cabOxfLp1.setIdentity();
        cabOxfLp2.setIdentity();

        cabJenLow.setIdentity();
        cabJenMid.setIdentity();
        cabJenLp1.setIdentity();
        cabJenLp2.setIdentity();

        sagVoltage = 1.0f;
        gridCapBrill1 = gridCapBrill2 = gridCapNorm1 = gridCapNorm2 = 0.0f;
        tremPhase = 0.0f;

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
        // 1x12 Oxford 12L6 (Smooth 80Hz resonant bloom, 2.7kHz warm bark, 5.1kHz cutoff)
        cabOxfLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabOxfMid.setPeaking(2700.0f, 4.0f, 1.3f, sampleRate);
        cabOxfLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabOxfLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 1x12 Jensen P12Q (Crisp 3.1kHz top, tight 78Hz bass)
        cabJenLow.setPeaking(78.0f, 4.6f, 1.1f, sampleRate);
        cabJenMid.setPeaking(3100.0f, 4.2f, 1.4f, sampleRate);
        cabJenLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabJenLp2.setLowPass(6800.0f, 0.707f, sampleRate);
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

    // 2x 6L6GC 30W Power Stage with GZ34 Rectifier Sag
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

        if (cabType == 1) {
            s = cabJenLow.process(s);
            s = cabJenMid.process(s);
            s = cabJenLp1.process(s);
            s = cabJenLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabOxfLow.process(s);
            s = cabOxfMid.process(s);
            s = cabOxfLp1.process(s);
            s = cabOxfLp2.process(s);
            return s * 1.05f;
        }
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 2.0f);
    }

    void updateParams(float bass, float treb, bool isBright) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(120.0f, bassDb, 0.7f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.6f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        if (isBright) {
            brightCapFilter.setHighShelf(3400.0f, 5.5f, 0.7f, sampleRate);
        } else {
            brightCapFilter.setIdentity();
        }
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pNormVol, float pNormBass, float pNormTreb,
        float pBrightVol, float pBrightBass, float pBrightTreb, float pTremSpeed, float pTremIntensity,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        bool isBright = (channel > 0.5f);
        float activeVol = isBright ? pBrightVol : pNormVol;
        float activeBass = isBright ? pBrightBass : pNormBass;
        float activeTreb = isBright ? pBrightTreb : pNormTreb;

        updateParams(activeBass, activeTreb, isBright);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        float preGain = audioTaper(activeVol, 3.6f);
        float tremDepth = (pTremIntensity > 1.0f) ? (pTremIntensity / 100.0f) * 0.45f : 0.0f;
        float tremInc = (2.0f * (float)M_PI * pTremSpeed) / (float)sampleRate;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 22.0f, sampleRate);

            // 12AX7 Preamp Stage
            s = triode12AX7(s, isBright ? gridCapBrill1 : gridCapNorm1, 1.8f, 0.08f);
            s = snubberPre.lp(s, 11500.0f, sampleRate);

            if (isBright) {
                s = brightCapFilter.process(s);
            }

            s *= preGain;

            // Tone Stack
            s = toneBass.process(s);
            s = toneTreble.process(s);
            s = couplingCapTone.hp(s, 18.0f, sampleRate);

            // V2 12AX7 Driver
            s = triode12AX7(s, isBright ? gridCapBrill2 : gridCapNorm2, 2.1f, 0.10f);

            
            // True Bias-Wiggle Tremolo: Modulates stage gain symmetrically (ZERO DC thump!)
            float tremGain = 1.0f;
            if (tremDepth > 0.01f) {
                float lfo = sinf(tremPhase);
                tremPhase += tremInc;
                if (tremPhase > 2.0f * (float)M_PI) tremPhase -= 2.0f * (float)M_PI;
                tremGain = 1.0f + lfo * tremDepth * 0.75f;
            }

            // Phase Inverter & 2x 6L6GC Power Stage
            float push = s * 3.4f * tremGain;
            float pull = -s * 3.4f * 0.985f * tremGain;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
pOut = snubberPower.lp(pOut, 11500.0f, sampleRate);

            // GZ34 Tube Rectifier Dynamic Sag
            float currentDraw = fabsf(pOut) * 0.040f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.65f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 1x12 Oxford 12L6 Speaker Simulation
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
    Biquad toneBass, toneTreble, brightCapFilter;
    Biquad cabOxfLow, cabOxfMid, cabOxfLp1, cabOxfLp2;
    Biquad cabJenLow, cabJenMid, cabJenLp1, cabJenLp2;
    OnePole snubberPre, snubberPower;
    OnePole dcBlockerPre, dcBlockerPost, couplingCap1, couplingCapTone;

    float sagVoltage;
    float gridCapBrill1, gridCapBrill2, gridCapNorm1, gridCapNorm2;
    float tremPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberFenderVibrolux6G11LV2 {
    CyberFenderVibrolux6G11* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* channel;
    const float* normVol;
    const float* normBass;
    const float* normTreble;
    const float* brightVol;
    const float* brightBass;
    const float* brightTreble;
    const float* tremSpeed;
    const float* tremIntensity;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberFenderVibrolux6G11LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), channel(nullptr), normVol(nullptr), normBass(nullptr), normTreble(nullptr),
        brightVol(nullptr), brightBass(nullptr), brightTreble(nullptr),
        tremSpeed(nullptr), tremIntensity(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberFenderVibrolux6G11LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberFenderVibrolux6G11LV2* handle = new CyberFenderVibrolux6G11LV2();
    handle->dsp = new CyberFenderVibrolux6G11(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberFenderVibrolux6G11LV2* h = (CyberFenderVibrolux6G11LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_CHANNEL:        h->channel = (const float*)data; break;
        case PORT_NORM_VOL:       h->normVol = (const float*)data; break;
        case PORT_NORM_BASS:      h->normBass = (const float*)data; break;
        case PORT_NORM_TREBLE:    h->normTreble = (const float*)data; break;
        case PORT_BRIGHT_VOL:     h->brightVol = (const float*)data; break;
        case PORT_BRIGHT_BASS:    h->brightBass = (const float*)data; break;
        case PORT_BRIGHT_TREBLE:  h->brightTreble = (const float*)data; break;
        case PORT_TREM_SPEED:     h->tremSpeed = (const float*)data; break;
        case PORT_TREM_INTENSITY: h->tremIntensity = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberFenderVibrolux6G11LV2* h = (CyberFenderVibrolux6G11LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->channel ? *h->channel : 1.0f,
        h->normVol ? *h->normVol : 5.0f,
        h->normBass ? *h->normBass : 5.0f,
        h->normTreble ? *h->normTreble : 5.5f,
        h->brightVol ? *h->brightVol : 5.5f,
        h->brightBass ? *h->brightBass : 5.0f,
        h->brightTreble ? *h->brightTreble : 6.0f,
        h->tremSpeed ? *h->tremSpeed : 4.5f,
        h->tremIntensity ? *h->tremIntensity : 0.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberFenderVibrolux6G11LV2* h = (CyberFenderVibrolux6G11LV2*)instance; if (h) delete h; }
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
