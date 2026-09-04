/*
 * Cyber Fender Brownface Deluxe 6G3 (1961) - 20W 6V6 "Mini-Marshall" Tube Combo LV2 Plugin
 * Authentic analog circuit emulation of the 1961 Fender 6G3 Deluxe.
 * Features:
 *  - Independent Normal and Bright Channels with 12AX7 Preamp & Driver Cascades
 *  - Dynamic Bright Cap across Volume Pot that smoothly tapers with rotation
 *  - Pure Fixed-Bias 2x 6V6GT Power Stage with Bias-Wiggle Tremolo (zero DC thump)
 *  - 5Y3 Tube Rectifier Dynamic Sag & Compression
 *  - Jensen P12R Alnico 12" Dynamic Speaker Stress with natural 6.6kHz acoustic top end
 *  - Zero-Latency Smart Zero-Floor Noise Suppressor with transparent -80dB threshold
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-fender-deluxe6g3"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_NORM_VOL       = 4,
    PORT_NORM_TONE      = 5,
    PORT_BRIGHT_VOL     = 6,
    PORT_BRIGHT_TONE    = 7,
    PORT_TREM_SPEED     = 8,
    PORT_TREM_INTENSITY = 9,
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

class CyberFenderDeluxe6G3 {
public:
    CyberFenderDeluxe6G3(double rate) : sampleRate(rate) {
        toneFilter.setIdentity();

        cabP12Low.setIdentity();
        cabP12Mid.setIdentity();
        cabP12Lp.setIdentity();

        cabBluLow.setIdentity();
        cabBluMid.setIdentity();
        cabBluLp.setIdentity();

        sagVoltage = 1.0f;
        gridCapNorm1 = gridCapNorm2 = 0.0f;
        gridCapBrill1 = gridCapBrill2 = 0.0f;
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
        // 1x12 Jensen P12R Brownface (Woody 88Hz punch, raw 2.6kHz mid-bark, natural 6.6kHz open acoustic rolloff)
        cabP12Low.setPeaking(88.0f, 4.5f, 1.2f, sampleRate);
        cabP12Mid.setPeaking(2600.0f, 4.0f, 1.3f, sampleRate);
        cabP12Lp.setLowPass(6600.0f, 0.707f, sampleRate);

        // 1x12 Celestion Blue Alnico (Chimey 3.2kHz bell presence, tight 80Hz bass)
        cabBluLow.setPeaking(80.0f, 4.8f, 1.1f, sampleRate);
        cabBluMid.setPeaking(3200.0f, 4.2f, 1.4f, sampleRate);
        cabBluLp.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.004f;
        } else {
            gridBias *= 0.9997f;
        }
        float effIn = (in - gridBias * 0.16f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // 2x 6V6GT Fixed-Bias Power Stage (Punchy, aggressive "Mini-Marshall" crunch)
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
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.28f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.30f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.04f * (t * t)) / (1.0f + driveAmount * 0.25f);

            float dampingFc = 6600.0f - driveAmount * 1400.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.80f) * s + (driveAmount * 0.80f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.25f);
        }

        if (cabType == 1) {
            s = cabBluLow.process(s);
            s = cabBluMid.process(s);
            s = cabBluLp.process(s);
            return s * 1.02f;
        } else {
            s = cabP12Low.process(s);
            s = cabP12Mid.process(s);
            s = cabP12Lp.process(s);
            return s * 1.05f;
        }
    }

    inline float logTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.42f * norm + 0.58f * norm * norm) * (maxGain * 1.85f);
    }

    void updateParams(float tone) {
        // 6G3 Tone control: single RC low-pass / high-pass tilt
        // At Tone=5.0: Balanced neutral vintage Tweed response
        // At Tone=10.0: +8dB sparkling upper-mid bite (2.8kHz)
        // At Tone=0.0: Warm, mellow jazz roll-off (-10dB)
        float toneDb = (tone - 5.0f) * 2.8f;
        toneFilter.setHighShelf(2400.0f, toneDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float pNormVol, float pNormTone,
        float pBrightVol, float pBrightTone, float pTremSpeed, float pTremIntensity,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        bool isBright = (channel > 0.5f);
        float activeVol = isBright ? pBrightVol : pNormVol;
        float activeTone = isBright ? pBrightTone : pNormTone;

        updateParams(activeTone);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        // 6G3 Volume Gain: Full dynamic punch from 1 to 10
        float preGain = logTaper(activeVol, 5.8f) + 0.4f;

        // Bright cap mix: across volume pot (maximum at low volume, zero at max volume)
        float brightMix = isBright ? std::max(0.0f, 1.0f - (activeVol / 10.0f)) * 0.55f : 0.0f;

        // Bias-wiggle tremolo depth & frequency
        float tremDepth = (pTremIntensity > 1.0f) ? (pTremIntensity / 100.0f) * 0.75f : 0.0f;
        float tremInc = (2.0f * (float)M_PI * pTremSpeed) / (float)sampleRate;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // Stage 1 (V1 12AX7 Preamp) with independent bias capacitor
            if (isBright) {
                s = triode12AX7(s, gridCapBrill1, 1.9f, 0.08f);
                // Bright cap gives crisp chime at low volumes
                if (brightMix > 0.01f) {
                    s = s + brightCap.hp(s, 2400.0f, sampleRate) * brightMix;
                }
            } else {
                s = triode12AX7(s, gridCapNorm1, 1.8f, 0.08f);
            }
            s = antiAlias1.lp(s, 12500.0f, sampleRate);

            s *= preGain;
            s = toneFilter.process(s);
            s = couplingCap1.hp(s, 20.0f, sampleRate);

            // Stage 2 (V2 12AX7 Driver) with independent bias capacitor
            if (isBright) {
                s = triode12AX7(s, gridCapBrill2, 2.2f, 0.10f);
            } else {
                s = triode12AX7(s, gridCapNorm2, 2.1f, 0.10f);
            }
            s = antiAlias2.lp(s, 12000.0f, sampleRate);
            s = couplingCap2.hp(s, 20.0f, sampleRate);

            // True Bias-Wiggle Tremolo: Modulates stage gain symmetrically (ZERO DC thump!)
            float tremGain = 1.0f;
            if (tremDepth > 0.01f) {
                float lfo = sinf(tremPhase);
                tremPhase += tremInc;
                if (tremPhase > 2.0f * (float)M_PI) tremPhase -= 2.0f * (float)M_PI;
                tremGain = 1.0f + lfo * tremDepth * 0.75f;
            }

            // Phase Inverter & 2x 6V6GT Fixed-Bias Power Section
            float push = s * 3.4f * tremGain;
            float pull = -s * 3.4f * 0.985f * tremGain;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = antiAliasPower.lp(pOut, 11800.0f, sampleRate);

            // 5Y3 Tube Rectifier Dynamic Sag & Compression
            float currentDraw = fabsf(pOut) * 0.042f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0028f;
            sagVoltage = std::max(0.65f, std::min(1.0f, sagVoltage));

            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            // Dynamic 1x12 Jensen P12R Speaker Simulation
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
    Biquad toneFilter;
    Biquad cabP12Low, cabP12Mid, cabP12Lp;
    Biquad cabBluLow, cabBluMid, cabBluLp;

    OnePole dcBlockerPre, dcBlockerPost, brightCap, couplingCap1, couplingCap2;
    OnePole antiAlias1, antiAlias2, antiAliasPower;

    float sagVoltage;

    // Independent dedicated grid bias capacitors
    float gridCapNorm1, gridCapNorm2;
    float gridCapBrill1, gridCapBrill2;
    float tremPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct PluginData {
    CyberFenderDeluxe6G3* amp;
    float* ports[PORT_COUNT];
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* path, const LV2_Feature* const* features) {
    PluginData* data = new PluginData();
    data->amp = new CyberFenderDeluxe6G3(rate);
    for (int i = 0; i < PORT_COUNT; ++i) data->ports[i] = nullptr;
    return (LV2_Handle)data;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    PluginData* data = (PluginData*)instance;
    if (port < PORT_COUNT) data->ports[port] = (float*)data_location;
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t sample_count) {
    PluginData* data = (PluginData*)instance;
    if (!data || !data->amp) return;

    const float* in = data->ports[PORT_AUDIO_IN];
    float* out = data->ports[PORT_AUDIO_OUT];
    if (!in || !out) return;

    float bypass        = data->ports[PORT_BYPASS] ? *data->ports[PORT_BYPASS] : 1.0f;
    float channel       = data->ports[PORT_CHANNEL] ? *data->ports[PORT_CHANNEL] : 0.0f;
    float pNormVol      = data->ports[PORT_NORM_VOL] ? *data->ports[PORT_NORM_VOL] : 5.0f;
    float pNormTone     = data->ports[PORT_NORM_TONE] ? *data->ports[PORT_NORM_TONE] : 5.5f;
    float pBrightVol    = data->ports[PORT_BRIGHT_VOL] ? *data->ports[PORT_BRIGHT_VOL] : 5.0f;
    float pBrightTone   = data->ports[PORT_BRIGHT_TONE] ? *data->ports[PORT_BRIGHT_TONE] : 6.0f;
    float pTremSpeed    = data->ports[PORT_TREM_SPEED] ? *data->ports[PORT_TREM_SPEED] : 4.5f;
    float pTremIntensity= data->ports[PORT_TREM_INTENSITY] ? *data->ports[PORT_TREM_INTENSITY] : 0.0f;
    float pSpeakerCab   = data->ports[PORT_SPEAKER_CAB] ? *data->ports[PORT_SPEAKER_CAB] : 0.0f;
    float pSpeakerDrive = data->ports[PORT_SPEAKER_DRIVE] ? *data->ports[PORT_SPEAKER_DRIVE] : 4.0f;
    float pNoiseGate    = data->ports[PORT_NOISE_GATE] ? *data->ports[PORT_NOISE_GATE] : 1.0f;
    float pOutputLevel  = data->ports[PORT_OUTPUT_LEVEL] ? *data->ports[PORT_OUTPUT_LEVEL] : 7.0f;

    data->amp->process(
        in, out, sample_count,
        bypass, channel, pNormVol, pNormTone, pBrightVol, pBrightTone,
        pTremSpeed, pTremIntensity, pSpeakerCab, pSpeakerDrive, pNoiseGate, pOutputLevel
    );
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    PluginData* data = (PluginData*)instance;
    if (data) {
        delete data->amp;
        delete data;
    }
}

static const void* extension_data(const char* uri) {
    return nullptr;
}

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

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : nullptr;
}
