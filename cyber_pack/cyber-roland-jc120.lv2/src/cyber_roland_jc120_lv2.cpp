/*
 * Cyber Roland JC-120 Jazz Chorus - Legendary Solid-State Clean Amplifier LV2 Plugin
 * Authentic analog circuit emulation of the 1975 Roland JC-120,
 * featuring ultra-linear discrete solid-state preamp, classic diode distortion,
 * true BBD Dimensional Space Chorus & Vibrato, dual 60W power amps,
 * dual 12" Silver-Dome dynamic speaker stress, and Smart Zero-Floor Suppressor.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "lv2/lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-roland-jc120"

enum PortIndex {
    PORT_AUDIO_IN       = 0,
    PORT_AUDIO_OUT      = 1,
    PORT_BYPASS         = 2,
    PORT_VOLUME         = 3,
    PORT_BRIGHT         = 4,
    PORT_TREBLE         = 5,
    PORT_MIDDLE         = 6,
    PORT_BASS           = 7,
    PORT_DISTORTION     = 8,
    PORT_MOD_MODE       = 9,
    PORT_MOD_SPEED      = 10,
    PORT_MOD_DEPTH      = 11,
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

class CyberRolandJC120 {
public:
    CyberRolandJC120(double rate) : sampleRate(rate) {
        toneBass.setIdentity();
        toneMid.setIdentity();
        toneTreble.setIdentity();
        brightFilter.setIdentity();

        cabJcLow.setIdentity();
        cabJcMid.setIdentity();
        cabJcLp1.setIdentity();
        cabJcLp2.setIdentity();

        lfoPhase = 0.0f;
        bbdWritePos = 0;
        memset(bbdBuffer, 0, sizeof(bbdBuffer));

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
        // Roland Silver-Dome 2x12 (Ultra-wide transparent bandwidth, 65Hz punch, 3.8kHz crystalline sparkle, 6.2kHz cut)
        cabJcLow.setPeaking(70.0f, 4.5f, 1.1f, sampleRate);
        cabJcMid.setPeaking(3800.0f, 4.2f, 1.4f, sampleRate);
        cabJcLp1.setLowPass(6200.0f, 0.707f, sampleRate);
        cabJcLp2.setLowPass(6200.0f, 0.707f, sampleRate);
    }

    // Discrete Bipolar Transistor Clean Preamp Stage
    inline float solidStatePreamp(float in, float gain) {
        float x = in * gain;
        // Hyper-linear up to rails, then smooth saturation
        if (x > 1.2f) return 1.2f + 0.15f * tanhf(x - 1.2f);
        if (x < -1.2f) return -1.2f + 0.15f * tanhf(x + 1.2f);
        return x;
    }

    // Classic JC-120 Diode Distortion Circuit
    inline float diodeDistortion(float in, float drive) {
        if (drive < 0.1f) return in;
        float x = in * (1.0f + drive * 2.5f);
        // Germanium clipping threshold at 0.35V
        float t = tanhf(x * 1.8f);
        return (t - 0.08f * (t * t)) * 0.8f;
    }

    // 120W Solid-State Power Amp (No sag, instant punch)
    inline float powerAmpStage(float in) {
        // High headroom linear power delivery
        return in * 1.15f;
    }

    inline float processSpeaker(float in, int cabType, float driveVal) {
        if (cabType == 1) return in; // Direct bypass

        float s = in;
        if (driveVal > 0.05f) {
            float driveAmount = driveVal / 10.0f;
            float rect = fabsf(s);
            if (rect > speakerEnv) speakerEnv += spkAtk * (rect - speakerEnv);
            else speakerEnv += spkRel * (rect - speakerEnv);
            speakerThermalEnv += spkThermalRel * (speakerEnv - speakerThermalEnv);

            float comp = 1.0f / (1.0f + speakerEnv * driveAmount * 1.2f);
            float thermalComp = 1.0f / (1.0f + speakerThermalEnv * driveAmount * 0.20f);
            s = s * comp * thermalComp;

            float coneStress = s * (1.0f + driveAmount * 1.15f);
            float t = tanhf(coneStress);
            float coneOut = (t - 0.03f * (t * t)) / (1.0f + driveAmount * 0.20f);

            float dampingFc = 7200.0f - driveAmount * 1400.0f;
            float w = 2.0f * (float)M_PI * dampingFc / (float)sampleRate;
            float a0 = w / (1.0f + w);
            speakerConeHistory += a0 * (coneOut - speakerConeHistory);

            s = (1.0f - driveAmount * 0.85f) * s + (driveAmount * 0.85f) * speakerConeHistory;
            s *= (1.0f + driveAmount * 0.20f);
        }

        s = cabJcLow.process(s);
        s = cabJcMid.process(s);
        s = cabJcLp1.process(s);
        s = cabJcLp2.process(s);
        return s * 1.05f;
    }

    inline float audioTaper(float val0to10, float maxGain) {
        if (val0to10 <= 0.05f) return 0.0f;
        float norm = val0to10 / 10.0f;
        return (0.22f * norm + 0.78f * norm * norm * norm) * (maxGain * 1.55f);
    }

    void updateParams(float bass, float mid, float treb, float bright) {
        float bassDb = -2.0f + (bass / 10.0f) * 15.0f;
        toneBass.setLowShelf(100.0f, bassDb, 0.7f, sampleRate);

        float midDb = (mid - 5.0f) * 2.8f;
        toneMid.setPeaking(800.0f, midDb, 1.1f, sampleRate);

        float trebDb = (treb - 5.0f) * 2.8f;
        toneTreble.setHighShelf(3500.0f, trebDb, 0.7f, sampleRate);

        if (bright > 0.5f) {
            brightFilter.setHighShelf(4200.0f, 6.5f, 0.7f, sampleRate);
        } else {
            brightFilter.setIdentity();
        }
    }

    void process(
        const float* input, float* output, uint32_t n_samples,
        float bypass, float pVolume, float pBright, float pTreble, float pMiddle, float pBass,
        float pDist, float pModMode, float pModSpeed, float pModDepth,
        float pSpeakerCab, float pSpeakerDrive, float pNoiseGate, float pOutputLevel
    ) {
        if (bypass < 0.5f) {
            if (output != input) memcpy(output, input, n_samples * sizeof(float));
            return;
        }

        updateParams(pBass, pMiddle, pTreble, pBright);

        int modMode = (int)std::max(0.0f, std::min(2.0f, pModMode));
        int cabType = (int)std::max(0.0f, std::min(1.0f, pSpeakerCab));
        bool useGate = (pNoiseGate > 0.5f);

        float preGain = audioTaper(pVolume, 2.8f);
        float distDrive = pDist / 10.0f;
        float lfoInc = (2.0f * (float)M_PI * pModSpeed) / (float)sampleRate;
        float modDepthNorm = pModDepth / 100.0f;

        for (uint32_t i = 0; i < n_samples; ++i) {
            float rawIn = input[i];
            float s = rawIn;

            s = dcBlockerPre.hp(s, 20.0f, sampleRate);

            // Solid-State High Headroom Preamp
            s = brightFilter.process(s);
            s = solidStatePreamp(s, preGain);

            // Diode Distortion
            if (distDrive > 0.01f) {
                s = diodeDistortion(s, distDrive);
            }

            // 3-Band Linear Equalizer
            s = toneBass.process(s);
            s = toneMid.process(s);
            s = toneTreble.process(s);

            // Dimensional Space Chorus / Vibrato (BBD Analog Delay Line)
            if (modMode > 0) {
                bbdBuffer[bbdWritePos] = s;

                float lfo = sinf(lfoPhase);
                lfoPhase += lfoInc;
                if (lfoPhase > 2.0f * (float)M_PI) lfoPhase -= 2.0f * (float)M_PI;

                // Center delay: 7ms (chorus) / 5ms (vibrato), modulated by +/- 2.5ms
                float baseDelay = (modMode == 2) ? 0.007f : 0.005f;
                float modDelayMs = baseDelay + lfo * 0.0025f * modDepthNorm;
                float delaySamples = modDelayMs * (float)sampleRate;

                float readPos = (float)bbdWritePos - delaySamples;
                while (readPos < 0.0f) readPos += (float)BBD_BUF_SIZE;
                int rIdx0 = (int)readPos;
                int rIdx1 = (rIdx0 + 1) % BBD_BUF_SIZE;
                float frac = readPos - (float)rIdx0;
                float bbdOut = bbdBuffer[rIdx0] + frac * (bbdBuffer[rIdx1] - bbdBuffer[rIdx0]);

                bbdWritePos = (bbdWritePos + 1) % BBD_BUF_SIZE;

                if (modMode == 1) {
                    // Vibrato: 100% wet pitch-modulated signal
                    s = bbdOut;
                } else {
                    // Dimensional Chorus: Dry + BBD Wet sum
                    s = (s + bbdOut) * 0.72f;
                }
            }

            // 120W Solid-State Power Amp
            float pOut = powerAmpStage(s);
            pOut = dcBlockerPost.hp(pOut, 18.0f, sampleRate);

            // Dynamic 2x12 Silver-Dome Speaker Simulation
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
    Biquad toneBass, toneMid, toneTreble, brightFilter;
    Biquad cabJcLow, cabJcMid, cabJcLp1, cabJcLp2;
    OnePole dcBlockerPre, dcBlockerPost;

    static const int BBD_BUF_SIZE = 2048;
    float bbdBuffer[BBD_BUF_SIZE];
    int bbdWritePos;
    float lfoPhase;

    float speakerEnv, speakerThermalEnv, speakerConeHistory;
    float spkAtk, spkRel, spkThermalRel;

    OnePole gateScHp, gateScLp;
    float gateEnv, gateGain, gateAtk, gateRel, gateAtkSmooth, gateRelSmooth;
    bool gateIsOpen;
};

struct CyberRolandJC120LV2 {
    CyberRolandJC120* dsp;
    const float* audioIn;
    float* audioOut;
    const float* bypass;
    const float* volume;
    const float* bright;
    const float* treble;
    const float* middle;
    const float* bass;
    const float* distortion;
    const float* modMode;
    const float* modSpeed;
    const float* modDepth;
    const float* speakerCab;
    const float* speakerDrive;
    const float* noiseGate;

    const float* outputLevel;
    CyberRolandJC120LV2() : dsp(nullptr), audioIn(nullptr), audioOut(nullptr),
        bypass(nullptr), volume(nullptr), bright(nullptr), treble(nullptr), middle(nullptr),
        bass(nullptr), distortion(nullptr), modMode(nullptr), modSpeed(nullptr), modDepth(nullptr),
        speakerCab(nullptr), speakerDrive(nullptr), noiseGate(nullptr) , outputLevel(nullptr) {}
    ~CyberRolandJC120LV2() { if (dsp) delete dsp; }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    CyberRolandJC120LV2* handle = new CyberRolandJC120LV2();
    handle->dsp = new CyberRolandJC120(rate);
    return (LV2_Handle)handle;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    CyberRolandJC120LV2* h = (CyberRolandJC120LV2*)instance;
    switch ((PortIndex)port) {
        case PORT_AUDIO_IN:     h->audioIn = (const float*)data; break;
        case PORT_AUDIO_OUT:    h->audioOut = (float*)data; break;
        case PORT_BYPASS:       h->bypass = (const float*)data; break;
        case PORT_VOLUME:       h->volume = (const float*)data; break;
        case PORT_BRIGHT:       h->bright = (const float*)data; break;
        case PORT_TREBLE:       h->treble = (const float*)data; break;
        case PORT_MIDDLE:       h->middle = (const float*)data; break;
        case PORT_BASS:         h->bass = (const float*)data; break;
        case PORT_DISTORTION:   h->distortion = (const float*)data; break;
        case PORT_MOD_MODE:     h->modMode = (const float*)data; break;
        case PORT_MOD_SPEED:    h->modSpeed = (const float*)data; break;
        case PORT_MOD_DEPTH:    h->modDepth = (const float*)data; break;
        case PORT_SPEAKER_CAB:  h->speakerCab = (const float*)data; break;
        case PORT_SPEAKER_DRIVE:h->speakerDrive = (const float*)data; break;
        case PORT_NOISE_GATE:   h->noiseGate = (const float*)data; break;
        case PORT_OUTPUT_LEVEL:   h->outputLevel = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t n_samples) {
    CyberRolandJC120LV2* h = (CyberRolandJC120LV2*)instance;
    if (!h || !h->dsp || !h->audioIn || !h->audioOut) return;

    h->dsp->process(
        h->audioIn, h->audioOut, n_samples,
        h->bypass ? *h->bypass : 1.0f,
        h->volume ? *h->volume : 5.0f,
        h->bright ? *h->bright : 0.0f,
        h->treble ? *h->treble : 5.5f,
        h->middle ? *h->middle : 5.0f,
        h->bass ? *h->bass : 5.0f,
        h->distortion ? *h->distortion : 0.0f,
        h->modMode ? *h->modMode : 2.0f,
        h->modSpeed ? *h->modSpeed : 3.5f,
        h->modDepth ? *h->modDepth : 60.0f,
        h->speakerCab ? *h->speakerCab : 0.0f,
        h->speakerDrive ? *h->speakerDrive : 3.5f,
        h->noiseGate ? *h->noiseGate : 1.0f, h->outputLevel ? *h->outputLevel : 7.0f
    );
}

static void deactivate(LV2_Handle instance) {}
static void cleanup(LV2_Handle instance) { CyberRolandJC120LV2* h = (CyberRolandJC120LV2*)instance; if (h) delete h; }
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
