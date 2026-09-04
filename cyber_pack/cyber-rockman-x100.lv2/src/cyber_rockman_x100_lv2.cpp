/*
 * Cyber Tom Scholz Rockman X100 (1982) - 80s AOR Guitar System LV2 Plugin
 * Authentic analog circuit emulation of the 1982 SR&D Rockman X100,
 * featuring 4 Mode Amp voicing (Clean 1, Clean 2, Edge, Dist),
 * FET compression, 800Hz / 2.1kHz Scholz mid filter, analog BBD stereo chorus,
 * Rockman studio frequency shaping, Dynamic Speaker Stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-rockman-x100"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_MODE           = 3,
    PORT_VOLUME         = 4,
    PORT_CHORUS         = 5,
    PORT_CHORUS_DEPTH   = 6,
    PORT_BASS_BOOST     = 7,
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

class CyberRockmanX100 {
public:
    CyberRockmanX100(double rate) : sampleRate(rate) {
        scholzMid1.setIdentity();
        scholzMid2.setIdentity();
        bassBoostFilter.setIdentity();

        cabStudioLow.setIdentity();
        cabStudioMid.setIdentity();
        cabStudioLp1.setIdentity();
        cabStudioLp2.setIdentity();

        cabGbLow.setIdentity();
        cabGbMid.setIdentity();
        cabGbLp1.setIdentity();
        cabGbLp2.setIdentity();

        compEnv = 0.0f;
        compAtk = 1.0f - expf(-1.0f / ((float)sampleRate * 0.0012f));
        compRel = 1.0f - expf(-1.0f / ((float)sampleRate * 0.080f));

        chorusBuffer = new float[2048];
        memset(chorusBuffer, 0, 2048 * sizeof(float));
        chorusWriteIdx = 0;
        chorusPhase = 0.0f;

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

    ~CyberRockmanX100() {
        if (chorusBuffer) delete[] chorusBuffer;
    }

    void initFilters() {
        // Tom Scholz Signature Mid Peaks (800 Hz throat bloom + 2.1 kHz laser bite)
        scholzMid1.setPeaking(800.0f, 4.5f, 1.4f, sampleRate);
        scholzMid2.setPeaking(2100.0f, 5.2f, 1.5f, sampleRate);

        // Rockman Direct Studio Cab Simulation (Warm 100Hz bottom, 3.2kHz studio bite, 5.4kHz cutoff)
        cabStudioLow.setPeaking(100.0f, 4.2f, 1.2f, sampleRate);
        cabStudioMid.setPeaking(3200.0f, 4.5f, 1.4f, sampleRate);
        cabStudioLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabStudioLp2.setLowPass(6800.0f, 0.707f, sampleRate);

        // 4x12 Greenback
        cabGbLow.setPeaking(80.0f, 4.8f, 1.2f, sampleRate);
        cabGbMid.setPeaking(2500.0f, 4.0f, 1.3f, sampleRate);
        cabGbLp1.setLowPass(6800.0f, 0.707f, sampleRate);
        cabGbLp2.setLowPass(6800.0f, 0.707f, sampleRate);
    }

    // Scholz FET Compressor
    inline float processCompressor(float in) {
        float rect = fabsf(in);
        if (rect > compEnv) compEnv += compAtk * (rect - compEnv);
        else compEnv += compRel * (rect - compEnv);

        float gain = 1.0f;
        const float compThresh = 0.08f;
        if (compEnv > compThresh) {
            gain = 1.0f / (1.0f + (compEnv - compThresh) * 4.8f);
        }
        return in * gain;
    }

    // Op-Amp / Diode Multi-Stage Soft Saturation
    inline float softDistortion(float in, float gainFactor) {
        float x = in * gainFactor;
        float t = tanhf(x);
        return t - 0.04f * (t * t);
    }

    // BBD Chorus Delay
    inline float processChorus(float in, float depthNorm) {
        chorusBuffer[chorusWriteIdx] = in;

        float lfo = sinf(chorusPhase);
        chorusPhase += (2.0f * (float)M_PI * 0.45f) / (float)sampleRate; // ~0.45 Hz Boston chorus sweep
        if (chorusPhase > 2.0f * (float)M_PI) chorusPhase -= 2.0f * (float)M_PI;

        // Delay ~ 12ms to 18ms
        float delayMs = 15.0f + 3.0f * lfo * depthNorm;
        float delaySamples = delayMs * 0.001f * (float)sampleRate;
        float rIdx = (float)chorusWriteIdx - delaySamples;
        while (rIdx < 0.0f) rIdx += 2048.0f;

        int i0 = (int)rIdx;
        int i1 = (i0 + 1) & 2047;
        float frac = rIdx - (float)i0;
        float wet = chorusBuffer[i0] + frac * (chorusBuffer[i1] - chorusBuffer[i0]);

        chorusWriteIdx = (chorusWriteIdx + 1) & 2047;
        return 0.6f * in + 0.55f * wet;
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
            s = cabGbLow.process(s);
            s = cabGbMid.process(s);
            s = cabGbLp1.process(s);
            s = cabGbLp2.process(s);
            return s * 1.02f;
        } else {
            s = cabStudioLow.process(s);
            s = cabStudioMid.process(s);
            s = cabStudioLp1.process(s);
            s = cabStudioLp2.process(s);
            return s * 1.05f;
        }
    }

    void updateParams(bool bassBoost) {
        if (bassBoost) {
            bassBoostFilter.setLowShelf(100.0f, 4.5f, 0.7f, sampleRate);
        } else {
            bassBoostFilter.setIdentity();
        }
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pMode, float pVolume, float pChorus, float pChorusDepth,
        float pBassBoost, float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        int mode = (int)std::max(0.0f, std::min(3.0f, pMode));
        bool bassBoost = (pBassBoost > 0.5f);
        updateParams(bassBoost);

        int cabType = (int)std::max(0.0f, std::min(2.0f, pSpeakerCab));
        bool useChorus = (pChorus > 0.5f);
        float chorusDepthNorm = (pChorusDepth / 100.0f);
        bool useGate = (pNoiseGate > 0.5f);

        float outTrim = (pVolume / 10.0f) * 1.4f;

        // Mode gain factors
        // 0: Clean 1 (Pristine compressor clean)
        // 1: Clean 2 (Compressor with subtle warmth)
        // 2: Edge (Crunch power chord rhythm)
        // 3: Dist (High gain singing Boston lead)
        float modeDrive = 1.0f;
        if (mode == 0) modeDrive = 1.1f;
        else if (mode == 1) modeDrive = 1.7f;
        else if (mode == 2) modeDrive = 4.2f;
        else if (mode == 3) modeDrive = 8.5f;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 25.0f, sampleRate);

            // 1. Scholz FET Compressor
            s = processCompressor(s);

            // 2. Scholz Signature Mid Filters (800Hz & 2.1kHz)
            s = scholzMid1.process(s);
            s = scholzMid2.process(s);

            if (bassBoost) {
                s = bassBoostFilter.process(s);
            }

            // 3. Distortion Stage
            if (mode == 0) {
                // Pure clean
                s *= 1.2f;
            } else if (mode == 1) {
                s = softDistortion(s, modeDrive);
            } else {
                s = softDistortion(s, modeDrive);
                s = postDistLp.lp(s, 6800.0f, sampleRate);
                s = softDistortion(s, 1.8f);
            }

            // 4. Analog BBD Stereo Chorus
            if (useChorus) {
                s = processChorus(s, chorusDepthNorm);
            }

            s *= outTrim;

            // 5. Dynamic Cabinet Contouring
            s = processSpeaker(s, cabType, pSpeakerDrive);

            // 6. Smart Zero-Floor Noise Suppressor
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
                s *= gateGain;
            }

            output[i] = s;
        }
    }

private:
    double sampleRate;
    Biquad scholzMid1, scholzMid2, bassBoostFilter;
    Biquad cabStudioLow, cabStudioMid, cabStudioLp1, cabStudioLp2;
    Biquad cabGbLow, cabGbMid, cabGbLp1, cabGbLp2;
    OnePole postDistLp;
    OnePole dcBlockerPre;

    float compEnv, compAtk, compRel;

    float* chorusBuffer;
    int chorusWriteIdx;
    float chorusPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberRockmanX100LV2 {
    CyberRockmanX100* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* mode;
    const float* volume;
    const float* chorus;
    const float* chorusDepth;
    const float* bassBoost;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberRockmanX100LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), mode(nullptr), volume(nullptr), chorus(nullptr),
        chorusDepth(nullptr), bassBoost(nullptr), speakerCab(nullptr),
        speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberRockmanX100LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberRockmanX100LV2* handle = new CyberRockmanX100LV2();
    handle->dsp = new CyberRockmanX100(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberRockmanX100LV2* h = (CyberRockmanX100LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:       h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:      h->audioOut = (float*)data; break;
        case PORT_BYPASS:         h->bypass = (const float*)data; break;
        case PORT_MODE:           h->mode = (const float*)data; break;
        case PORT_VOLUME:         h->volume = (const float*)data; break;
        case PORT_CHORUS:         h->chorus = (const float*)data; break;
        case PORT_CHORUS_DEPTH:   h->chorusDepth = (const float*)data; break;
        case PORT_BASS_BOOST:     h->bassBoost = (const float*)data; break;
        case PORT_SPEAKER_CAB:    h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:  h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:     h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberRockmanX100LV2* h = (CyberRockmanX100LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->mode ? *h->mode : 3.0f,
        h->volume ? *h->volume : 7.0f,
        h->chorus ? *h->chorus : 1.0f,
        h->chorusDepth ? *h->chorusDepth : 75.0f,
        h->bassBoost ? *h->bassBoost : 0.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 4.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberRockmanX100LV2* h = (CyberRockmanX100LV2*)instance; if (h) delete h; }
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
