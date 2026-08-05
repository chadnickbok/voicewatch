#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DOODAD_TEXT_STYLE_DISPLAY = 0,
    DOODAD_TEXT_STYLE_TITLE = 1,
    DOODAD_TEXT_STYLE_BODY = 2,
    DOODAD_TEXT_STYLE_CAPTION = 3,
    DOODAD_TEXT_STYLE_MUTED = 4,
};

enum {
    DOODAD_DIRECTION_COLUMN = 0,
    DOODAD_DIRECTION_ROW = 1,
};

enum {
    DOODAD_ALIGN_START = 0,
    DOODAD_ALIGN_CENTER = 1,
    DOODAD_ALIGN_END = 2,
    DOODAD_ALIGN_STRETCH = 3,
};

enum {
    DOODAD_SCENE_CAUSE_START = 0,
    DOODAD_SCENE_CAUSE_SEMANTIC_EVENT = 1,
    DOODAD_SCENE_CAUSE_PROVIDER_EVENT = 2,
    DOODAD_SCENE_CAUSE_TIMER_EVENT = 3,
    DOODAD_SCENE_CAUSE_REPLAY = 4,
};

enum {
    DOODAD_SCENE_OPERATION_APPSPEC_MOUNT = 0,
    DOODAD_SCENE_OPERATION_COMMAND_BATCH = 1,
};

enum {
    DOODAD_SCENE_OUTCOME_COMMITTED = 0,
    DOODAD_SCENE_OUTCOME_REJECTED = 1,
};

typedef void (*doodad_host_scene_operation_callback_t)(
    void* context,
    uint64_t scene_revision,
    uint64_t route_generation,
    uint64_t scenario_ms,
    int cause_kind,
    const uint8_t* cause,
    size_t cause_size,
    int operation_kind,
    int outcome,
    const uint8_t* operation,
    size_t operation_size,
    const char* snapshot_json,
    size_t snapshot_size);

int doodad_host_create(void);
void doodad_host_destroy(void);
const char* doodad_host_last_error(void);

int doodad_host_start_wasm(const char* path);
void doodad_host_render_now(void);
const uint16_t* doodad_host_framebuffer(void);
size_t doodad_host_framebuffer_pixels(void);
void doodad_host_show_catalog(int story);
int doodad_host_start_system_shell(
    const char* app_id,
    const char* app_name,
    const char* app_detail,
    const char* wasm_path);
int doodad_host_click_system_action(const char* action_id);
int doodad_host_system_back(void);
int doodad_host_system_home(void);
int doodad_host_system_surface(void);
int doodad_host_system_advance_animation(uint32_t milliseconds);
int doodad_host_show_appspec(const uint8_t* bytes, size_t size);
int doodad_host_click_first_action(void);
int doodad_host_click_button(const char* label);
int doodad_host_dispatch_semantic_action(
    const char* node_id,
    const char* action_id,
    int event_kind,
    int value_kind,
    int32_t integer_value,
    int boolean_value,
    const char* text_value);
const char* doodad_host_node_text(const char* node_id);
size_t doodad_host_semantic_snapshot(char* output, size_t output_size);
size_t doodad_host_scene_snapshot(char* output, size_t output_size);
size_t doodad_host_node_layout_evidence(
    char* output,
    size_t output_size);
void doodad_host_set_scene_operation_callback(
    doodad_host_scene_operation_callback_t callback,
    void* context);
uint64_t doodad_host_scene_revision(void);
uint64_t doodad_host_route_generation(void);
int doodad_host_set_font_scale_milli(uint16_t scale_milli);
uint16_t doodad_host_font_scale_milli(void);
int doodad_host_replay_mount(
    const uint8_t* bytes,
    size_t size,
    uint64_t scenario_ms);
int doodad_host_replay_command_batch(
    const uint8_t* bytes,
    size_t size,
    uint64_t scenario_ms);
uint64_t doodad_host_wasm_call_count(void);
size_t doodad_host_mounted_node_count(void);
size_t doodad_host_mounted_event_count(void);
size_t doodad_host_lvgl_object_count(void);
size_t doodad_host_lvgl_max_depth(void);
uint64_t doodad_host_semantic_event_count(void);
uint64_t doodad_host_provider_request_count(void);
void doodad_host_set_display_awake(int awake);
int doodad_host_display_awake(void);
int doodad_host_advance_time(uint64_t milliseconds);
uint64_t doodad_host_scenario_time(void);
int doodad_host_deliver_provider(void);
int doodad_host_deliver_weather_payload(
    const uint8_t* payload,
    size_t payload_size,
    uint8_t freshness);

void* doodad_host_ui_begin_document(int direction, int align, int gap);
void* doodad_host_ui_add_stack(
    void* parent, int direction, int align, int gap);
void* doodad_host_ui_add_text(void* parent, const char* text, int style);
void* doodad_host_ui_add_button(
    void* parent, const char* identifier, const char* label, int disabled);
void* doodad_host_ui_add_progress(
    void* parent, const char* label, int value, int maximum);

#ifdef __cplusplus
}
#endif
