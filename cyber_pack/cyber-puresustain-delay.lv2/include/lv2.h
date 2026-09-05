/*
 * LV2 Audio Plugin Specification Core Header
 * Compliant with LV2 Core Specification (http://lv2plug.in/ns/lv2core)
 */

#ifndef LV2_H
#define LV2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LV2_CORE_URI    "http://lv2plug.in/ns/lv2core"
#define LV2_CORE_PREFIX LV2_CORE_URI "#"

typedef void* LV2_Handle;

typedef struct _LV2_Feature {
    const char* URI;
    void*       data;
} LV2_Feature;

typedef struct _LV2_Descriptor {
    const char* URI;

    LV2_Handle (*instantiate)(const struct _LV2_Descriptor* descriptor,
                              double                        sample_rate,
                              const char*                   bundle_path,
                              const LV2_Feature* const*     features);

    void (*connect_port)(LV2_Handle instance,
                         uint32_t   port,
                         void*      data_location);

    void (*activate)(LV2_Handle instance);

    void (*run)(LV2_Handle instance,
                uint32_t   sample_count);

    void (*deactivate)(LV2_Handle instance);

    void (*cleanup)(LV2_Handle instance);

    const void* (*extension_data)(const char* uri);
} LV2_Descriptor;

#if defined(_WIN32) || defined(__CYGWIN__)
  #define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
  #define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index);

typedef const LV2_Descriptor* (*LV2_Descriptor_Function)(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* LV2_H */
