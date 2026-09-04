#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace AudioDSP {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Fast linear to dB and dB to linear
inline float dbToLin(float db) {
    if (db <= -60.0f) return 0.0f;
    return std::pow(10.0f, db * 0.05f);
}

// Simple State Variable Filter (SVF) for EQ and tone shaping
class StateVariableFilter {
public:
    void reset() {
        s1 = 0.0f;
        s2 = 0.0f;
    }

    void setParameters(float cutoffHz, float q, float sampleRate) {
        cutoffHz = std::max(20.0f, std::min(cutoffHz, sampleRate * 0.45f));
        q = std::max(0.2f, std::min(q, 10.0f));
        float g = std::tan(static_cast<float>(M_PI) * cutoffHz / sampleRate);
        float k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        this->k = k;
    }

    float processLowpass(float in) {
        float v3 = in - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return v2;
    }

    float processHighpass(float in) {
        float v3 = in - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return in - k * v1 - v2;
    }

    float processBandpass(float in) {
        float v3 = in - s2;
        float v1 = a1 * s1 + a2 * v3;
        float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return v1;
    }

private:
    float s1{0.0f};
    float s2{0.0f};
    float k{1.0f};
    float a1{0.0f};
    float a2{0.0f};
    float a3{0.0f};
};

class CyberStompBoxEngine {
public:
    enum SoundModel {
        MODEL_SUB_STOMP = 0,
        MODEL_CAJON     = 1,
        MODEL_ROCK_KICK = 2,
        MODEL_808_BOOM  = 3,
        MODEL_TAMBOURINE = 4,
        MODEL_FOOT_SNARE = 5
    };

    void init(double sRate) {
        sampleRate = static_cast<float>(sRate > 8000.0 ? sRate : 48000.0);
        reset();
    }

    void reset() {
        phaseOsc1 = 0.0f;
        phaseOsc2 = 0.0f;
        phaseClick = 0.0f;
        
        envAmp = 0.0f;
        envPitch = 0.0f;
        envClick = 0.0f;
        envNoise = 0.0f;
        
        bodyFilter.reset();
        subFilter.reset();
        toneFilter.reset();
        noiseFilter.reset();
        
        debounceCounter = 0;
        ledDecay = 0.0f;
        noiseSeed = 22222;
    }

    // Trigger the stomp with a specific velocity (0.0 to 1.0)
    void trigger(float vel = 1.0f) {
        if (debounceCounter > 0) return; // Debounce guard against mechanical switch bounce

        vel = std::max(0.05f, std::min(vel, 1.0f));
        currentVelocity = fixedVelocityMode ? 1.0f : vel;

        // Smoothly restart envelopes
        envAmp = currentVelocity;
        envPitch = 1.0f;
        envClick = currentVelocity * punchAmount;
        envNoise = currentVelocity;

        // Reset oscillator phases smoothly
        phaseOsc1 = 0.0f;
        phaseOsc2 = 0.0f;
        phaseClick = 0.0f;

        // Reset debounce guard (~35ms)
        debounceCounter = static_cast<int>(sampleRate * 0.035f);

        // Flash activity LED
        ledDecay = 1.0f;
    }

    // Parameter Setters
    void setModel(int model) {
        activeModel = static_cast<SoundModel>(std::max(0, std::min(model, 5)));
    }

    void setPitchSemitones(float semitones) {
        pitchMultiplier = std::pow(2.0f, semitones / 12.0f);
    }

    void setDecayTime(float seconds) {
        decaySec = std::max(0.05f, std::min(seconds, 2.0f));
    }

    void setPunch(float punch) {
        punchAmount = std::max(0.0f, std::min(punch, 1.0f));
    }

    void setSubThumpDb(float db) {
        subThumpGain = dbToLin(db);
    }

    void setTone(float tone) {
        toneAmount = std::max(0.0f, std::min(tone, 1.0f));
    }

    void setFixedVelocity(bool fixed) {
        fixedVelocityMode = fixed;
    }

    void setGuitarGainDb(float db) {
        guitarGain = dbToLin(db);
    }

    void setStompGainDb(float db) {
        stompGain = dbToLin(db);
    }

    void setSplitOutput(bool split) {
        splitOutputMode = split;
    }

    float getActivityLed() const {
        return ledDecay;
    }

    // Process a block of audio
    void process(const float* inL, const float* inR,
                 float* outL, float* outR,
                 uint32_t numSamples)
    {
        // Decay coefficients per sample
        float ampDecayRate = std::exp(-1.0f / (sampleRate * decaySec));
        float pitchDecayRate = std::exp(-1.0f / (sampleRate * (0.025f + decaySec * 0.03f)));
        float clickDecayRate = std::exp(-1.0f / (sampleRate * 0.008f));
        float noiseDecayRate = std::exp(-1.0f / (sampleRate * (activeModel == MODEL_TAMBOURINE ? 0.18f : 0.09f)));

        // Base frequency according to model
        float baseFreq = 50.0f;
        float startPitchOffset = 110.0f;

        switch (activeModel) {
            case MODEL_SUB_STOMP:
                baseFreq = 46.0f * pitchMultiplier;
                startPitchOffset = 95.0f * pitchMultiplier;
                break;
            case MODEL_CAJON:
                baseFreq = 62.0f * pitchMultiplier;
                startPitchOffset = 80.0f * pitchMultiplier;
                break;
            case MODEL_ROCK_KICK:
                baseFreq = 54.0f * pitchMultiplier;
                startPitchOffset = 140.0f * pitchMultiplier;
                break;
            case MODEL_808_BOOM:
                baseFreq = 42.0f * pitchMultiplier;
                startPitchOffset = 160.0f * pitchMultiplier;
                ampDecayRate = std::exp(-1.0f / (sampleRate * decaySec * 1.5f));
                break;
            case MODEL_TAMBOURINE:
                baseFreq = 220.0f;
                startPitchOffset = 0.0f;
                break;
            case MODEL_FOOT_SNARE:
                baseFreq = 165.0f * pitchMultiplier;
                startPitchOffset = 100.0f * pitchMultiplier;
                break;
        }

        // Setup dynamic filters based on tone and sub thump
        float toneCutoff = 400.0f + toneAmount * 9000.0f;
        toneFilter.setParameters(toneCutoff, 0.707f, sampleRate);
        subFilter.setParameters(52.0f * pitchMultiplier, 1.2f, sampleRate);
        
        float bodyCutoff = (activeModel == MODEL_CAJON) ? 125.0f : 85.0f;
        bodyFilter.setParameters(bodyCutoff * pitchMultiplier, 1.8f, sampleRate);

        if (activeModel == MODEL_TAMBOURINE) {
            noiseFilter.setParameters(4500.0f + toneAmount * 2500.0f, 1.4f, sampleRate);
        } else if (activeModel == MODEL_FOOT_SNARE) {
            noiseFilter.setParameters(2200.0f + toneAmount * 3000.0f, 0.9f, sampleRate);
        }

        for (uint32_t i = 0; i < numSamples; ++i) {
            if (debounceCounter > 0) debounceCounter--;
            if (ledDecay > 0.0f) {
                ledDecay -= (1.0f / (sampleRate * 0.12f)); // LED stays lit ~120ms
                if (ledDecay < 0.0f) ledDecay = 0.0f;
            }

            float stompSample = 0.0f;

            if (envAmp > 0.00005f) {
                if (activeModel == MODEL_TAMBOURINE) {
                    // Metallic jingle synthesis
                    float whiteNoise = nextNoise();
                    float highJingle = noiseFilter.processHighpass(whiteNoise);
                    float ringMod = std::sin(phaseOsc1 * 2.0f * static_cast<float>(M_PI)) *
                                    std::sin(phaseOsc2 * 2.0f * static_cast<float>(M_PI));

                    stompSample = (highJingle * 0.7f + ringMod * 0.3f) * envNoise;

                    phaseOsc1 += (4800.0f / sampleRate);
                    if (phaseOsc1 >= 1.0f) phaseOsc1 -= 1.0f;
                    phaseOsc2 += (7200.0f / sampleRate);
                    if (phaseOsc2 >= 1.0f) phaseOsc2 -= 1.0f;

                } else if (activeModel == MODEL_FOOT_SNARE) {
                    // Snare: tonal shell pop + wire sizzle
                    float oscFreq = baseFreq + envPitch * startPitchOffset;
                    float shell = std::sin(phaseOsc1 * 2.0f * static_cast<float>(M_PI)) * envAmp;
                    float whiteNoise = nextNoise();
                    float wires = noiseFilter.processBandpass(whiteNoise) * envNoise;

                    stompSample = (shell * 0.5f + wires * 0.8f + envClick * 0.4f);

                    phaseOsc1 += (oscFreq / sampleRate);
                    if (phaseOsc1 >= 1.0f) phaseOsc1 -= 1.0f;

                } else {
                    // Kick & Stomp Synthesis:
                    float currentFreq = baseFreq + envPitch * startPitchOffset;
                    currentFreq = std::min(currentFreq, sampleRate * 0.45f);

                    // Dual oscillator for rich punch
                    float sine1 = std::sin(phaseOsc1 * 2.0f * static_cast<float>(M_PI));
                    float sine2 = std::sin(phaseOsc2 * 2.0f * static_cast<float>(M_PI));
                    
                    float oscMix = (sine1 * 0.85f + sine2 * 0.15f) * envAmp;

                    // Click / Beater transient
                    float click = (std::sin(phaseClick * 2.0f * static_cast<float>(M_PI)) * 0.5f + nextNoise() * 0.5f) * envClick;
                    
                    // Combine & shape through body + tone filters
                    float body = bodyFilter.processBandpass(oscMix);
                    float sub = subFilter.processBandpass(sine1 * envAmp) * subThumpGain;
                    
                    stompSample = (oscMix + body * 0.4f + sub * 0.6f + click * punchAmount * 0.5f);

                    // Update oscillator phases
                    phaseOsc1 += (currentFreq / sampleRate);
                    if (phaseOsc1 >= 1.0f) phaseOsc1 -= 1.0f;

                    phaseOsc2 += ((currentFreq * 1.98f) / sampleRate);
                    if (phaseOsc2 >= 1.0f) phaseOsc2 -= 1.0f;

                    phaseClick += (1200.0f / sampleRate);
                    if (phaseClick >= 1.0f) phaseClick -= 1.0f;
                }

                // Apply Tone Filter & Warm Saturation
                stompSample = toneFilter.processLowpass(stompSample);
                stompSample = std::tanh(stompSample * 1.3f) * 0.85f; // Musical soft-clip
                stompSample *= stompGain;

                // Decay envelopes
                envAmp *= ampDecayRate;
                envPitch *= pitchDecayRate;
                envClick *= clickDecayRate;
                envNoise *= noiseDecayRate;
            }

            // Input handling (pass guitar through)
            float inSampleL = inL ? inL[i] * guitarGain : 0.0f;
            float inSampleR = inR ? inR[i] * guitarGain : inSampleL;

            if (splitOutputMode) {
                // Split Mode:
                // Left output = Clean Guitar
                // Right output = Stomp Box only (Direct to PA / Subwoofer!)
                outL[i] = inSampleL;
                outR[i] = stompSample;
            } else {
                // Mix Mode:
                // Guitar and Stomp mixed together on both outputs
                outL[i] = inSampleL + stompSample;
                outR[i] = inSampleR + stompSample;
            }
        }
    }

private:
    float nextNoise() {
        noiseSeed = (noiseSeed * 196314165 + 907633515);
        return static_cast<float>(static_cast<int32_t>(noiseSeed)) / 2147483648.0f;
    }

    float sampleRate{48000.0f};

    // State & Envelopes
    float phaseOsc1{0.0f};
    float phaseOsc2{0.0f};
    float phaseClick{0.0f};

    float envAmp{0.0f};
    float envPitch{0.0f};
    float envClick{0.0f};
    float envNoise{0.0f};

    float currentVelocity{1.0f};
    int debounceCounter{0};
    float ledDecay{0.0f};
    uint32_t noiseSeed{12345};

    // Filters
    StateVariableFilter bodyFilter;
    StateVariableFilter subFilter;
    StateVariableFilter toneFilter;
    StateVariableFilter noiseFilter;

    // Parameters
    SoundModel activeModel{MODEL_SUB_STOMP};
    float pitchMultiplier{1.0f};
    float decaySec{0.35f};
    float punchAmount{0.7f};
    float subThumpGain{1.41f}; // ~+3dB
    float toneAmount{0.5f};
    bool fixedVelocityMode{true};
    float guitarGain{1.0f};
    float stompGain{1.0f};
    bool splitOutputMode{false};
};

} // namespace AudioDSP
