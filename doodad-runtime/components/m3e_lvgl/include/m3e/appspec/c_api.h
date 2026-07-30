#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

int m3e_appspec_render_canonical_cbor(
    lv_obj_t* root,
    const uint8_t* bytes,
    size_t size,
    char* error,
    size_t error_size);

typedef void (*m3e_appspec_event_callback_t)(
    const uint8_t* canonical_event,
    size_t size,
    void* context);

int m3e_appspec_render_canonical_cbor_with_events(
    lv_obj_t* root,
    const uint8_t* bytes,
    size_t size,
    m3e_appspec_event_callback_t callback,
    void* callback_context,
    char* error,
    size_t error_size);

int m3e_appspec_apply_command_batch(
    const uint8_t* bytes,
    size_t size,
    char* error,
    size_t error_size);

#ifdef __cplusplus
}
#endif
