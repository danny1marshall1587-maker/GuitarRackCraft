#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include "lv2/lv2.h"
#include "CyberStompBoxEngine.hpp"

#define CYBER_STOMP_BOX_URI "http://moddevices.com/plugins/danny/cyber-stomp-box"

#define LV2_ATOM__Sequence "http://lv2plug.in/ns/ext/atom#Sequence"
#define LV2_MIDI__MidiEvent "http://lv2plug.in/ns/ext/midi#MidiEvent"
#define LV2_URID__map "http://lv2plug.in/ns/ext/urid#map"

typedef uint32_t LV2_URID;
typedef void* LV2_URID_Map_Handle;

typedef struct _LV2_URID_Map {
    LV2_URID_Map_Handle handle;
    LV2_URID (*map)(LV2_URID_Map_Handle handle, const char* uri);
} LV2_URID_Map;

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

enum PortIndex {
    PORT_AUDIO_IN_L     = 0,
    PORT_AUDIO_IN_R     = 1,
    PORT_AUDIO_OUT_L    = 2,
    PORT_AUDIO_OUT_R    = 3,
    PORT_MIDI_IN        = 4,
    PORT_BYPASS         = 5,
    PORT_TRIGGER        = 6,
    PORT_MODEL          = 7,
    PORT_PITCH          = 8,
    PORT_DECAY          = 9,
    PORT_PUNCH          = 10,
    PORT_SUB_THUMP      = 11,
    PORT_TONE           = 12,
    PORT_FIXED_VEL      = 13,
    PORT_GUITAR_VOL     = 14,
    PORT_STOMP_VOL      = 15,
    PORT_OUTPUT_MODE    = 16,
    PORT_LED_ACTIVITY   = 17
};

struct CyberStompBoxLV2 {
    const float* inL;
    const float* inR;
    float*       outL;
    float*       outR;
    const LV2_Atom_Sequence* midi_in;

    const float* bypass;
    const float* trigger_port;
    const float* model;
    const float* pitch;
    const float* decay;
    const float* punch;
    const float* sub_thump;
    const float* tone;
    const float* fixed_vel;
    const float* guitar_vol;
    const float* stomp_vol;
    const float* output_mode;

    float*       led_activity;

    AudioDSP::CyberStompBoxEngine engine;

    LV2_URID_Map* map;
    LV2_URID urid_sequence;
    LV2_URID urid_midi_event;

    float lastTriggerVal;
    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                             double sample_rate,
                             const char* bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor; (void)bundle_path;
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)std::calloc(1, sizeof(CyberStompBoxLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);
    self->lastTriggerVal = 0.0f;

    if (features) {
        for (int i = 0; features[i]; ++i) {
            if (features[i]->URI && !std::strcmp(features[i]->URI, LV2_URID__map)) {
                self->map = (LV2_URID_Map*)features[i]->data;
                break;
            }
        }
    }

    if (self->map && self->map->map && self->map->handle) {
        self->urid_sequence = self->map->map(self->map->handle, LV2_ATOM__Sequence);
        self->urid_midi_event = self->map->map(self->map->handle, LV2_MIDI__MidiEvent);
    } else {
        self->urid_sequence = 1;
        self->urid_midi_event = 2;
    }

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN_L:   self->inL          = (const float*)data; break;
        case PORT_AUDIO_IN_R:   self->inR          = (const float*)data; break;
        case PORT_AUDIO_OUT_L:  self->outL         = (float*)data;       break;
        case PORT_AUDIO_OUT_R:  self->outR         = (float*)data;       break;
        case PORT_MIDI_IN:      self->midi_in      = (const LV2_Atom_Sequence*)data; break;
        case PORT_BYPASS:       self->bypass       = (const float*)data; break;
        case PORT_TRIGGER:      self->trigger_port = (const float*)data; break;
        case PORT_MODEL:        self->model        = (const float*)data; break;
        case PORT_PITCH:        self->pitch        = (const float*)data; break;
        case PORT_DECAY:        self->decay        = (const float*)data; break;
        case PORT_PUNCH:        self->punch        = (const float*)data; break;
        case PORT_SUB_THUMP:    self->sub_thump    = (const float*)data; break;
        case PORT_TONE:         self->tone         = (const float*)data; break;
        case PORT_FIXED_VEL:    self->fixed_vel    = (const float*)data; break;
        case PORT_GUITAR_VOL:   self->guitar_vol   = (const float*)data; break;
        case PORT_STOMP_VOL:    self->stomp_vol    = (const float*)data; break;
        case PORT_OUTPUT_MODE:  self->output_mode  = (const float*)data; break;
        case PORT_LED_ACTIVITY: self->led_activity = (float*)data;       break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (self) self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    CyberStompBoxLV2* self = (CyberStompBoxLV2*)instance;
    if (!self || !self->outL) return;

    // Handle bypass
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (self->inL && self->outL && self->inL != self->outL) {
            std::memcpy(self->outL, self->inL, sample_count * sizeof(float));
        }
        if (self->inR && self->outR && self->inR != self->outR) {
            std::memcpy(self->outR, self->inR, sample_count * sizeof(float));
        } else if (!self->inR && self->outR && self->inL) {
            std::memcpy(self->outR, self->inL, sample_count * sizeof(float));
        }
        if (self->led_activity) *self->led_activity = 0.0f;
        return;
    }

    // Update DSP parameters
    if (self->model)       self->engine.setModel(static_cast<int>(*self->model + 0.5f));
    if (self->pitch)       self->engine.setPitchSemitones(*self->pitch);
    if (self->decay)       self->engine.setDecayTime(*self->decay);
    if (self->punch)       self->engine.setPunch(*self->punch);
    if (self->sub_thump)   self->engine.setSubThumpDb(*self->sub_thump);
    if (self->tone)        self->engine.setTone(*self->tone);
    if (self->fixed_vel)   self->engine.setFixedVelocity(*self->fixed_vel >= 0.5f);
    if (self->guitar_vol)  self->engine.setGuitarGainDb(*self->guitar_vol);
    if (self->stomp_vol)   self->engine.setStompGainDb(*self->stomp_vol);
    if (self->output_mode) self->engine.setSplitOutput(*self->output_mode >= 0.5f);

    // 1. Check Footswitch Control Port Trigger (Edge Detection for MIDI Learn / Manual Clicks)
    if (self->trigger_port) {
        float currentTrig = *self->trigger_port;
        // Trigger on rising edge or switch toggle
        if (currentTrig > 0.0f && self->lastTriggerVal == 0.0f) {
            self->engine.trigger(1.0f);
        } else if (std::abs(currentTrig - self->lastTriggerVal) > 0.5f && currentTrig > 0.1f) {
            self->engine.trigger(1.0f);
        }
        self->lastTriggerVal = currentTrig;
    }

    // 2. Parse Incoming MIDI Events (Nektar Pacer Notes & CCs)
    if (self->midi_in && self->urid_midi_event) {
        const uint8_t* ptr = (const uint8_t*)self->midi_in + sizeof(LV2_Atom_Sequence);
        const uint8_t* end = (const uint8_t*)self->midi_in + sizeof(LV2_Atom) + self->midi_in->atom.size;

        while (ptr + sizeof(LV2_Atom_Event) <= end) {
            const LV2_Atom_Event* ev = (const LV2_Atom_Event*)ptr;
            if (ev->body.type == self->urid_midi_event && ev->body.size >= 1) {
                const uint8_t* msg = (const uint8_t*)(ev + 1);
                uint8_t status = msg[0] & 0xF0;

                // Handle MIDI Note On (Note 36 GM Kick, or any footswitch Note)
                if (status == 0x90 && ev->body.size >= 3) {
                    uint8_t vel = msg[2];
                    if (vel > 0) {
                        self->engine.trigger(static_cast<float>(vel) / 127.0f);
                    }
                }
                // Handle MIDI Control Change (CC switches: momentary 127 or toggle >= 64)
                else if (status == 0xB0 && ev->body.size >= 3) {
                    uint8_t val = msg[2];
                    if (val >= 64) {
                        self->engine.trigger(static_cast<float>(val) / 127.0f);
                    }
                }
            }
            uint32_t padded = (sizeof(LV2_Atom_Event) + ev->body.size + 7) & ~7;
            ptr += padded;
        }
    }

    // 3. Process Audio Block
    const float* inL = self->inL ? self->inL : self->outL;
    const float* inR = self->inR ? self->inR : inL;
    float* outL = self->outL;
    float* outR = self->outR ? self->outR : outL;

    self->engine.process(inL, inR, outL, outR, sample_count);

    // 4. Output LED Telemetry
    if (self->led_activity) {
        *self->led_activity = self->engine.getActivityLed();
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    if (instance) std::free(instance);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    CYBER_STOMP_BOX_URI,
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
