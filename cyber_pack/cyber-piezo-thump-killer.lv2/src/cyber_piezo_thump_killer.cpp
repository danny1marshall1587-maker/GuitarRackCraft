#include <lv2.h>
#include "PiezoThumpEngine.hpp"

#define PLUGIN_URI "http://cyberaudio.com/plugins/cyber-piezo-thump-killer"

enum PortIndex {
    PORT_AUDIO_IN    = 0,
    PORT_AUDIO_OUT   = 1,
    PORT_BYPASS      = 2,
    PORT_HPF_FREQ    = 3,
    PORT_THUMP_FREQ  = 4,
    PORT_THUMP_SENS  = 5,
    PORT_THUMP_DEPTH = 6,
    PORT_MUD_FREQ    = 7,
    PORT_MUD_SENS    = 8,
    PORT_MUD_DEPTH   = 9,
    PORT_HARMONICS   = 10,
    PORT_PEAK_CLAMP  = 11,
    PORT_LISTEN      = 12,
    PORT_OUT_GAIN    = 13,
    PORT_THUMP_GR    = 14,
    PORT_MUD_GR      = 15
};

class CyberPiezoThumpKillerPlugin {
private:
    const float* p_in;
    float* p_out;
    const float* p_bypass;
    const float* p_hpf_freq;
    const float* p_thump_freq;
    const float* p_thump_sens;
    const float* p_thump_depth;
    const float* p_mud_freq;
    const float* p_mud_sens;
    const float* p_mud_depth;
    const float* p_harmonics;
    const float* p_peak_clamp;
    const float* p_listen;
    const float* p_out_gain;
    float* p_thump_gr;
    float* p_mud_gr;

    PiezoThumpEngine engine;

public:
    CyberPiezoThumpKillerPlugin(double sample_rate)
        : p_in(nullptr), p_out(nullptr), p_bypass(nullptr),
          p_hpf_freq(nullptr), p_thump_freq(nullptr), p_thump_sens(nullptr), p_thump_depth(nullptr),
          p_mud_freq(nullptr), p_mud_sens(nullptr), p_mud_depth(nullptr),
          p_harmonics(nullptr), p_peak_clamp(nullptr), p_listen(nullptr), p_out_gain(nullptr),
          p_thump_gr(nullptr), p_mud_gr(nullptr)
    {
        engine.init(sample_rate);
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_AUDIO_IN:    p_in = (const float*)data; break;
            case PORT_AUDIO_OUT:   p_out = (float*)data; break;
            case PORT_BYPASS:      p_bypass = (const float*)data; break;
            case PORT_HPF_FREQ:    p_hpf_freq = (const float*)data; break;
            case PORT_THUMP_FREQ:  p_thump_freq = (const float*)data; break;
            case PORT_THUMP_SENS:  p_thump_sens = (const float*)data; break;
            case PORT_THUMP_DEPTH: p_thump_depth = (const float*)data; break;
            case PORT_MUD_FREQ:    p_mud_freq = (const float*)data; break;
            case PORT_MUD_SENS:    p_mud_sens = (const float*)data; break;
            case PORT_MUD_DEPTH:   p_mud_depth = (const float*)data; break;
            case PORT_HARMONICS:   p_harmonics = (const float*)data; break;
            case PORT_PEAK_CLAMP:  p_peak_clamp = (const float*)data; break;
            case PORT_LISTEN:      p_listen = (const float*)data; break;
            case PORT_OUT_GAIN:    p_out_gain = (const float*)data; break;
            case PORT_THUMP_GR:    p_thump_gr = (float*)data; break;
            case PORT_MUD_GR:      p_mud_gr = (float*)data; break;
        }
    }

    void activate() {
        engine.reset();
    }

    void run(uint32_t sample_count) {
        if (!p_in || !p_out) return;

        bool active = p_bypass ? (*p_bypass > 0.5f) : true;
        if (!active) {
            std::memcpy(p_out, p_in, sizeof(float) * sample_count);
            if (p_thump_gr) *p_thump_gr = 0.0f;
            if (p_mud_gr) *p_mud_gr = 0.0f;
            return;
        }

        float hpf_freq    = p_hpf_freq ? *p_hpf_freq : 40.0f;
        float thump_freq  = p_thump_freq ? *p_thump_freq : 95.0f;
        float thump_sens  = p_thump_sens ? *p_thump_sens : 50.0f;
        float thump_depth = p_thump_depth ? *p_thump_depth : 14.0f;
        float mud_freq    = p_mud_freq ? *p_mud_freq : 220.0f;
        float mud_sens    = p_mud_sens ? *p_mud_sens : 50.0f;
        float mud_depth   = p_mud_depth ? *p_mud_depth : 10.0f;
        float harmonics   = p_harmonics ? *p_harmonics : 100.0f;
        float peak_clamp  = p_peak_clamp ? *p_peak_clamp : -4.0f;
        float listen_mode = p_listen ? *p_listen : 0.0f;
        float out_gain_db = p_out_gain ? *p_out_gain : 0.0f;

        engine.process(p_in, p_out, sample_count,
                       hpf_freq,
                       thump_freq, thump_sens, thump_depth,
                       mud_freq, mud_sens, mud_depth,
                       harmonics,
                       peak_clamp,
                       listen_mode, out_gain_db);

        if (p_thump_gr) *p_thump_gr = engine.getThumpReductionDb();
        if (p_mud_gr) *p_mud_gr = engine.getMudReductionDb();
    }

    void deactivate() {}
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
    return (LV2_Handle)new CyberPiezoThumpKillerPlugin(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    ((CyberPiezoThumpKillerPlugin*)instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    ((CyberPiezoThumpKillerPlugin*)instance)->activate();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    ((CyberPiezoThumpKillerPlugin*)instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {
    ((CyberPiezoThumpKillerPlugin*)instance)->deactivate();
}

static void cleanup(LV2_Handle instance) {
    delete (CyberPiezoThumpKillerPlugin*)instance;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    nullptr
};

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
