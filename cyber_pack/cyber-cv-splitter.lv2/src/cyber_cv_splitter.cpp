#include "lv2.h"
#include <cmath>
#include <cstring>

#define PLUGIN_URI "http://cyberaudio.com/plugins/cyber-cv-splitter"

enum PortIndex {
    PORT_CONTROL_IN = 0,
    PORT_CV_OUT_NORM = 1,
    PORT_CV_OUT_INV = 2,
    PORT_MIN_VAL = 3,
    PORT_MAX_VAL = 4,
    PORT_CV_OUT_SCALED = 5,
};

class CyberCVSplitter {
private:
    const float* p_control_in;
    const float* p_min_val;
    const float* p_max_val;
    
    float* p_cv_out_norm;
    float* p_cv_out_inv;
    float* p_cv_out_scaled;
    
    double sample_rate;

public:
    CyberCVSplitter(double sr) : sample_rate(sr) {
        p_control_in = nullptr;
        p_min_val = nullptr;
        p_max_val = nullptr;
        p_cv_out_norm = nullptr;
        p_cv_out_inv = nullptr;
        p_cv_out_scaled = nullptr;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_CONTROL_IN:    p_control_in = (const float*)data; break;
            case PORT_MIN_VAL:       p_min_val = (const float*)data; break;
            case PORT_MAX_VAL:       p_max_val = (const float*)data; break;
            case PORT_CV_OUT_NORM:   p_cv_out_norm = (float*)data; break;
            case PORT_CV_OUT_INV:    p_cv_out_inv = (float*)data; break;
            case PORT_CV_OUT_SCALED: p_cv_out_scaled = (float*)data; break;
        }
    }

    void activate() {
        // Nothing to reset
    }

    void run(uint32_t sample_count) {
        float ctrl = p_control_in ? *p_control_in : 0.0f;
        // Map 0-100 control down to 0.0-1.0 mathematically internally
        float norm = ctrl * 0.01f;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        
        float inv = 1.0f - norm;
        
        float min_v = p_min_val ? (*p_min_val * 0.01f) : 0.0f;
        float max_v = p_max_val ? (*p_max_val * 0.01f) : 1.0f;
        
        float scaled = min_v + norm * (max_v - min_v);

        // CV ports operate at audio rate (one sample per audio frame)
        // Since the control port changes only once per block (or sparsely), 
        // we can just fill the CV buffer with the calculated constant.
        // To prevent zipper noise on parameters connected to CV, we could interpolate, 
        // but for an expression pedal, block-rate update is generally fine.
        
        if (p_cv_out_norm) {
            for (uint32_t i = 0; i < sample_count; ++i) {
                p_cv_out_norm[i] = norm * 10.0f;
            }
        }
        
        if (p_cv_out_inv) {
            for (uint32_t i = 0; i < sample_count; ++i) {
                p_cv_out_inv[i] = inv * 10.0f;
            }
        }
        
        if (p_cv_out_scaled) {
            for (uint32_t i = 0; i < sample_count; ++i) {
                p_cv_out_scaled[i] = scaled * 10.0f;
            }
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    return new CyberCVSplitter(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    static_cast<CyberCVSplitter*>(instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    static_cast<CyberCVSplitter*>(instance)->activate();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    static_cast<CyberCVSplitter*>(instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete static_cast<CyberCVSplitter*>(instance);
}

static const void* extension_data(const char* uri) {
    return nullptr;
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

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
