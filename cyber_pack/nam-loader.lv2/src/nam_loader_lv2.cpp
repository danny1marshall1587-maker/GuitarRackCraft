#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "lv2/lv2.h"
#include "NamNeuralEngine.hpp"

#define NAM_LOADER_URI      "http://moddevices.com/plugins/danny/nam-loader"
#define LV2_STATE__interface "http://lv2plug.in/ns/ext/state#interface"
#define LV2_ATOM__Sequence   "http://lv2plug.in/ns/ext/atom#Sequence"
#define LV2_MIDI__MidiEvent  "http://lv2plug.in/ns/ext/midi#MidiEvent"
#define LV2_URID__map        "http://lv2plug.in/ns/ext/urid#map"

// ─── Minimal Atom / MIDI structs (no external headers needed) ────────────────
typedef uint32_t LV2_URID;

typedef struct {
    uint32_t size;
    LV2_URID type;
} LV2_Atom;

typedef struct {
    LV2_Atom atom;
    int64_t  frames;
} LV2_Atom_Event;

typedef struct {
    LV2_Atom        atom;
    // body follows: pairs of LV2_Atom_Event
} LV2_Atom_Sequence;

typedef struct {
    void*      handle;
    LV2_URID (*map)(void* handle, const char* uri);
} LV2_URID_Map_Struct;


// LV2 state typedefs
typedef void* LV2_State_Handle;
typedef enum {
    LV2_STATE_SUCCESS         = 0,
    LV2_STATE_ERR_UNKNOWN     = 1,
    LV2_STATE_ERR_BAD_TYPE    = 2,
    LV2_STATE_ERR_BAD_FLAGS   = 3,
    LV2_STATE_ERR_NO_SPACE    = 4
} LV2_State_Status;
typedef LV2_State_Status (*LV2_State_Store_Function)(LV2_State_Handle, uint32_t, const void*, size_t, uint32_t, uint32_t);
typedef const void* (*LV2_State_Retrieve_Function)(LV2_State_Handle, uint32_t, size_t*, uint32_t*, uint32_t*);
typedef struct {
    LV2_State_Status (*save)(LV2_Handle, LV2_State_Store_Function, LV2_State_Handle, uint32_t, const LV2_Feature* const*);
    LV2_State_Status (*restore)(LV2_Handle, LV2_State_Retrieve_Function, LV2_State_Handle, uint32_t, const LV2_Feature* const*);
} LV2_State_Interface;

// ─── Port indices ─────────────────────────────────────────────────────────────
enum PortIndex {
    PORT_AUDIO_IN    = 0,
    PORT_AUDIO_OUT   = 1,
    PORT_BYPASS      = 2,
    PORT_PROFILE_A   = 3,   // Slot A model (renamed from profile)
    PORT_DRIVE       = 4,
    PORT_BASS        = 5,
    PORT_MID         = 6,
    PORT_TREBLE      = 7,
    PORT_VOLUME      = 8,
    PORT_GATE        = 9,
    PORT_CAB_ENABLE  = 10,
    PORT_QUALITY     = 11,  // NEW: 0=Slim, 1=Full
    PORT_MIDI_IN     = 12,  // NEW: Atom MIDI input
    PORT_MIDI_CC     = 13,  // NEW: CC number for slot switching (0-127)
    PORT_ACTIVE_SLOT = 14,  // NEW: 0=Slot A, 1=Slot B (control/display)
    PORT_PROFILE_B   = 15,  // NEW: Slot B model selector
};

struct NamLoaderLV2 {
    // Audio
    const float* in;
    float*       out;
    // Controls
    const float* bypass;
    const float* profile_a;
    const float* profile_b;
    const float* drive;
    const float* bass;
    const float* mid;
    const float* treble;
    const float* volume;
    const float* gate;
    const float* cab_enable;
    const float* quality;
    const float* midi_cc;
    const float* active_slot;
    // MIDI atom port
    const LV2_Atom_Sequence* midi_in;

    // URIDs
    LV2_URID urid_atom_sequence;
    LV2_URID urid_midi_event;

    AudioDSP::NamNeuralEngine engine;
    double sampleRate;
};

// ─── Alignment helper for Atom sequence iteration ────────────────────────────
static inline uint32_t lv2_atom_pad_size(uint32_t size) {
    return (size + 7u) & (~7u);
}

static LV2_Handle instantiate(const LV2_Descriptor*, double rate,
                               const char*, const LV2_Feature* const* features)
{
    NamLoaderLV2* self = (NamLoaderLV2*)std::calloc(1, sizeof(NamLoaderLV2));
    if (!self) return nullptr;

    self->sampleRate = rate;
    self->engine.init(rate);

    // Map URIDs from features
    for (int i = 0; features[i]; ++i) {
        if (features[i]->URI && std::strcmp(features[i]->URI, LV2_URID__map) == 0) {
            LV2_URID_Map_Struct* m = (LV2_URID_Map_Struct*)features[i]->data;
            if (m && m->map) {
                self->urid_atom_sequence = m->map(m->handle, LV2_ATOM__Sequence);
                self->urid_midi_event    = m->map(m->handle, LV2_MIDI__MidiEvent);
            }
        }
    }

    return (LV2_Handle)self;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self) return;
    switch (port) {
        case PORT_AUDIO_IN:    self->in           = (const float*)data; break;
        case PORT_AUDIO_OUT:   self->out          = (float*)data;       break;
        case PORT_BYPASS:      self->bypass       = (const float*)data; break;
        case PORT_PROFILE_A:   self->profile_a    = (const float*)data; break;
        case PORT_PROFILE_B:   self->profile_b    = (const float*)data; break;
        case PORT_DRIVE:       self->drive        = (const float*)data; break;
        case PORT_BASS:        self->bass         = (const float*)data; break;
        case PORT_MID:         self->mid          = (const float*)data; break;
        case PORT_TREBLE:      self->treble       = (const float*)data; break;
        case PORT_VOLUME:      self->volume       = (const float*)data; break;
        case PORT_GATE:        self->gate         = (const float*)data; break;
        case PORT_CAB_ENABLE:  self->cab_enable   = (const float*)data; break;
        case PORT_QUALITY:     self->quality      = (const float*)data; break;
        case PORT_MIDI_IN:     self->midi_in      = (const LV2_Atom_Sequence*)data; break;
        case PORT_MIDI_CC:     self->midi_cc      = (const float*)data; break;
        case PORT_ACTIVE_SLOT: self->active_slot  = (const float*)data; break;
        default: break;
    }
}

static void activate(LV2_Handle instance)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (self) self->engine.reset();
}

static void run(LV2_Handle instance, uint32_t sample_count)
{
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !self->out || sample_count == 0) return;

    const float* in  = self->in ? self->in : self->out;
    float*       out = self->out;

    // ── Bypass ───────────────────────────────────────────────────────────────
    bool isBypassed = (self->bypass && *self->bypass < 0.5f);
    if (isBypassed) {
        if (out != in) std::memcpy(out, in, sample_count * sizeof(float));
        return;
    }

    // ── MIDI CC Slot Switching ───────────────────────────────────────────────
    if (self->midi_in && self->urid_atom_sequence &&
        self->midi_in->atom.type == self->urid_atom_sequence)
    {
        int targetCC = self->midi_cc ? (int)(*self->midi_cc + 0.5f) : 20;
        // Walk atom sequence
        const uint8_t* body_ptr = (const uint8_t*)(self->midi_in + 1);
        uint32_t body_size = self->midi_in->atom.size - sizeof(int64_t);
        uint32_t offset = 0;
        while (offset + sizeof(LV2_Atom_Event) <= body_size) {
            const LV2_Atom_Event* ev = (const LV2_Atom_Event*)(body_ptr + offset);
            if (ev->atom.size == 0) break;
            if (ev->atom.type == self->urid_midi_event && ev->atom.size >= 3) {
                const uint8_t* msg = (const uint8_t*)(ev + 1);
                uint8_t status = msg[0] & 0xF0;
                if (status == 0xB0) { // Control Change
                    int cc_num = (int)msg[1];
                    int cc_val = (int)msg[2];
                    if (cc_num == targetCC) {
                        // CC value: 0–63 = Slot A, 64–127 = Slot B
                        int newSlot = (cc_val >= 64) ? 1 : 0;
                        self->engine.setActiveSlot(newSlot);
                    }
                }
            }
            uint32_t evSize = sizeof(LV2_Atom_Event) + lv2_atom_pad_size(ev->atom.size);
            if (evSize == 0) break;
            offset += evSize;
        }
    }

    // ── Update engine parameters from control ports ───────────────────────────
    if (self->profile_a)
        self->engine.setProfileA((int)(*self->profile_a + 0.5f));
    if (self->profile_b)
        self->engine.setProfileB((int)(*self->profile_b + 0.5f));

    // Active slot from control port (can also be changed by MIDI above)
    if (self->active_slot)
        self->engine.setActiveSlot((int)(*self->active_slot + 0.5f));

    if (self->drive)      self->engine.setDrive(*self->drive);
    if (self->bass)       self->engine.setBass(*self->bass);
    if (self->mid)        self->engine.setMid(*self->mid);
    if (self->treble)     self->engine.setTreble(*self->treble);
    if (self->volume)     self->engine.setVolume(*self->volume);
    if (self->gate)       self->engine.setGateThreshold(*self->gate);
    if (self->cab_enable) self->engine.setCabEnabled(*self->cab_enable >= 0.5f);
    if (self->quality)    self->engine.setSlimMode(*self->quality < 0.5f);

    self->engine.process(in, out, sample_count);
}

static void deactivate(LV2_Handle) {}
static void cleanup(LV2_Handle instance)
{
    if (instance) std::free(instance);
}

// ─── State save/restore ───────────────────────────────────────────────────────
static LV2_State_Status state_save(LV2_Handle instance,
                                   LV2_State_Store_Function store,
                                   LV2_State_Handle handle,
                                   uint32_t flags,
                                   const LV2_Feature* const* features)
{
    (void)flags; (void)features;
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !store) return LV2_STATE_ERR_UNKNOWN;
    int32_t profileA = (int32_t)self->engine.getProfileA().id;
    int32_t profileB = (int32_t)self->engine.getProfileB().id;
    int32_t slot     = (int32_t)self->engine.getActiveSlot();
    store(handle, 1, &profileA, sizeof(int32_t), 0, 0);
    store(handle, 2, &profileB, sizeof(int32_t), 0, 0);
    store(handle, 3, &slot,     sizeof(int32_t), 0, 0);
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status state_restore(LV2_Handle instance,
                                      LV2_State_Retrieve_Function retrieve,
                                      LV2_State_Handle handle,
                                      uint32_t flags,
                                      const LV2_Feature* const* features)
{
    (void)flags; (void)features;
    NamLoaderLV2* self = (NamLoaderLV2*)instance;
    if (!self || !retrieve) return LV2_STATE_ERR_UNKNOWN;
    size_t sz = 0; uint32_t ty = 0, fl = 0;
    const void* v;
    v = retrieve(handle, 1, &sz, &ty, &fl);
    if (v && sz >= sizeof(int32_t)) self->engine.setProfileA(*(const int32_t*)v);
    v = retrieve(handle, 2, &sz, &ty, &fl);
    if (v && sz >= sizeof(int32_t)) self->engine.setProfileB(*(const int32_t*)v);
    v = retrieve(handle, 3, &sz, &ty, &fl);
    if (v && sz >= sizeof(int32_t)) self->engine.setActiveSlot(*(const int32_t*)v);
    return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_interface = { state_save, state_restore };

static const void* extension_data(const char* uri)
{
    if (uri && std::strcmp(uri, LV2_STATE__interface) == 0)
        return &state_interface;
    return nullptr;
}

static const LV2_Descriptor descriptor = {
    NAM_LOADER_URI,
    instantiate, connect_port, activate, run, deactivate, cleanup, extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return (index == 0) ? &descriptor : nullptr;
}
