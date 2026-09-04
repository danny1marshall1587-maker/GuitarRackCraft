#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "lv2/lv2.h"
#include "GuitarMidiEngine.hpp"

#define GUITAR_MIDI_URI "http://moddevices.com/plugins/danny/guitar-midi"

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
    PORT_AUDIO_IN       = 0,
    PORT_MIDI_OUT       = 1,
    PORT_BYPASS         = 2,
    PORT_GAIN           = 3,
    PORT_SENSITIVITY    = 4,
    PORT_STABILITY      = 5,
    PORT_CHORD_EXT      = 6,
    PORT_LATCH_MODE     = 7,
    PORT_KEY_ROOT       = 8,
    PORT_SCALE_MODE     = 9,
    PORT_BASS_ENABLE    = 10,
    PORT_VEL_MODE       = 11,
    PORT_DETECTED_ROOT  = 12,
    PORT_DETECTED_QUAL  = 13
};

struct GuitarMidiLV2 {
    const float* in;
    LV2_Atom_Sequence* midi_out;

    const float* bypass;
    const float* gain;
    const float* sensitivity;
    const float* stability;
    const float* chord_ext;
    const float* latch_mode;
    const float* key_root;
    const float* scale_mode;
    const float* bass_enable;
    const float* vel_mode;

    float* detected_root;
    float* detected_qual;

    AudioDSP::GuitarMidiEngine engine;
    std::vector<AudioDSP::MidiMessage> midiQueue;

    LV2_URID_Map* map;
    LV2_URID urid_sequence;
    LV2_URID urid_midi_event;

    double sampleRate;
};

static LV2_Handle instantiate(const LV2_Descriptor*     descriptor,
                             double                    sample_rate,
                             const char*               bundle_path,
                             const LV2_Feature* const* features)
{
    (void)descriptor;
    (void)bundle_path;

    GuitarMidiLV2* self = (GuitarMidiLV2*)std::calloc(1, sizeof(GuitarMidiLV2));
    if (!self) return nullptr;

    self->sampleRate = sample_rate;
    self->engine.init(sample_rate);
    self->midiQueue.reserve(128);

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

static void connect_port(LV2_Handle instance, uint32_t port, void* data_location)
{
    GuitarMidiLV2* self = (GuitarMidiLV2*)instance;
    if (!self) return;

    switch (port) {
        case PORT_AUDIO_IN:
            self->in = (const float*)data_location;
            break;
        case PORT_MIDI_OUT:
            self->midi_out = (LV2_Atom_Sequence*)data_location;
            break;
        case PORT_BYPASS:
            self->bypass = (const float*)data_location;
            break;
        case PORT_GAIN:
            self->gain = (const float*)data_location;
            break;
        case PORT_SENSITIVITY:
            self->sensitivity = (const float*)data_location;
            break;
        case PORT_STABILITY:
            self->stability = (const float*)data_location;
            break;
        case PORT_CHORD_EXT:
            self->chord_ext = (const float*)data_location;
            break;
        case PORT_LATCH_MODE:
            self->latch_mode = (const float*)data_location;
            break;
        case PORT_KEY_ROOT:
            self->key_root = (const float*)data_location;
            break;
        case PORT_SCALE_MODE:
            self->scale_mode = (const float*)data_location;
            break;
        case PORT_BASS_ENABLE:
            self->bass_enable = (const float*)data_location;
            break;
        case PORT_VEL_MODE:
            self->vel_mode = (const float*)data_location;
            break;
        case PORT_DETECTED_ROOT:
            self->detected_root = (float*)data_location;
            break;
        case PORT_DETECTED_QUAL:
            self->detected_qual = (float*)data_location;
            break;
        default:
            break;
    }
}

static void activate(LV2_Handle instance)
{
    GuitarMidiLV2* self = (GuitarMidiLV2*)instance;
    if (!self) return;
    self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    GuitarMidiLV2* self = (GuitarMidiLV2*)instance;
    if (!self || sample_count == 0) return;

    uint32_t capacity = 0;
    if (self->midi_out) {
        capacity = self->midi_out->atom.size;
        self->midi_out->atom.type = self->urid_sequence;
        self->midi_out->atom.size = sizeof(LV2_Atom_Sequence_Body);
        self->midi_out->body.unit = 0;
        self->midi_out->body.pad = 0;
    }

    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed || !self->in) {
        if (self->detected_root) *self->detected_root = -1.0f;
        if (self->detected_qual) *self->detected_qual = 0.0f;
        return;
    }

    // Set DSP Parameters
    if (self->gain) self->engine.setInputGainDb(*self->gain);
    if (self->sensitivity) self->engine.setSensitivity(*self->sensitivity);
    if (self->stability) self->engine.setStability(*self->stability);
    if (self->chord_ext) self->engine.setChordExtension(static_cast<int>(*self->chord_ext + 0.5f));
    if (self->latch_mode) self->engine.setLatchMode(*self->latch_mode >= 0.5f);
    if (self->key_root) self->engine.setKeyRoot(static_cast<int>(*self->key_root + 0.5f));
    if (self->scale_mode) self->engine.setScaleMode(static_cast<int>(*self->scale_mode + 0.5f));
    if (self->bass_enable) self->engine.setBassEnable(*self->bass_enable >= 0.5f);
    if (self->vel_mode) self->engine.setDynamicVelocity(*self->vel_mode >= 0.5f);

    // Process Chroma & Chord Tracking
    self->midiQueue.clear();
    self->engine.process(self->in, sample_count, self->midiQueue);

    // Safely Write Events to Host Atom Buffer
    if (self->midi_out && capacity >= sizeof(LV2_Atom_Sequence_Body) + 16) {
        uint8_t* seqBuf = (uint8_t*)self->midi_out;
        uint32_t offset = sizeof(LV2_Atom_Sequence);

        for (const auto& msg : self->midiQueue) {
            uint32_t evSize = sizeof(LV2_Atom_Event) + msg.size;
            uint32_t paddedEvSize = (evSize + 7) & ~7;

            if (offset + paddedEvSize <= sizeof(LV2_Atom) + capacity) {
                LV2_Atom_Event* ev = (LV2_Atom_Event*)(seqBuf + offset);
                ev->time.frames = msg.frameOffset;
                ev->body.type = self->urid_midi_event;
                ev->body.size = msg.size;
                std::memcpy((uint8_t*)ev + sizeof(LV2_Atom_Event), msg.data, msg.size);

                offset += paddedEvSize;
                self->midi_out->atom.size = offset - sizeof(LV2_Atom);
            }
        }
    }

    // Telemetry Outputs
    if (self->detected_root) {
        *self->detected_root = static_cast<float>(self->engine.getActiveRoot());
    }
    if (self->detected_qual) {
        *self->detected_qual = static_cast<float>(self->engine.getActiveQuality());
    }
}

static void deactivate(LV2_Handle instance)
{
    (void)instance;
}

static void cleanup(LV2_Handle instance)
{
    GuitarMidiLV2* self = (GuitarMidiLV2*)instance;
    if (self) std::free(self);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    GUITAR_MIDI_URI,
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
