/*
 * Cyber Friedman BE-100 - Hot-Rodded British High-Gain Amplifier LV2 Plugin
 * Authentic analog circuit emulation of Dave Friedman's flagship BE-100.
 * Features:
 *  - 4-Stage V1A -> V1B (Cold Clipper) -> V2A -> V2B (HBE) All-Tube Cascade
 *  - Dave Friedman C45 Voicing (Mid-Focus Bright Switch) & FAT Low-End Push
 *  - SAT Diode Soft-Saturation Circuit
 *  - Authentic Marshall 3-Band Cathode-Follower Tone Stack with natural mid-scoop
 *  - 4x EL34 100W Power Section with Presence in Negative Feedback Loop
 *  - Friedman 4x12 (V30 & Greenback) Dynamic Speaker Stress with open 6.8kHz top end
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

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-friedman-be100"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_CHANNEL        = 3,
    PORT_HBE_MODE       = 4,
    PORT_CLEAN_VOL      = 5,
    PORT_BE_GAIN        = 6,
    PORT_BASS           = 7,
    PORT_MID            = 8,
    PORT_TREBLE         = 9,
    PORT_MASTER         = 10,
    PORT_PRESENCE       = 11,
    PORT_C45            = 12,
    PORT_FAT            = 13,
    PORT_SAT            = 14,
    PORT_SPEAKER_CAB    = 15,
    PORT_SPEAKER_DRIVE  = 16,
    PORT_NOISE_GATE     = 17,
    PORT_OUTPUT_LEVEL   = 18,
    PORT_COUNT          = 18 + 1
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

class CyberFriedmanBE100 {
public:
    CyberFriedmanBE100(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        presenceFilter.setIdentity();
        c45Voicing.setIdentity();

        cabMixLow.setIdentity();
        cabMixMid.setIdentity();
        cabMixLp.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp.setIdentity();

        sagVoltage = 1.0f;

        // Independent dedicated grid bias capacitors for EVERY stage
        gridCapClean1 = gridCapClean2 = 0.0f;
        gridCapV1A = gridCapV1B = gridCapV2A = gridCapV2B = 0.0f;

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
        // Friedman 4x12 Mixed (Celestion V30 + Greenback):
        // Massive 95Hz low punch, singing 3.4kHz upper-mid vocal spike, natural open 6.8kHz acoustic rolloff
        cabMixLow.setPeaking(95.0f, 5.2f, 1.2f, sampleRate);
        cabMixMid.setPeaking(3400.0f, 4.5f, 1.4f, sampleRate);
        cabMixLp.setLowPass(6800.0f, 0.707f, sampleRate);

        // 4x12 Vintage Celestion Greenbacks (Chewy woody midrange, early compression, 2.6kHz bite)
        cabGbLow.setPeaking(105.0f, 4.8f, 1.1f, sampleRate);
        cabGbMid.setPeaking(2600.0f, 4.2f, 1.3f, sampleRate);
        cabGbLp.setLowPass(6400.0f, 0.707f, sampleRate);
    }

    inline float triode12AX7(float in, float &gridBias, float gainFactor, float asym) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.004f;
        } else {
            gridBias *= 0.9997f;
        }
        float effIn = (in - gridBias * 0.15f) * gainFactor;
        float t = tanhf(effIn);
        return t - asym * (t * t);
    }

    // Dave Friedman / Marshall 10k Cold Clipper Stage (V1B)
    inline float triodeColdClipper(float in, float &gridBias, float gainFactor) {
        if (in > 0.0f) {
            gridBias += (in - gridBias) * 0.002f;
        } else {
            gridBias *= 0.9998f;
        }
        float effIn = (in - gridBias * 0.12f) * gainFactor;
        float out;
        if (effIn < -0.45f) {
            out = -0.45f - 0.12f * tanhf((effIn + 0.45f) * 1.5f);
        } else {
            out = tanhf(effIn * 0.95f) * 1.15f;
        }
        return out;
    }

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
            if (rect > speakerEnv) {
                speakerEnv += spkAtk * (rect - speakerEnv);
            } else {
                speakerEnv += spkRel * (rect - speakerEnv);
            }
            speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.2f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.25f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.25f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.04f * (t * t)) / (1.0f + driveAmount * 0.25f);

            float dampingFc = 6800.0f - driveAmount * 1400.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.80f) * s + (driveAmount * 0.80f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.25f);
        }

        if (cabType == 1) {
            s = cabGbLow.process(s);
            s = cabGbMid.process(s);
            s = cabGbLp.process(s);
            return s * 1.0f;
        } else {
            s = cabMixLow.process(s);
            s = cabMixMid.process(s);
            s = cabMixLp.process(s);
            return s * 1.05f;
        }
    }

    inline float logTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.40f * norm + 0.60f * norm * norm) * (maxGain * 1.85f);
    }

    void updateParams(float bass, float mid, float treb, float pres, float c45) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(110.0f, bassDb, 0.7f, sampleRate);

        // Marshall mid scoop: naturally scooped by -5.5dB at 650Hz when at 5.0
        float midDb = -5.5f + (mid / 10.0f) * 10.5f;
        toneMid.setPeaking(650.0f, midDb, 1.2f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.5f;
        toneTreble.setHighShelf(3200.0f, trebDb, 0.7f, sampleRate);

        if (c45 > 0.5f) {
            c45Voicing.setPeaking(2200.0f, 3.8f, 1.3f, sampleRate);
        } else {
            c45Voicing.setIdentity();
        }

        float presDb = (pres / 10.0f) * 8.5f;
        presenceFilter.setHighShelf(4200.0f, presDb, 0.7f, sampleRate);
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float channel, float hbeMode,
        float pCleanVol, float pBeGain, float pBass, float pMid, float pTreble,
        float pMaster, float pPresence, float pC45, float pFat, float pSat,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pBass, pMid, pTreble, pPresence, pC45);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool isLead = (channel > 0.5f);
        bool isHbe = (hbeMode > 0.5f);
        bool useGate = (pNoiseGate > 0.5f);

        float cleanGain = logTaper(pCleanVol, 3.2f);
        float masterGain = logTaper(pMaster, 3.5f);
        
        float beGain = logTaper(pBeGain, 6.5f) + 0.5f;
        if (isHbe) beGain *= 1.85f;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            if (pFat > 0.5f) {
                s = fatBoost.lp(s, 140.0f, sampleRate) * 1.35f + s;
            }

            if (!isLead) {
                s = triode12AX7(s, gridCapClean1, 1.5f, 0.06f);
                s = antiAlias1.lp(s, 12500.0f, sampleRate);
                s = couplingCapV1A.hp(s, 18.0f, sampleRate);
                s = toneBass.process(s);
                s = toneTreble.process(s);
                s = couplingCapTone.hp(s, 20.0f, sampleRate);
                s *= cleanGain;
                s = triode12AX7(s, gridCapClean2, 1.4f, 0.06f);
            } else {
                // V1A
                s = antiAliasTight.hp(s, 32.0f, sampleRate);
                s = triode12AX7(s, gridCapV1A, 2.2f, 0.08f);
                s = antiAlias1.lp(s, 12500.0f, sampleRate);
                s = couplingCapV1A.hp(s, 18.0f, sampleRate);

                if (pC45 < 0.5f) {
                    float brightMix = std::max(0.0f, 1.0f - (pBeGain / 10.0f)) * 0.45f;
                    s = s + brightCap.hp(s, 2800.0f, sampleRate) * brightMix;
                } else {
                    s = c45Voicing.process(s);
                }

                s *= beGain;

                // V1B Cold Clipper
                s = triodeColdClipper(s, gridCapV1B, 2.4f);
                s = antiAlias2.lp(s, 12000.0f, sampleRate);
                s = couplingCapV1B.hp(s, 18.0f, sampleRate);

                // V2A Saturation Stage
                s = triode12AX7(s, gridCapV2A, 2.5f, 0.12f);
                s = antiAlias3.lp(s, 12000.0f, sampleRate);
                s = couplingCapV2A.hp(s, 18.0f, sampleRate);

                // V2B HBE Stage
                if (isHbe) {
                    s = triode12AX7(s, gridCapV2B, 2.6f, 0.14f);
                    s = antiAlias4.lp(s, 11500.0f, sampleRate);
                }

                if (pSat > 0.5f) {
                    s = tanhf(s * 1.35f) * 0.82f + 0.18f * s;
                }

                s = toneBass.process(s);
                s = toneMid.process(s);
                s = toneTreble.process(s);
                s = couplingCapTone.hp(s, 20.0f, sampleRate);
            }

            float push = s * masterGain;
            float pull = -s * masterGain * 0.985f;

            float pOut = (powerTubeStage(push, sagVoltage) - powerTubeStage(pull, sagVoltage)) * 0.5f;
            pOut = antiAliasPower.lp(pOut, 11800.0f, sampleRate);

            float currentDraw = fabsf(pOut) * 0.028f;
            sagVoltage += (1.0f - currentDraw - sagVoltage) * 0.0018f;
            sagVoltage = std::max(0.72f, std::min(1.0f, sagVoltage));

            pOut = presenceFilter.process(pOut);
            pOut = dcBlockerPost.hp(pOut, 20.0f, sampleRate);

            pOut = processSpeaker(pOut, cabType, pSpeakerDrive);

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
    Biquad toneBass, toneMid, toneTreble, presenceFilter, c45Voicing;
    Biquad cabMixLow, cabMixMid, cabMixLp;
    Biquad cabGbLow, cabGbMid, cabGbLp;

    OnePole dcBlockerPre, dcBlockerPost, fatBoost;
    OnePole antiAliasTight, antiAlias1, antiAlias2, antiAlias3, antiAlias4, antiAliasPower, brightCap, couplingCapV1A, couplingCapV1B, couplingCapV2A, couplingCapTone;

    float sagVoltage;

    float gridCapClean1, gridCapClean2;
    float gridCapV1A, gridCapV1B, gridCapV2A, gridCapV2B;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct PluginData {
    CyberFriedmanBE100* amp;
    float* ports[PORT_COUNT];
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* path, const LV2_Feature* const* features) {
    PluginData* data = new PluginData();
    data->amp = new CyberFriedmanBE100(rate);
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
    float channel       = data->ports[PORT_CHANNEL] ? *data->ports[PORT_CHANNEL] : 1.0f;
    float hbeMode       = data->ports[PORT_HBE_MODE] ? *data->ports[PORT_HBE_MODE] : 0.0f;
    float cleanVol      = data->ports[PORT_CLEAN_VOL] ? *data->ports[PORT_CLEAN_VOL] : 5.0f;
    float beGain        = data->ports[PORT_BE_GAIN] ? *data->ports[PORT_BE_GAIN] : 6.0f;
    float bass          = data->ports[PORT_BASS] ? *data->ports[PORT_BASS] : 5.5f;
    float mid           = data->ports[PORT_MID] ? *data->ports[PORT_MID] : 5.0f;
    float treble        = data->ports[PORT_TREBLE] ? *data->ports[PORT_TREBLE] : 6.0f;
    float master        = data->ports[PORT_MASTER] ? *data->ports[PORT_MASTER] : 5.0f;
    float presence      = data->ports[PORT_PRESENCE] ? *data->ports[PORT_PRESENCE] : 5.5f;
    float c45           = data->ports[PORT_C45] ? *data->ports[PORT_C45] : 0.0f;
    float fat           = data->ports[PORT_FAT] ? *data->ports[PORT_FAT] : 0.0f;
    float sat           = data->ports[PORT_SAT] ? *data->ports[PORT_SAT] : 0.0f;
    float speakerCab    = data->ports[PORT_SPEAKER_CAB] ? *data->ports[PORT_SPEAKER_CAB] : 0.0f;
    float speakerDrive  = data->ports[PORT_SPEAKER_DRIVE] ? *data->ports[PORT_SPEAKER_DRIVE] : 4.5f;
    float outputLevel   = data->ports[PORT_OUTPUT_LEVEL] ? *data->ports[PORT_OUTPUT_LEVEL] : 7.0f;
    float noiseGate     = data->ports[PORT_NOISE_GATE] ? *data->ports[PORT_NOISE_GATE] : 1.0f;

    data->amp->process(
        in, out, sample_count,
        bypass, channel, hbeMode, cleanVol, beGain, bass, mid, treble,
        master, presence, c45, fat, sat, speakerCab, speakerDrive, noiseGate, outputLevel
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
