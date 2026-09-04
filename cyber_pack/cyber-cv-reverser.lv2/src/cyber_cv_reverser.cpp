#include "lv2.h"
#include <cmath>
#include <cstring>

#define PLUGIN_URI "http://cyberaudio.com/plugins/cyber-cv-reverser"

enum PortIndex {
    PORT_CV_IN       = 0,
    PORT_CV_OUT_REV  = 1,
    PORT_CV_OUT_THRU = 2,
    PORT_MIN_VAL     = 3,
    PORT_MAX_VAL     = 4,
    PORT_SMOOTHING   = 5,
};

class CyberCVReverser {
private:
    const float* p_cv_in;
    float*       p_cv_out_rev;
    float*       p_cv_out_thru;
    const float* p_min_val;
    const float* p_max_val;
    const float* p_smoothing;

    double sample_rate;
    float current_filtered_rev;
    bool initialized;

public:
    CyberCVReverser(double sr) : sample_rate(sr), current_filtered_rev(0.0f), initialized(false) {
        p_cv_in = nullptr;
        p_cv_out_rev = nullptr;
        p_cv_out_thru = nullptr;
        p_min_val = nullptr;
        p_max_val = nullptr;
        p_smoothing = nullptr;
    }

    void connect_port(uint32_t port, void* data) {
        switch ((PortIndex)port) {
            case PORT_CV_IN:       p_cv_in = (const float*)data; break;
            case PORT_CV_OUT_REV:  p_cv_out_rev = (float*)data; break;
            case PORT_CV_OUT_THRU: p_cv_out_thru = (float*)data; break;
            case PORT_MIN_VAL:     p_min_val = (const float*)data; break;
            case PORT_MAX_VAL:     p_max_val = (const float*)data; break;
            case PORT_SMOOTHING:   p_smoothing = (const float*)data; break;
        }
    }

    void activate() {
        initialized = false;
    }

    void run(uint32_t sample_count) {
        float min_pct = p_min_val ? (*p_min_val * 0.01f) : 0.0f;
        float max_pct = p_max_val ? (*p_max_val * 0.01f) : 1.0f;
        bool smooth = p_smoothing ? (*p_smoothing > 0.5f) : true;

        // Ensure bounds
        if (min_pct < 0.0f) min_pct = 0.0f;
        if (min_pct > 1.0f) min_pct = 1.0f;
        if (max_pct < 0.0f) max_pct = 0.0f;
        if (max_pct > 1.0f) max_pct = 1.0f;

        // Smoothing coefficient (~20ms time constant at sample rate)
        float alpha = smooth ? (float)(1.0 - exp(-1.0 / (0.02 * sample_rate))) : 1.0f;

        for (uint32_t i = 0; i < sample_count; ++i) {
            float in_v = p_cv_in ? p_cv_in[i] : 0.0f;
            
            // Normalize incoming 0.0V - 10.0V to 0.0 - 1.0
            float norm = in_v * 0.1f;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;

            // Invert mathematically: 0.0 becomes 1.0, 1.0 becomes 0.0
            float inv_norm = 1.0f - norm;

            // Scale between min and max range
            float target_rev = min_pct + inv_norm * (max_pct - min_pct);
            float target_volts = target_rev * 10.0f;

            if (!initialized) {
                current_filtered_rev = target_volts;
                initialized = true;
            } else {
                current_filtered_rev += alpha * (target_volts - current_filtered_rev);
            }

            if (p_cv_out_rev) {
                p_cv_out_rev[i] = current_filtered_rev;
            }

            if (p_cv_out_thru) {
                p_cv_out_thru[i] = in_v;
            }
        }
    }
};

static LV2_Handle instantiate(const LV2_Descriptor* descriptor,
                              double rate,
                              const char* bundle_path,
                              const LV2_Feature* const* features) {
    return new CyberCVReverser(rate);
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
    static_cast<CyberCVReverser*>(instance)->connect_port(port, data);
}

static void activate(LV2_Handle instance) {
    static_cast<CyberCVReverser*>(instance)->activate();
}

static void run(LV2_Handle instance, uint32_t sample_count) {
    static_cast<CyberCVReverser*>(instance)->run(sample_count);
}

static void deactivate(LV2_Handle instance) {}

static void cleanup(LV2_Handle instance) {
    delete static_cast<CyberCVReverser*>(instance);
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
