/*
 * GrainEngine — granular delay engine for Grains of Sand.
 *
 * Pure C++14, header-only, no DPF includes — testable standalone
 * (tests/test_grain_engine.cpp) and wrapped by GrainsOfSandPlugin.cpp.
 *
 * Design: docs/superpowers/specs/2026-06-03-grains-of-sand-design.md
 *
 * RT rules: setup() allocates and is NOT RT-safe; process() never
 * allocates, locks, or does I/O.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gos {

// Non-finite check by bit pattern (exponent all-ones == inf/NaN). DPF
// release builds use -ffast-math, whose -ffinite-math-only folds
// std::isfinite to a constant — these guards must survive those flags.
inline bool finitef(float v)
{
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return ((u >> 23) & 0xFFu) != 0xFFu;
}

constexpr float    kPi             = 3.14159265358979323846f;
constexpr float    kBufferSeconds  = 4.0f;
constexpr float    kMaxDelayMs     = 2500.0f;
constexpr int      kMaxGrains      = 20;   // slots: 16 musical + steal-fade headroom
constexpr int      kTargetGrains   = 16;   // scheduler cap on concurrently sounding grains
constexpr int      kWinLutSize     = 2048; // full-Hann LUT resolution
constexpr uint32_t kStealFadeLen   = 64;   // samples for a stolen grain's fade-out

// Feedback-tap gain compensation: grains re-capture only their windowed
// average (Hann avg gain ~0.5), so the recycle loop taps the L+R SUM
// (x2 vs the stereo average) to make the feedback knob behave like a
// conventional delay feedback control. Without this, even fb=1.1 decays
// in well under a second. Measured: neither version self-oscillates;
// with compensation fb=1.1 gives a multi-second soft-limited decay.
constexpr float kFbWindowComp = 2.0f;

enum Quantize { kQuantFree = 0, kQuantSemitones, kQuantOctaves, kQuantFifths, kQuantIntervals, kNumQuantModes };
enum FbMode   { kFbRecycle = 0, kFbPostDelay, kFbRepeat, kFbPingPong, kFbRepeatN, kNumFbModes };

// xorshift32 — cheap, RT-safe, deterministic under a fixed seed.
struct Rng {
    uint32_t state = 0x12345678u;
    void seed(uint32_t s) { state = s ? s : 0x12345678u; }
    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }    // [0,1) exact
    float bipolar() { return uniform() * 2.f - 1.f; }                   // [-1,1)
};

struct Grain {
    bool     active    = false;
    double   pos       = 0.0;   // read position in buffer samples
    double   inc       = 1.0;   // signed fractional increment (pitch x direction)
    uint32_t remaining = 0;
    uint32_t length    = 1;
    float    invLength = 1.f;   // 1/length, cached at birth (no per-sample divide)
    float    shape     = 0.7f;  // window shape latched at birth
    float    panL      = 0.7071f, panR = 0.7071f;
    float    fade      = 1.f, fadeStep = 0.f;  // steal fade-out
    uint32_t age       = 0;     // birth sequence number (steal-oldest)
};

struct Params {
    float delayMs        = 400.f;   // 0..2500
    float sprayMs        = 20.f;    // 0..500
    float stretch        = 1.f;     // sweep-playhead speed 1/16..16; 1 = off (pinned to delay)
    float sizeMs         = 150.f;   // 10..500
    float density        = 8.f;     // grains/s, 0.5..40
    float jitter         = 0.25f;   // 0..1
    float shape          = 0.7f;    // 0..1 (boxy..Hann)
    float reverse        = 0.f;     // 0..1 per-grain reverse probability
    float spread         = 0.6f;    // 0..1 random pan width
    float pitch          = 0.f;     // semitones -12..+12
    int   quantize       = kQuantFree;
    float detune         = 8.f;     // cents 0..100
    float intervalChance = 0.f;     // 0..1; LV2 port symbol: pitch_rnd
    float feedback       = 0.3f;    // 0..1.1
    int   fbMode         = kFbRecycle;
    float repeatDice     = 1.f;     // 0..1; Repeat # only: P(play held slice vs live)
    float toneHz         = 6000.f;  // 500..12000
    bool  freeze         = false;
    float threshDb       = -70.f;   // -70 == Off
};

class GrainEngine {
public:
    // -------- lifecycle (NOT RT-safe) --------
    void setup(double sampleRate)
    {
        fs_ = sampleRate;
        bufferSize_ = (uint32_t)(kBufferSeconds * fs_);
        buffer_.assign(bufferSize_, 0.f);
        pdSize_ = (uint32_t)(kMaxDelayMs * 0.001 * fs_) + 4;
        pdBufL_.assign(pdSize_, 0.f);
        pdBufR_.assign(pdSize_, 0.f);
        for (int i = 0; i <= kWinLutSize; ++i)
            winLut_[i] = 0.5f - 0.5f * std::cos(2.f * kPi * (float)i / (float)kWinLutSize);
        // force exact-zero endpoints (float cos(2*pi) != 1 exactly) — the
        // no-click invariant requires window(0) == window(1) == 0
        winLut_[0] = 0.f;
        winLut_[kWinLutSize] = 0.f;
        writeRampStep_ = 1.f / (0.010f * (float)fs_);          // 10 ms freeze crossfade
        envAtkCoef_    = 1.f - std::exp(-1.f / (0.005f * (float)fs_));
        envRelCoef_    = 1.f - std::exp(-1.f / (0.150f * (float)fs_));
        holdSamples_   = (uint32_t)(0.080 * fs_);              // 80 ms below-threshold hold
        smoothCoef_    = 1.f - std::exp(-1.f / (0.010f * (float)fs_));
        pdTimeCoef_    = 1.f - std::exp(-1.f / (0.050f * (float)fs_));
        dcCoef_        = 1.f - (2.f * kPi * 25.f) / (float)fs_;   // ~25 Hz DC-block corner, SR-independent
        reset();
    }

    void reset()
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.f);
        std::fill(pdBufL_.begin(), pdBufL_.end(), 0.f);
        std::fill(pdBufR_.begin(), pdBufR_.end(), 0.f);
        for (auto& g : grains_) g = Grain();
        rng_.seed(0xC0FFEEu);
        writeIdx_ = 0; pdIdx_ = 0;
        writeGain_ = 1.f;
        samplesToNext_ = 1;
        sweepBehind_ = 0.0; stretchOn_ = false;
        spawnCount_ = 0; spawnSeq_ = 0;
        norm_ = 1.f;
        fbSample_ = toneLp_ = dcX1_ = dcY1_ = 0.f;
        fbGain_ = 0.f;
        pdFbGain_ = 0.f;
        pdSendGain_ = 0.f;
        pdDrain_ = 0;
        pdToneL_ = pdToneR_ = 0.f;
        pdTime_ = 0.f;
        env_ = 0.f; holdCount_ = 0; threshFrozen_ = false; frozenState_ = false;
        haveLastSlice_ = false; lastSlicePos_ = 0.0; lastSliceInc_ = 1.0; lastSliceLen_ = 1;
        repeatSlot_ = 0; heldPos_ = 0.0; heldInc_ = 1.0; heldLen_ = 1;
    }

    // -------- block interface (RT-safe) --------
    void setParams(const Params& p) { params_ = p; }

    void process(const float* in, float* wetL, float* wetR, uint32_t frames)
    {
        const bool threshOn = params_.threshDb > -69.5f;
        const float threshLin = threshOn ? std::pow(10.f, params_.threshDb * (1.f / 20.f)) : 0.f;
        const float threshHyst = threshLin * 2.f;    // +6 dB release hysteresis

        toneCoef_ = 1.f - std::exp(-2.f * kPi * std::min(params_.toneHz, 0.45f * (float)fs_) / (float)fs_);
        const float fbTarget = (params_.fbMode == kFbRecycle)
                             ? std::min(params_.feedback, 1.1f) : 0.f;
        const bool pdOn = (params_.fbMode == kFbPostDelay)
                       || (params_.fbMode == kFbPingPong);
        const bool pdCross = (params_.fbMode == kFbPingPong);   // swap L/R per repeat
        const float pdFbTarget = pdOn ? std::min(params_.feedback, 1.1f) : 0.f;
        const float pdSendTarget = pdOn ? 1.f : 0.f;

        // ---- stretch sweep playhead (block-constant window) ----
        // At 1x the playhead is pinned to `delay` (classic behavior). Off
        // 1x it drifts by (head advance - stretch) per sample and wraps
        // sawtooth within (0, delay] — Particle DELAY+LFO stretch/compress.
        stretchOn_ = std::fabs(params_.stretch - 1.f) > 1e-3f;
        const double sweepWindow = std::max(1.0, (double)params_.delayMs * 0.001 * fs_);
        if (!stretchOn_)
            sweepBehind_ = sweepWindow;
        else if (sweepBehind_ > sweepWindow || sweepBehind_ <= 0.0)
            sweepBehind_ = sweepWindow;   // window jumped (delay knob): re-enter
        const float pdTimeTarget = std::max(1.f, params_.delayMs * 0.001f * (float)fs_);

        for (uint32_t f = 0; f < frames; ++f) {
            float x = in[f];
            if (!finitef(x)) x = 0.f;           // never let NaN into the buffer
            x = std::min(std::max(x, -16.f), 16.f);   // absurd-but-finite input
                                                      // can't push grain sums to inf

            // ---- threshold freeze state machine ----
            const float a = std::fabs(x);
            env_ += (a - env_) * (a > env_ ? envAtkCoef_ : envRelCoef_);
            if (env_ < 1e-15f) env_ = 0.f;     // denormal guard (ARM VFP stalls on subnormals)
            if (threshOn) {
                if (env_ > threshHyst) {                 // ~ +6 dB hysteresis
                    holdCount_ = 0;
                    threshFrozen_ = false;
                } else if (env_ < threshLin) {
                    if (holdCount_ < holdSamples_ && ++holdCount_ >= holdSamples_)
                        threshFrozen_ = true;
                } else {
                    // hysteresis dead zone [threshLin, threshHyst]: frozen
                    // state is sticky (un-freeze needs +6 dB), but the hold
                    // must be CONSECUTIVE below-thresh time (spec: "stays
                    // below thresh for ~80 ms") — reset it here
                    holdCount_ = 0;
                }
            } else {
                threshFrozen_ = false;
                holdCount_ = 0;
            }

            // ---- freeze: OR of latch + threshold, 10 ms write crossfade ----
            // updated per-sample; grain scheduling code in this loop must read
            // this variable directly, NOT the end-of-block frozen() accessor
            frozenState_ = params_.freeze || threshFrozen_;
            const float wgTarget = frozenState_ ? 0.f : 1.f;
            if (writeGain_ < wgTarget)
                writeGain_ = std::min(wgTarget, writeGain_ + writeRampStep_);
            else if (writeGain_ > wgTarget)
                writeGain_ = std::max(wgTarget, writeGain_ - writeRampStep_);

            // ---- write head (halts at writeGain == 0) ----
            const bool headAdvances = writeGain_ > 0.f;
            if (headAdvances) {
                const float w = x + fbSample_;                // recycle injection (tone LPF→tanh→DC)
                float& cell = buffer_[writeIdx_];
                cell = writeGain_ * w + (1.f - writeGain_) * cell;
                if (++writeIdx_ >= bufferSize_) writeIdx_ = 0;   // no per-sample UDIV
            }

            // ---- stretch sweep: drift + sawtooth wrap ----
            // While frozen the head halts, so the playhead sweeps through
            // the frozen buffer — motorized scrubbing of a held bed.
            if (stretchOn_) {
                sweepBehind_ += (headAdvances ? 1.0 : 0.0) - (double)params_.stretch;
                if (sweepBehind_ > sweepWindow)  sweepBehind_ -= sweepWindow;
                else if (sweepBehind_ <= 0.0)    sweepBehind_ += sweepWindow;
            }

            // ---- scheduler ----
            if (--samplesToNext_ <= 0) {
                spawnGrain();
                scheduleNext();
            }

            // ---- render grains ----
            float gl = 0.f, gr = 0.f;
            // NOTE: 'sounding' here counts ALL active grains including steal-fading
            // ones (they do emit audio, so the energy normalization must see them);
            // spawnGrain()'s cap check deliberately EXCLUDES fading grains. Both are
            // intentional — don't "unify" them.
            int sounding = 0;
            for (auto& g : grains_) {
                if (!g.active) continue;
                ++sounding;
                const float phase = 1.f - (float)g.remaining * g.invLength;
                const float wgain = window(phase, g.shape) * g.fade;
                const float s = readInterp(g.pos) * wgain;
                gl += s * g.panL;
                gr += s * g.panR;
                g.pos = wrapPos(g.pos + g.inc);
                if (g.fadeStep != 0.f) {
                    g.fade += g.fadeStep;
                    if (g.fade <= 0.f) { g.active = false; continue; }
                }
                if (--g.remaining == 0)
                    g.active = false;
            }

            // ---- 1/sqrt(N) overlap normalization, smoothed ----
            const float targetNorm = 1.f / std::sqrt((float)std::max(1, sounding));
            norm_ += (targetNorm - norm_) * smoothCoef_;
            wetL[f] = gl * norm_;
            wetR[f] = gr * norm_;

            // ---- recycle feedback: wet -> tone LPF -> tanh -> DC block -> write head ----
            fbGain_ += (fbTarget - fbGain_) * smoothCoef_;
            const float mono = (wetL[f] + wetR[f]) * 0.5f * kFbWindowComp * fbGain_;
            toneLp_ += toneCoef_ * (mono - toneLp_);
            const float sat = std::tanh(toneLp_);
            const float dc = sat - dcX1_ + dcCoef_ * dcY1_;
            dcX1_ = sat;
            dcY1_ = dc;
            fbSample_ = dc;

            // ---- post-delay topology: clean stereo echo of the wet sum ----
            // The line keeps running after fb_mode switches away (mode
            // stepping is an advertised live gesture): only the SEND is
            // gated, so the stored tail plays out instead of hard-cutting,
            // and one full buffer lap later the line is silent — nothing
            // stale replays when Post-delay is re-entered. pdDrain_ counts
            // that lap so quiet modes pay no per-sample cost afterwards.
            if (pdOn) {
                pdDrain_ = pdSize_;
            } else if (pdDrain_ > 0) {
                --pdDrain_;
                if (pdDrain_ == 0)
                    pdToneL_ = pdToneR_ = 0.f;
            }
            if (pdDrain_ > 0) {
                pdFbGain_ += (pdFbTarget - pdFbGain_) * smoothCoef_;
                pdSendGain_ += (pdSendTarget - pdSendGain_) * smoothCoef_;
                pdTime_ += (pdTimeTarget - pdTime_) * pdTimeCoef_;   // tape-like glide
                const float dl = readPd(pdBufL_, pdTime_);
                const float dr = readPd(pdBufR_, pdTime_);
                pdToneL_ += toneCoef_ * (dl - pdToneL_);
                pdToneR_ += toneCoef_ * (dr - pdToneR_);
                // ping-pong: the recirculation crosses channels, so each
                // repeat hops sides (send/return stay straight)
                const float fbL = std::tanh((pdCross ? pdToneR_ : pdToneL_) * pdFbGain_);
                const float fbR = std::tanh((pdCross ? pdToneL_ : pdToneR_) * pdFbGain_);
                pdBufL_[pdIdx_] = wetL[f] * pdSendGain_ + fbL;
                pdBufR_[pdIdx_] = wetR[f] * pdSendGain_ + fbR;
                if (++pdIdx_ >= pdSize_) pdIdx_ = 0;                 // no per-sample UDIV
                wetL[f] += dl;
                wetR[f] += dr;
            }
        }

        // ---- never let non-finite state survive a block ----
        if (!finitef(fbSample_) || !finitef(toneLp_)
            || !finitef(dcX1_) || !finitef(dcY1_) || !finitef(norm_)) {
            fbSample_ = toneLp_ = dcX1_ = dcY1_ = 0.f;
            norm_ = 1.f;
        }
        if (!finitef(pdToneL_) || !finitef(pdToneR_)) {
            pdToneL_ = pdToneR_ = 0.f;
            std::fill(pdBufL_.begin(), pdBufL_.end(), 0.f);   // rare catastrophic reset
            std::fill(pdBufR_.begin(), pdBufR_.end(), 0.f);
        }
    }

    // -------- introspection (tests, freeze_state output port) --------
    bool     frozen() const        { return frozenState_; }
    int      activeGrains() const
    {
        int n = 0;
        for (const auto& g : grains_) if (g.active) ++n;
        return n;
    }
    uint32_t grainsSpawned() const { return spawnCount_; }
    uint32_t writeIndex() const    { return writeIdx_; }
    uint32_t bufferSize() const    { return bufferSize_; }
    float    bufferAt(uint32_t i) const { return buffer_[i % bufferSize_]; }
    const Grain& grain(int i) const
    {
        assert(i >= 0 && i < kMaxGrains);
        return grains_[i];
    }
    double   lastSlicePos() const  { return lastSlicePos_; }

    // Snap a semitone value to the quantize scale (nearest member).
    static float snapPitch(float semis, int quantize)
    {
        if (quantize == kQuantFree)
            return semis;
        if (quantize == kQuantSemitones)
            return std::round(semis);
        const float* tab; int n;
        quantTable(quantize, tab, n);
        float best = tab[0];
        for (int i = 1; i < n; ++i)
            if (std::fabs(semis - tab[i]) < std::fabs(semis - best))
                best = tab[i];
        return best;
    }

    static void quantTable(int quantize, const float*& tab, int& n)
    {
        static const float kOctaves[]   = { -12.f, 0.f, 12.f };
        static const float kFifths[]    = { -12.f, -5.f, 0.f, 7.f, 12.f };
        static const float kIntervals[] = { -12.f, -10.f, -7.f, -5.f, -4.f, -3.f,
                                            0.f, 3.f, 4.f, 5.f, 7.f, 10.f, 12.f };
        static const float kSemis[]     = { -12.f, -11.f, -10.f, -9.f, -8.f, -7.f, -6.f,
                                            -5.f, -4.f, -3.f, -2.f, -1.f, 0.f, 1.f, 2.f,
                                            3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f };
        switch (quantize) {
            case kQuantOctaves:   tab = kOctaves;   n = 3;  break;
            case kQuantFifths:    tab = kFifths;    n = 5;  break;
            case kQuantIntervals: tab = kIntervals; n = 13; break;
            default:              tab = kSemis;     n = 25; break;   // Semitones (and Free's random draw)
        }
    }

    // Window gain for phase in [0,1]. shape: 0 = boxy trapezoid (2% edges),
    // 0.5 = triangle, 1 = full Hann. Endpoints are exactly 0 for all shapes.
    float window(float phase, float shape) const
    {
        phase = std::min(std::max(phase, 0.f), 1.f);
        const float tri = 1.f - std::fabs(2.f * phase - 1.f);
        if (shape < 0.5f) {
            const float edge = 0.02f + 0.96f * shape;          // 0.02 .. 0.5
            const float t = std::min(phase, 1.f - phase);      // distance to nearest end
            return std::min(1.f, t / edge);
        }
        const float m = (shape - 0.5f) * 2.f;                  // 0..1 triangle -> Hann
        const float x = phase * (float)kWinLutSize;
        const int   i = (int)x;
        const float f = x - (float)i;
        const float hann = winLut_[i] + f * (winLut_[std::min(i + 1, kWinLutSize)] - winLut_[i]);
        return tri + m * (hann - tri);
    }

private:
    // -------- internals --------
    double wrapPos(double pos) const
    {
        const double n = (double)bufferSize_;
        while (pos < 0.0)  pos += n;
        while (pos >= n)   pos -= n;
        return pos;
    }

    // delaySamps must be in [1, pdSize_-2]. No '%' — RT path.
    float readPd(const std::vector<float>& buf, float delaySamps) const
    {
        double rp = (double)pdIdx_ - (double)delaySamps;
        if (rp < 0.0) rp += (double)pdSize_;
        uint32_t i0 = (uint32_t)rp;
        const float frac = (float)(rp - (double)i0);
        if (i0 >= pdSize_) i0 = 0;                                   // defensive
        const uint32_t i1 = (i0 + 1u == pdSize_) ? 0u : i0 + 1u;
        return buf[i0] + frac * (buf[i1] - buf[i0]);
    }

    // pos must be pre-wrapped to [0, bufferSize_). No '%' here — RT path.
    float readInterp(double pos) const
    {
        uint32_t i0 = (uint32_t)pos;
        const float frac = (float)(pos - (double)i0);
        if (i0 >= bufferSize_) i0 = 0;                    // defensive
        const uint32_t i1 = (i0 + 1u == bufferSize_) ? 0u : i0 + 1u;
        return buffer_[i0] + frac * (buffer_[i1] - buffer_[i0]);
    }

    void scheduleNext()
    {
        const float base = (float)fs_ / std::max(0.1f, params_.density);
        const float r = 1.f + params_.jitter * rng_.bipolar();   // uniform(1-j, 1+j)
        samplesToNext_ = std::max(1, (int32_t)(base * r));
    }

    int findFreeSlot() const
    {
        for (int i = 0; i < kMaxGrains; ++i)
            if (!grains_[i].active) return i;
        return -1;
    }

    void stealOldest()
    {
        int oldest = -1;
        uint32_t bestAge = 0xFFFFFFFFu;
        for (int i = 0; i < kMaxGrains; ++i)
            if (grains_[i].active && grains_[i].fadeStep == 0.f && grains_[i].age < bestAge) {
                bestAge = grains_[i].age;
                oldest = i;
            }
        if (oldest >= 0)
            grains_[oldest].fadeStep = -1.f / (float)kStealFadeLen;
    }

    void spawnGrain()
    {
        // count grains not already fading out; enforce the musical cap
        int sounding = 0;
        for (const auto& g : grains_)
            if (g.active && g.fadeStep == 0.f) ++sounding;
        const int slot = findFreeSlot();
        if (slot < 0)
            return;                       // pathological overload: drop this
                                          // spawn BEFORE stealing — never
                                          // fade a voice for a grain that
                                          // can't spawn anyway
        if (sounding >= kTargetGrains)
            stealOldest();
        Grain& g = grains_[slot];
        const float fsf = (float)fs_;

        // max() before the cast: negative float -> uint32 is UB per the
        // standard (the engine header advertises standalone use, so it
        // can't rely on the wrapper's knob clamping)
        uint32_t length = (uint32_t)(std::max(params_.sizeMs, 0.f) * 0.001f * fsf);
        length = std::min(std::max(length, 64u), (uint32_t)(0.5f * fsf));

        const float rate = grainRate();   // pitch system (Task 5); returns 1.f until then
        const bool rev = rng_.uniform() < params_.reverse;
        g.inc = rev ? -(double)rate : (double)rate;

        // position behind the write head: delay +- spray, clamped so a
        // forward grain can't overtake the write head and nothing reads
        // outside the valid CONTENT span. The write head advances `length`
        // samples during the grain's life, so distance-behind-head drifts
        // by (1+rate) per sample for reverse grains and (1-rate) for slow
        // forward ones — the clamp must reserve that drift or the read
        // wraps past the oldest data into freshly written audio. (Like the
        // Particle, grain span imposes min/max effective delay.)
        double behind = (stretchOn_ ? sweepBehind_
                                    : (double)params_.delayMs * 0.001 * fs_)
                      + (double)rng_.bipolar() * (double)params_.sprayMs * 0.001 * fs_;
        behind = std::max(behind, 1.0);
        if (!rev)
            behind = std::max(behind, (double)length * (double)rate + 4.0);
        const double drift = rev ? (1.0 + (double)rate)
                                 : std::max(0.0, 1.0 - (double)rate);
        behind = std::min(behind, (double)bufferSize_ - (double)length * drift - 4.0);

        // repeat topologies. NOTE (accepted): cloned/held positions are
        // absolute buffer indices. At high feedback the live write head laps
        // the 4 s buffer underneath them, so after a lap the repeated slice
        // plays freshly-overwritten audio — a bounded discontinuity,
        // embraced as stutter character.
        if (params_.fbMode == kFbRepeatN) {
            // counted repeat (Particle "Repeat #"): the held slice is
            // recaptured every N grain slots; dice = P(play held vs live)
            const int n = (int)std::lround(std::min(params_.feedback, 1.f) * 8.f);
            const double livePos = wrapPos((double)writeIdx_ - behind);
            if (n <= 0) {
                g.pos = livePos;
            } else {
                if (repeatSlot_ % (uint32_t)n == 0) {
                    heldPos_ = livePos;
                    heldInc_ = g.inc;
                    heldLen_ = length;
                }
                ++repeatSlot_;
                if (rng_.uniform() < params_.repeatDice) {
                    g.pos = heldPos_;
                    g.inc = heldInc_;
                    length = heldLen_;
                } else {
                    g.pos = livePos;
                }
            }
        } else if (params_.fbMode == kFbRepeat && haveLastSlice_
                   && rng_.uniform() < std::min(params_.feedback, 1.f)) {
            // probabilistic repeat (Particle "Repeat %"): clone the
            // previous grain's slice with p = feedback
            g.pos = lastSlicePos_;
            g.inc = lastSliceInc_;
            length = lastSliceLen_;
        } else {
            g.pos = wrapPos((double)writeIdx_ - behind);
        }
        lastSlicePos_ = g.pos;
        lastSliceInc_ = g.inc;
        lastSliceLen_ = length;
        haveLastSlice_ = true;

        g.length = length;
        g.remaining = length;
        g.invLength = 1.f / (float)length;
        g.shape = params_.shape;

        // equal-power pan at a random position within +-spread
        const float panPos = rng_.bipolar() * params_.spread;        // [-spread, +spread]
        const float theta = (panPos + 1.f) * 0.25f * kPi;            // [0, pi/2]
        g.panL = std::cos(theta);
        g.panR = std::sin(theta);

        g.fade = 1.f;
        g.fadeStep = 0.f;
        g.age = spawnSeq_++;
        g.active = true;
        ++spawnCount_;
    }

    // Per-grain playback rate: base pitch (snapped to the quantize scale),
    // with probability `intervalChance` replaced by a random member of the
    // active scale (Free: uniform anywhere in +-12 st), plus +-detune cents.
    float grainRate()
    {
        float semis;
        if (rng_.uniform() < params_.intervalChance) {
            if (params_.quantize == kQuantFree) {
                semis = rng_.bipolar() * 12.f;
            } else {
                const float* tab; int n;
                quantTable(params_.quantize, tab, n);
                semis = tab[rng_.next() % (uint32_t)n];
            }
        } else {
            semis = snapPitch(params_.pitch, params_.quantize);
        }
        semis += rng_.bipolar() * params_.detune * 0.01f;
        return std::exp2(semis * (1.f / 12.f));
    }

    // -------- state --------
    double fs_ = 48000.0;
    uint32_t bufferSize_ = 1;
    std::vector<float> buffer_;
    uint32_t writeIdx_ = 0;
    float winLut_[kWinLutSize + 1] = {};
    Grain grains_[kMaxGrains];
    Params params_;
    Rng rng_;
    // scheduler
    int32_t samplesToNext_ = 1;
    uint32_t spawnCount_ = 0, spawnSeq_ = 0;
    // stretch sweep playhead (distance behind the write head, samples)
    double sweepBehind_ = 0.0;
    bool stretchOn_ = false;
    // freeze
    float writeGain_ = 1.f, writeRampStep_ = 0.001f;
    float env_ = 0.f, envAtkCoef_ = 0.1f, envRelCoef_ = 0.001f;
    uint32_t holdCount_ = 0, holdSamples_ = 1;
    bool threshFrozen_ = false, frozenState_ = false;
    // output normalization
    float norm_ = 1.f, smoothCoef_ = 0.01f;
    // recycle feedback path
    float fbSample_ = 0.f, toneLp_ = 0.f, dcX1_ = 0.f, dcY1_ = 0.f, fbGain_ = 0.f;
    float toneCoef_ = 0.5f;   // per-block tone LPF coefficient (recomputed each block)
    float dcCoef_   = 0.995f; // DC-blocker pole; computed in setup() to track sample rate
    // post-delay topology
    uint32_t pdSize_ = 1, pdIdx_ = 0;
    std::vector<float> pdBufL_, pdBufR_;
    float pdToneL_ = 0.f, pdToneR_ = 0.f, pdTime_ = 0.f, pdTimeCoef_ = 0.01f;
    float pdFbGain_ = 0.f, pdSendGain_ = 0.f;
    uint32_t pdDrain_ = 0;   // samples of line run-out left after mode switch-away
    // repeat topology
    bool haveLastSlice_ = false;
    double lastSlicePos_ = 0.0, lastSliceInc_ = 1.0;
    uint32_t lastSliceLen_ = 1;
    // counted-repeat topology (Repeat #)
    uint32_t repeatSlot_ = 0;
    double heldPos_ = 0.0, heldInc_ = 1.0;
    uint32_t heldLen_ = 1;
};

} // namespace gos
