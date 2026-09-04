#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "lv2/lv2.h"

#define EXPRESSION_CV_URI "http://moddevices.com/plugins/danny/cyber-expression-cv"

enum PortIndex {
    PORT_MIDI_IN      = 0,
    PORT_CV_OUT_1     = 1,
    PORT_CV_OUT_2     = 2,
    PORT_CV_OUT_3     = 3,
    PORT_CV_OUT_4     = 4,
    PORT_CC_NUM       = 5,
    PORT_MIDI_CH      = 6,
    PORT_SMOOTHING    = 7,
    PORT_MIN_VAL      = 8,
    PORT_MAX_VAL      = 9,
    PORT_CURVE        = 10,
    PORT_MANUAL_POS   = 11
};

// LV2 Atom Sequence Structs for MIDI
struct LV2_Atom {
    uint32_t size;
    uint32_t type;
};

struct LV2_Atom_Event {
    union {
        int64_t frames;
        double  beats;
    } time;
    LV2_Atom body;
};

struct LV2_Atom_Sequence_Body {
    uint32_t unit;
    uint32_t pad;
};

struct LV2_Atom_Sequence {
    LV2_Atom atom;
    LV2_Atom_Sequence_Body body;
};

#define LV2_ATOM_SEQUENCE_FOREACH(seq, ev) \
    for (const LV2_Atom_Event* (ev) = (const LV2_Atom_Event*)((const uint8_t*)(seq) + sizeof(LV2_Atom_Sequence)); \
         (const uint8_t*)(ev) < ((const uint8_t*)(seq) + sizeof(LV2_Atom) + (seq)->atom.size); \
         (ev) = (const LV2_Atom_Event*)((const uint8_t*)(ev) + sizeof(LV2_Atom_Event) + (((ev)->body.size + 7) & ~7)))

struct CyberExpressionCV {
    const LV2_Atom_Sequence* midi_in;
    float* cv_out1;
    float* cv_out2;
    float* cv_out3;
    float* cv_out4;

    const float* cc_num;
    const float* midi_ch;
    const float* smoothing;
    const float* min_val;
    const float* max_val;
    const float* curve;
    const float* manual_pos;

    double sampleRate;
    float currentNormVal;
    float targetNormVal;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path; (void)features;
    CyberExpressionCV* self = (CyberExpressionCV*)std::calloc(1, sizeof(CyberExpressionCV));
    if (!self) return nullptr;

    self->sampleRate = sample_rate > 0.0 ? sample_rate : 48000.0;
    self->currentNormVal = 0.0f;
    self->targetNormVal = 0.0f;

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    CyberExpressionCV* self = (CyberExpressionCV*)instance;
    if (!self) return;

    switch (port) {
        case PORT_MIDI_IN:    self->midi_in    = (const LV2_Atom_Sequence*)data_location; break;
        case PORT_CV_OUT_1:   self->cv_out1    = (float*)data_location; break;
        case PORT_CV_OUT_2:   self->cv_out2    = (float*)data_location; break;
        case PORT_CV_OUT_3:   self->cv_out3    = (float*)data_location; break;
        case PORT_CV_OUT_4:   self->cv_out4    = (float*)data_location; break;
        case PORT_CC_NUM:     self->cc_num     = (const float*)data_location; break;
        case PORT_MIDI_CH:    self->midi_ch    = (const float*)data_location; break;
        case PORT_SMOOTHING:  self->smoothing  = (const float*)data_location; break;
        case PORT_MIN_VAL:    self->min_val    = (const float*)data_location; break;
        case PORT_MAX_VAL:    self->max_val    = (const float*)data_location; break;
        case PORT_CURVE:      self->curve      = (const float*)data_location; break;
        case PORT_MANUAL_POS: self->manual_pos = (const float*)data_location; break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    CyberExpressionCV* self = (CyberExpressionCV*)instance;
    if (!self) return;
    self->currentNormVal = 0.0f;
    self->targetNormVal = 0.0f;
}

static float applyCurve(float norm01, int curveType)
{
    float x = std::clamp(norm01, 0.0f, 1.0f);
    switch (curveType) {
        case 1: // Logarithmic / Audio Taper
            return std::pow(x, 2.2f);
        case 2: // Anti-Log / Reverse Audio
            return std::pow(x, 0.45f);
        case 3: // S-Curve (Smoothstep)
            return x * x * (3.0f - 2.0f * x);
        case 0: // Linear
        default:
            return x;
    }
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    CyberExpressionCV* self = (CyberExpressionCV*)instance;
    if (!self || sample_count == 0) return;

    int targetCC = self->cc_num ? static_cast<int>(*self->cc_num + 0.5f) : 7; // Default CC #7 Expression/Vol
    int targetCh = self->midi_ch ? static_cast<int>(*self->midi_ch + 0.5f) : 0; // 0 = Omni, 1-16
    float smoothMs = self->smoothing ? std::max(0.0f, *self->smoothing) : 15.0f;
    float minV = self->min_val ? *self->min_val : 0.0f;
    float maxV = self->max_val ? *self->max_val : 10.0f;
    int curveType = self->curve ? static_cast<int>(*self->curve + 0.5f) : 0;

    // Check manual override knob if set
    if (self->manual_pos && *self->manual_pos > 0.0f) {
        self->targetNormVal = std::clamp(*self->manual_pos / 100.0f, 0.0f, 1.0f);
    }

    // Process incoming MIDI Events from Atom Sequence
    if (self->midi_in) {
        LV2_ATOM_SEQUENCE_FOREACH(self->midi_in, ev) {
            if (ev->body.size >= 3) {
                const uint8_t* msg = (const uint8_t*)(ev + 1);
                uint8_t status = msg[0] & 0xF0;
                uint8_t ch = (msg[0] & 0x0F) + 1; // 1-16

                if (status == 0xB0) { // Control Change
                    uint8_t cc = msg[1];
                    uint8_t val = msg[2];

                    if ((targetCh == 0 || targetCh == ch) && cc == targetCC) {
                        self->targetNormVal = static_cast<float>(val) / 127.0f;
                    }
                }
            }
        }
    }

    // Smoothing coefficient per sample
    float sRate = static_cast<float>(self->sampleRate);
    float smoothSamples = std::max(1.0f, (smoothMs * 0.001f) * sRate);
    float smoothCoeff = 1.0f - std::exp(-3.0f / smoothSamples);

    float* out1 = self->cv_out1;
    float* out2 = self->cv_out2;
    float* out3 = self->cv_out3;
    float* out4 = self->cv_out4;

    for (uint32_t s = 0; s < sample_count; ++s) {
        self->currentNormVal += (self->targetNormVal - self->currentNormVal) * smoothCoeff;
        float shapedVal = applyCurve(self->currentNormVal, curveType);

        // CV Out 1: Direct scaled voltage (0.0 to 10.0V or custom min->max)
        float cv1 = minV + shapedVal * (maxV - minV);
        if (out1) out1[s] = cv1;

        // CV Out 2: Inverted voltage (max -> min)
        float cv2 = maxV - shapedVal * (maxV - minV);
        if (out2) out2[s] = cv2;

        // CV Out 3: Normalized 0.0 to 1.0V
        float cv3 = shapedVal * 1.0f;
        if (out3) out3[s] = cv3;

        // CV Out 4: Bipolar -5.0V to +5.0V
        float cv4 = (shapedVal - 0.5f) * 10.0f;
        if (out4) out4[s] = cv4;
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    CyberExpressionCV* self = (CyberExpressionCV*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri) { (void)uri; return nullptr; }

static const LV2_Descriptor descriptor = {
    EXPRESSION_CV_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : nullptr;
}
