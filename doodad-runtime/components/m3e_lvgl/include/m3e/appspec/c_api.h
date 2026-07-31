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

enum {
    M3E_APPSPEC_EVENT_VALUE_NONE = 0,
    M3E_APPSPEC_EVENT_VALUE_INTEGER = 1,
    M3E_APPSPEC_EVENT_VALUE_BOOLEAN = 2,
    M3E_APPSPEC_EVENT_VALUE_TEXT = 3,
};

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

int m3e_appspec_emit_semantic_event(
    const char* node_id,
    const char* action_id,
    int event_kind,
    uint64_t timestamp_ms,
    int value_kind,
    int32_t integer_value,
    int boolean_value,
    const char* text_value,
    char* error,
    size_t error_size);

const char* m3e_appspec_mounted_text(
    const char* node_id,
    int secondary);

size_t m3e_appspec_semantic_snapshot(
    char* output,
    size_t output_size);

size_t m3e_appspec_scene_snapshot_json(
    char* output,
    size_t output_size);

size_t m3e_appspec_node_layout_evidence_json(
    char* output,
    size_t output_size);

void m3e_appspec_reset_mounted_document(void);

size_t m3e_appspec_mounted_node_count(void);
size_t m3e_appspec_mounted_event_count(void);

#ifdef __cplusplus
}
#endif
