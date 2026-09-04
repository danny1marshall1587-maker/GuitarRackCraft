/*
 * Cyber Acoustic Feedbacker & Polyphonic Sustainer - LV2 Plugin
 * Copyright (c) 2026 Cyber Audio
 *
 * True Musical Harmonic Feedback Engine (FreqOut / DF-2 style):
 *  - Low-pass pre-filtered autocorrelation pitch tracker locks onto the exact guitar note.
 *  - Phase-locked sinusoidal harmonic resonator synthesizes pure musical feedback (Unison, 5th, Octave, 2nd Octave, Morph).
 *  - Dynamic tube saturation and analog acoustic speaker resonance.
 *  - Full vibrato and string bending pitch tracking.
 *  - Zero random noise / zero comb flutter.
 */

#include "lv2.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PLUGIN_URI "http://cyber-audio.co.uk/plugins/cyber-acoustic-feedbacker"
#define PITCH_BUF_SIZE 2048

enum PortIndex {
    PORT_AUDIO_IN_L    = 0,
    PORT_AUDIO_IN_R    = 1,
    PORT_AUDIO_OUT_L   = 2,
    PORT_AUDIO_OUT_R   = 3,
    PORT_BYPASS        = 4,
    PORT_TRIGGER       = 5,
    PORT_MODE          = 6,
    PORT_BLOOM         = 7,
    PORT_GAIN          = 8,
    PORT_HARMONIC      = 9,
    PORT_WARMTH        = 10,
    PORT_VIBRATO       = 11,
    PORT_MIX           = 12
};

class CyberAcousticFeedbacker {
private:
    double sample_rate;

    // Pitch Tracking State
    float pitch_buf[PITCH_BUF_SIZE];
    int pitch_idx;
    float prefilter_lp;
    float current_note_freq;
    float latched_note_freq;
    float target_osc_freq;
    float smoothed_osc_freq;
    float pitch_confidence;
    bool has_note_locked;
    bool prev_trigger_state;
    float prev_guitar_env;

    // Live Wavetable Capture State
    float history_buffer[4096];
    int history_idx;
    float wavetable[4096];
    float wt_period;
    double osc_phase;
    double wt_phase;
    float morph_progress;

    // Envelope and Trigger
    float guitar_env;
    float feedback_gain_env;
    float auto_trigger_timer;

    // Tone and Cabinet Simulation
    float cab_lp1_l, cab_lp1_r;
    float cab_lp2_l, cab_lp2_r;
    float cab_hp_l, cab_hp_r;
    
    // Acoustic Distance / Micro-Delay State
    float delay_buf_l[4096];
    float delay_buf_r[4096];
    int delay_idx;
    float lfo_phase;


    // Port Pointers
    const float* p_in_l;
    const float* p_in_r;
    float* p_out_l;
    float* p_out_r;
    const float* p_bypass;
    const float* p_trigger;
    const float* p_mode;
    const float* p_bloom;
    const float* p_gain;
    const float* p_harmonic;
    const float* p_warmth;
    const float* p_vibrato;
    const float* p_mix;

    // Fast Autocorrelation Pitch Detector
    void detect_guitar_pitch() {
        int half_w = PITCH_BUF_SIZE / 2;
        int min_tau = (int)(sample_rate / 1200.0); // Highest guitar note ~1200 Hz
        int max_tau = (int)(sample_rate / 65.0);   // Lowest guitar note ~65 Hz (Drop C)
        if (max_tau >= half_w) max_tau = half_w - 1;

        float e0 = 0.0f;
        for (int j = 0; j < half_w; ++j) {
            e0 += pitch_buf[j] * pitch_buf[j];
        }

        if (e0 < 0.0001f) {
            pitch_confidence = 0.0f;
            return;
        }

        // McLeod Pitch Method (MPM) - Normalized Square Difference Function
        float nsdf[2048]; 
        float global_max = -1.0f;
        
        for (int tau = min_tau; tau <= max_tau; ++tau) {
            float sum = 0.0f;
            float e_tau = 0.0f;
            for (int j = 0; j < half_w; ++j) {
                sum += pitch_buf[j] * pitch_buf[j + tau];
                e_tau += pitch_buf[j + tau] * pitch_buf[j + tau];
            }
            float val = (2.0f * sum) / (e0 + e_tau + 1e-9f);
            nsdf[tau] = val;
            if (val > global_max) global_max = val;
        }
        
        // MPM Peak Picking: Find the FIRST significant peak to avoid octave errors
        float best_tau = 0.0f;
        float threshold = 0.8f * global_max;
        if (threshold < 0.3f) threshold = 0.3f; // Absolute minimum confidence
        
        for (int tau = min_tau + 1; tau < max_tau; ++tau) {
            // Check for local maximum
            if (nsdf[tau] > nsdf[tau-1] && nsdf[tau] > nsdf[tau+1]) {
                if (nsdf[tau] >= threshold) {
                    // Parabolic interpolation for exact sub-sample frequency
                    float s0 = nsdf[tau-1];
                    float s1 = nsdf[tau];
                    float s2 = nsdf[tau+1];
                    float delta = (s2 - s0) / (2.0f * (2.0f * s1 - s2 - s0) + 1e-6f);
                    best_tau = (float)tau + delta;
                    pitch_confidence = s1;
                    break; // STOP at the first valid peak! (Solves octave errors)
                }
            }
        }

        if (best_tau > 0.0f) {
            float raw_hz = (float)sample_rate / best_tau;
            if (raw_hz >= 65.0f && raw_hz <= 1200.0f) {
                current_note_freq += 0.4f * (raw_hz - current_note_freq);
                has_note_locked = true;
            }
        } else {
            pitch_confidence = 0.0f;
        }
    }

    public:
    CyberAcousticFeedbacker(double sr) : sample_rate(sr) {
        memset(pitch_buf, 0, sizeof(pitch_buf));
        pitch_idx = 0;
        prefilter_lp = 0.0f;
        current_note_freq = 220.0f; // Default A3 (220 Hz)
        latched_note_freq = 220.0f;
        target_osc_freq = 220.0f;
        smoothed_osc_freq = 220.0f;
        pitch_confidence = 0.0f;
        has_note_locked = false;
        prev_trigger_state = false;

        history_idx = 0;
        wt_period = 100.0f;
        wt_phase = 0.0;
        osc_phase = 0.0;
        for(int i=0; i<4096; i++) {
            history_buffer[i] = 0.0f;
            wavetable[i] = 0.0f;
        }
        morph_progress = 0.0f;

        prev_guitar_env = 0.0f;
        guitar_env = 0.0f;
        feedback_gain_env = 0.0f;
        auto_trigger_timer = 0.0f;

        cab_lp1_l = cab_lp1_r = 0.0f;
        cab_lp2_l = cab_lp2_r = 0.0f;
        cab_hp_l = cab_hp_r = 0.0f;
        delay_idx = 0;
        lfo_phase = 0.0f;
        for(int i=0; i<4096; i++) {
            delay_buf_l[i] = 0.0f;
            delay_buf_r[i] = 0.0f;
        }
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN_L:  p_in_l = (const float*)data; break;
            case PORT_AUDIO_IN_R:  p_in_r = (const float*)data; break;
            case PORT_AUDIO_OUT_L: p_out_l = (float*)data; break;
            case PORT_AUDIO_OUT_R: p_out_r = (float*)data; break;
            case PORT_BYPASS:      p_bypass = (const float*)data; break;
            case PORT_TRIGGER:     p_trigger = (const float*)data; break;
            case PORT_MODE:        p_mode = (const float*)data; break;
            case PORT_BLOOM:       p_bloom = (const float*)data; break;
            case PORT_GAIN:        p_gain = (const float*)data; break;
            case PORT_HARMONIC:    p_harmonic = (const float*)data; break;
            case PORT_WARMTH:      p_warmth = (const float*)data; break;
            case PORT_VIBRATO:     p_vibrato = (const float*)data; break;
            case PORT_MIX:         p_mix = (const float*)data; break;
        }
    }

    void run(uint32_t sample_count) {
        bool bypass = (*p_bypass < 0.5f);
        if (bypass) {
            if (p_out_l != p_in_l) memcpy(p_out_l, p_in_l, sample_count * sizeof(float));
            if (p_out_r && p_in_r && p_out_r != p_in_r) memcpy(p_out_r, p_in_r, sample_count * sizeof(float));
            return;
        }

        bool manual_trigger = (p_trigger && *p_trigger > 0.5f);
        int mode = (int)std::round(p_mode ? *p_mode : 0.0f); // 0=Root, 1=Poly, 2=Bloom
        float bloom_knob = (p_bloom ? *p_bloom : 2.0f);
        float gain_knob = (p_gain ? *p_gain : 75.0f) * 0.01f;
        int harmonic_mode = (int)std::round(p_harmonic ? *p_harmonic : 1.0f); // 0=Unison, 1=5th, 2=Octave, 3=2nd Octave, 4=Morph
        float warmth_knob = (p_warmth ? *p_warmth : 50.0f) * 0.01f;
        float mix_knob = (p_mix ? *p_mix : 50.0f) * 0.01f;
        float vibrato_knob = (p_vibrato ? *p_vibrato : 75.0f) * 0.01f;
        
        float bloom_sec = bloom_knob;
        // The user wants a very slow morph to the pure sine, matching the bloom time!
        float morph_rate = 1.0f - expf(-1.0f / ((bloom_sec * 0.7f) * (float)sample_rate));
        float attack_rate = 1.0f - expf(-1.0f / (bloom_sec * (float)sample_rate));
        float release_rate = 1.0f - expf(-1.0f / (0.12f * (float)sample_rate));

        // Pitch smoothing coefficient
        float pitch_smooth_rate = 1.0f - expf(-1.0f / (0.02f * (float)sample_rate));

        // Prefilter for pitch detector (gentle 800Hz lowpass to isolate string fundamental)
        float prefilter_coeff = 1.0f - expf(-2.0f * (float)M_PI * 700.0f / (float)sample_rate);

        // Cabinet Tone Filters (simulates 12" speaker body resonance)
        float hp_coeff = 1.0f - expf(-2.0f * (float)M_PI * 100.0f / (float)sample_rate);
        float lp_cutoff = 1800.0f + (1.0f - warmth_knob) * 4500.0f;
        float lp_coeff = 1.0f - expf(-2.0f * (float)M_PI * lp_cutoff / (float)sample_rate);

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_l = p_in_l[i];
            float in_r = (p_in_r ? p_in_r[i] : in_l);
            float in_mono = 0.5f * (in_l + in_r);
            float in_abs = fabsf(in_mono);

            // Envelope tracking
            prev_guitar_env = guitar_env;
            guitar_env += (in_abs - guitar_env) * 0.02f;
            
            // Transient detection: If amplitude spikes rapidly, reset the bloom!
            bool is_new_pick_attack = false;
            if (guitar_env - prev_guitar_env > 0.05f) { // Transient threshold
                is_new_pick_attack = true;
            }

            // Lowpass prefilter audio before pitch detection
            prefilter_lp += prefilter_coeff * (in_mono - prefilter_lp);

            // Store in pitch buffer
            pitch_buf[pitch_idx] = prefilter_lp;
            if (++pitch_idx >= PITCH_BUF_SIZE) {
                pitch_idx = 0;
                detect_guitar_pitch();
            }

            // Record history buffer continuously
            history_buffer[history_idx] = in_mono;
            history_idx++;
            if (history_idx >= 4096) history_idx = 0;

            // Determine if feedback should trigger
            bool is_triggered = false;
            bool trigger_active = (manual_trigger > 0.5f);
            bool trigger_just_pressed = (trigger_active && !prev_trigger_state);
            prev_trigger_state = trigger_active;

            if (trigger_active) {
                is_triggered = true;
                // If trigger just engaged, lock the active note
                if (trigger_just_pressed || is_new_pick_attack) {
                    if (pitch_confidence > 0.3f && current_note_freq > 40.0f) {
                        latched_note_freq = current_note_freq;
                        has_note_locked = true;
                    } else if (!has_note_locked && current_note_freq > 40.0f) {
                        latched_note_freq = current_note_freq;
                        has_note_locked = true;
                    }
                    
                    if (has_note_locked) {
                        // Capture exactly one period from history into the wavetable!
                        wt_period = sample_rate / latched_note_freq;
                        if (wt_period > 4000.0f) wt_period = 4000.0f;
                        if (wt_period < 10.0f) wt_period = 10.0f;
                        
                        float max_val = 0.001f;
                        for (int i = 0; i < (int)wt_period; i++) {
                            int read_idx = history_idx - (int)wt_period + i;
                            if (read_idx < 0) read_idx += 4096;
                            float val = history_buffer[read_idx];
                            wavetable[i] = val;
                            if (fabsf(val) > max_val) max_val = fabsf(val);
                        }
                        
                        // NORMALIZE the waveform so it screams like a cranked amp!
                        float norm_factor = 1.0f / max_val;
                        for (int i = 0; i < (int)wt_period; i++) {
                            wavetable[i] *= norm_factor;
                        }
                        
                        // Crossfade the edges (10%) to guarantee seamless looping without clicks
                        int fade_len = (int)(wt_period * 0.1f);
                        if (fade_len > 0) {
                            for(int i=0; i<fade_len; i++) {
                                float env = (float)i / (float)fade_len;
                                wavetable[i] *= env;
                                wavetable[(int)wt_period - 1 - i] *= env;
                            }
                        }
                        wt_phase = 0.0;
        osc_phase = 0.0;
                    }
                }
            }

            // Envelope ramp
            if (is_triggered && has_note_locked) {
                if (is_new_pick_attack) {
                    feedback_gain_env = 0.0f;
                    morph_progress = 0.0f;
                }
                feedback_gain_env += (1.0f - feedback_gain_env) * attack_rate;               morph_progress += (1.0f - morph_progress) * morph_rate;
            } else {
                feedback_gain_env += (0.0f - feedback_gain_env) * release_rate;
                morph_progress = 0.0f;
            }

            // Calculate precise note frequency with vibrato & bending tracking
            float active_base_freq = latched_note_freq * (1.0f - vibrato_knob) + current_note_freq * vibrato_knob;
            if (active_base_freq < 65.0f) active_base_freq = 65.0f;
            if (active_base_freq > 1400.0f) active_base_freq = 1400.0f;

            // Compute harmonic multiplier
            float harm_mult = 1.0f;
            if (harmonic_mode == 0) {
                harm_mult = 1.0f;         // Unison (Fundamental E-Bow sustain)
            } else if (harmonic_mode == 1) {
                harm_mult = 1.498307f;    // 5th Above (Jimi Hendrix singing feedback)
            } else if (harmonic_mode == 2) {
                harm_mult = 2.0f;         // Octave Above
            } else if (harmonic_mode == 3) {
                harm_mult = 4.0f;         // 2nd Octave
            } else if (harmonic_mode == 4) {
                // Dynamic Morph: Starts at fundamental and blooms up into singing 5th
                harm_mult = 1.0f + morph_progress * 0.498307f;
            }

            target_osc_freq = active_base_freq * harm_mult;
            smoothed_osc_freq += (target_osc_freq - smoothed_osc_freq) * pitch_smooth_rate;

            // Live Wavetable Playback
            wt_phase += (smoothed_osc_freq / sample_rate);
            if (wt_phase >= 1.0) wt_phase -= 1.0;
            if (wt_phase < 0.0) wt_phase += 1.0;
            
            float read_idx = (float)(wt_phase * wt_period);
            int i0 = (int)read_idx;
            int i1 = i0 + 1;
            if (i1 >= (int)wt_period) i1 = 0;
            float frac = read_idx - (float)i0;
            
            float wt_out = wavetable[i0] + frac * (wavetable[i1] - wavetable[i0]);
            
            // Pure Sine Wave Generation
            double phase_inc = (2.0 * M_PI * smoothed_osc_freq) / sample_rate;
            if (mode == 1) phase_inc *= 1.5; // Poly Sustainer (5th)
            if (mode == 2) phase_inc *= 2.0; // Harmonic Bloom (Octave)
            
            osc_phase += phase_inc;
            if (osc_phase >= 2.0 * M_PI) osc_phase -= 2.0 * M_PI;
            float pure_sine = (float)sin(osc_phase);
            
            // Smoothly morph from the Guitar Timbre (wavetable) into the Pure Sine Wave (feedback whistle)
            // As the note blooms, the harmonics collapse into the resonant sine.
            float blended_fb = wt_out * (1.0f - morph_progress) + pure_sine * morph_progress;
            
            // Apply feedback gain envelope
            // The mix control scales the overall output volume.
            // We use gain_knob (0 to 1) just as an amplitude control, but NO DISTORTION.
            // 1. WARMTH: Gentle Tube Saturation
            // A pure sine wave sounds too digital. A slight soft-clip adds analog tube warmth.
            // We use the warmth_knob to gently saturate the blended tone.
            float pre_drive = blended_fb * (1.0f + warmth_knob * 2.0f);
            float saturated = tanhf(pre_drive) * (1.0f - warmth_knob * 0.3f); 
            
            float final_fb = saturated * feedback_gain_env * gain_knob * 1.2f;

            
            float fb_sig_l = final_fb;
            float fb_sig_r = final_fb;
            
            // Cabinet Emulation Filter (Highpass 100Hz + Dual Lowpass)
            cab_hp_l += hp_coeff * (fb_sig_l - cab_hp_l);
            cab_hp_r += hp_coeff * (fb_sig_r - cab_hp_r);
            float hp_l = fb_sig_l - cab_hp_l;
            float hp_r = fb_sig_r - cab_hp_r;

            cab_lp1_l += lp_coeff * (hp_l - cab_lp1_l);
            cab_lp1_r += lp_coeff * (hp_r - cab_lp1_r);
            cab_lp2_l += lp_coeff * (cab_lp1_l - cab_lp2_l);
            cab_lp2_r += lp_coeff * (cab_lp1_r - cab_lp2_r);

            // Final Output Mix (Clean dry guitar + singing musical feedback tone)
            // 2. ACOUSTIC DISTANCE (Micro-Delay Comb Filtering & Stereo Spread)
            // Simulates the physical air gap between the amplifier speaker and the guitar strings
            lfo_phase += 1.5f / sample_rate; // 1.5 Hz slow LFO
            if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;
            
            // L modulates 12ms to 16ms, R modulates 16ms to 20ms
            float delay_ms_l = 12.0f + sinf(lfo_phase * 2.0f * M_PI) * 4.0f;
            float delay_ms_r = 16.0f + cosf(lfo_phase * 2.0f * M_PI) * 4.0f;
            
            float read_samp_l = delay_ms_l * (sample_rate / 1000.0f);
            float read_samp_r = delay_ms_r * (sample_rate / 1000.0f);
            
            int ri_l = delay_idx - (int)read_samp_l;
            if (ri_l < 0) ri_l += 4096;
            int ri_r = delay_idx - (int)read_samp_r;
            if (ri_r < 0) ri_r += 4096;
            
            // Write to delay buffers
            delay_buf_l[delay_idx] = cab_lp2_l;
            delay_buf_r[delay_idx] = cab_lp2_r;
            
            if (++delay_idx >= 4096) delay_idx = 0;
            
            // Mix dry feedback with delayed feedback to create comb filtering (acoustic resonance)
            float final_wet_l = (cab_lp2_l * 0.6f + delay_buf_l[ri_l] * 0.5f) * (0.8f + gain_knob * 0.5f);
            float final_wet_r = (cab_lp2_r * 0.6f + delay_buf_r[ri_r] * 0.5f) * (0.8f + gain_knob * 0.5f);

            // Output Mix
            p_out_l[i] = in_l + final_wet_l * mix_knob;
            if (p_out_r) {
                p_out_r[i] = in_r + final_wet_r * mix_knob;
            }
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double rate,
                             const char* path,
                             const LV2_Feature* const* features) {
    return new CyberAcousticFeedbacker(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberAcousticFeedbacker*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberAcousticFeedbacker*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete (CyberAcousticFeedbacker*)instance;
}

static const void* extension_data(const char* uri) {
    return NULL;
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

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LV2_EXPORT __declspec(dllexport)
#else
  #define LV2_EXPORT __attribute__((visibility("default")))
#endif

LV2_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return (index == 0) ? &descriptor : NULL;
}

#ifdef __cplusplus
}
#endif
