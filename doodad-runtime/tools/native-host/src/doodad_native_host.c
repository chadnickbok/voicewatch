#include "doodad_native_host.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bh_platform.h"
#include "doodad_lvgl_ui.h"
#include "lvgl.h"
#include "m3e/appspec/c_api.h"
#include "m3e/catalog/catalog.h"
#include "wasm_export.h"

enum {
    kSurfaceWidth = 240,
    kSurfaceHeight = 240,
    kDrawRows = 24,
    kMaximumModuleBytes = 256 * 1024,
    kMaximumAppSpecBytes = 4096,
    kMaximumCommandBatchBytes = 4096,
    kWasmStackBytes = 16 * 1024,
    kWasmHeapBytes = 16 * 1024,
    kExecEnvStackBytes = 8 * 1024,
};

static uint16_t g_framebuffer[kSurfaceWidth * kSurfaceHeight];
static uint16_t g_draw_buffer[kSurfaceWidth * kDrawRows];
static lv_display_t* g_display;
static doodad_lvgl_ui_t g_ui;

static wasm_module_t g_module;
static wasm_module_inst_t g_instance;
static wasm_exec_env_t g_execution_environment;
static wasm_function_inst_t g_handle_event;
static uint8_t* g_module_bytes;
static bool g_runtime_ready;
static bool g_semantic_mount_called;
static uint8_t g_pending_event[512];
static size_t g_pending_event_size;
static char g_last_error[256];

static void set_error(const char* message) {
    snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

static uint32_t tick_milliseconds(void) {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint32_t)(value.tv_sec * 1000ULL + value.tv_nsec / 1000000ULL);
}

static void flush_framebuffer(
    lv_display_t* display, const lv_area_t* area, uint8_t* pixel_map) {
    const uint16_t* source = (const uint16_t*)pixel_map;
    const int32_t width = area->x2 - area->x1 + 1;
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t* destination =
            &g_framebuffer[y * kSurfaceWidth + area->x1];
        memcpy(destination, source, (size_t)width * sizeof(uint16_t));
        source += width;
    }
    lv_display_flush_ready(display);
}

static int32_t reject_guest_appspec(
    wasm_module_inst_t instance, const char* reason) {
    char message[192];
    snprintf(message, sizeof(message), "INVALID APPSPEC: %s", reason);
    set_error(message);
    doodad_lvgl_ui_show_error(&g_ui, "INVALID APPSPEC");
    wasm_runtime_set_exception(instance, "INVALID APPSPEC");
    return 0;
}

static int dispatch_guest_event(const uint8_t* bytes, size_t length) {
    if (g_instance == NULL || g_execution_environment == NULL
        || g_handle_event == NULL || length == 0 || length > 512) {
        set_error("guest event handler unavailable");
        return 0;
    }
    void* guest_native = NULL;
    const uint64_t guest_pointer =
        wasm_runtime_module_malloc(g_instance, length, &guest_native);
    if (guest_pointer == 0 || guest_pointer > UINT32_MAX
        || guest_native == NULL) {
        set_error("guest event allocation failed");
        return 0;
    }
    memcpy(guest_native, bytes, length);
    uint32_t arguments[2] = {
        (uint32_t)guest_pointer,
        (uint32_t)length,
    };
    const bool called = wasm_runtime_call_wasm(
        g_execution_environment, g_handle_event, 2, arguments);
    wasm_runtime_module_free(g_instance, guest_pointer);
    if (!called) {
        const char* exception = wasm_runtime_get_exception(g_instance);
        set_error(exception != NULL ? exception : "handle_event trapped");
        return 0;
    }
    uint64_t packed_result = 0;
    memcpy(&packed_result, arguments, sizeof(packed_result));
    const uint32_t result_pointer = (uint32_t)(packed_result >> 32);
    const uint32_t result_length = (uint32_t)packed_result;
    if (result_pointer == 0 || result_length == 0
        || result_length > kMaximumCommandBatchBytes) {
        set_error("guest returned invalid CommandBatch slice");
        return 0;
    }
    if (!wasm_runtime_validate_app_addr(
            g_instance, result_pointer, result_length)) {
        set_error("guest CommandBatch pointer outside linear memory");
        return 0;
    }
    const uint8_t* result_native =
        (const uint8_t*)wasm_runtime_addr_app_to_native(
            g_instance, result_pointer);
    if (result_native == NULL) {
        set_error("guest CommandBatch address translation failed");
        return 0;
    }
    uint8_t copied_result[kMaximumCommandBatchBytes];
    memcpy(copied_result, result_native, result_length);
    char error[192];
    memset(error, 0, sizeof(error));
    if (!m3e_appspec_apply_command_batch(
            copied_result, result_length, error, sizeof(error))) {
        set_error(error);
        return 0;
    }
    doodad_host_render_now();
    return 1;
}

static void host_semantic_event(
    const uint8_t* bytes, size_t length, void* context) {
    (void)context;
    if (length == 0 || length > sizeof(g_pending_event)) {
        set_error("semantic event exceeds host queue slot");
        return;
    }
    memcpy(g_pending_event, bytes, length);
    g_pending_event_size = length;
}

static int32_t host_ui_mount(
    wasm_exec_env_t environment, uint32_t pointer, uint32_t length) {
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(environment);
    if (length == 0 || length > kMaximumAppSpecBytes) {
        return reject_guest_appspec(
            instance, "length outside 1..4096 bytes");
    }
    if (!wasm_runtime_validate_app_addr(instance, pointer, length)) {
        return reject_guest_appspec(
            instance, "pointer outside guest linear memory");
    }

    const uint8_t* bytes =
        (const uint8_t*)wasm_runtime_addr_app_to_native(instance, pointer);
    if (bytes == NULL) {
        return reject_guest_appspec(
            instance, "guest address translation failed");
    }
    char error[192];
    memset(error, 0, sizeof(error));
    if (!m3e_appspec_render_canonical_cbor_with_events(
            lv_screen_active(),
            bytes,
            length,
            host_semantic_event,
            NULL,
            error,
            sizeof(error))) {
        return reject_guest_appspec(instance, error);
    }
    g_semantic_mount_called = true;
    return 1;
}

static NativeSymbol g_native_symbols[] = {
    {
        .symbol = "ui_mount",
        .func_ptr = (void*)host_ui_mount,
        .signature = "(ii)i",
        .attachment = NULL,
    },
};

static void release_wasm(void) {
    g_handle_event = NULL;
    if (g_execution_environment != NULL) {
        wasm_runtime_destroy_exec_env(g_execution_environment);
        g_execution_environment = NULL;
    }
    if (g_instance != NULL) {
        wasm_runtime_deinstantiate(g_instance);
        g_instance = NULL;
    }
    if (g_module != NULL) {
        wasm_runtime_unload(g_module);
        g_module = NULL;
    }
    free(g_module_bytes);
    g_module_bytes = NULL;
}

int doodad_host_create(void) {
    memset(g_last_error, 0, sizeof(g_last_error));
    memset(g_framebuffer, 0, sizeof(g_framebuffer));

    lv_init();
    lv_tick_set_cb(tick_milliseconds);
    g_display = lv_display_create(kSurfaceWidth, kSurfaceHeight);
    if (g_display == NULL) {
        set_error("LVGL display creation failed");
        return 0;
    }
    lv_display_set_color_format(g_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        g_display,
        g_draw_buffer,
        NULL,
        sizeof(g_draw_buffer),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_display, flush_framebuffer);
    doodad_lvgl_ui_init(&g_ui, lv_screen_active());
    doodad_lvgl_ui_show_shell(&g_ui, "STARTING", "DEV");

    RuntimeInitArgs arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.mem_alloc_type = Alloc_With_Allocator;
    arguments.mem_alloc_option.allocator.malloc_func = (void*)os_malloc;
    arguments.mem_alloc_option.allocator.realloc_func = (void*)os_realloc;
    arguments.mem_alloc_option.allocator.free_func = (void*)os_free;
    if (!wasm_runtime_full_init(&arguments)) {
        set_error("WAMR initialization failed");
        doodad_lvgl_ui_show_error(&g_ui, "WAMR INIT FAILED");
        return 0;
    }
    if (!wasm_runtime_register_natives(
            "doodad",
            g_native_symbols,
            sizeof(g_native_symbols) / sizeof(g_native_symbols[0]))) {
        set_error("WAMR native ABI registration failed");
        doodad_lvgl_ui_show_error(&g_ui, "WAMR INIT FAILED");
        wasm_runtime_destroy();
        return 0;
    }
    g_runtime_ready = true;
    doodad_host_render_now();
    return 1;
}

void doodad_host_destroy(void) {
    release_wasm();
    if (g_runtime_ready) {
        wasm_runtime_destroy();
        g_runtime_ready = false;
    }
    if (g_display != NULL) {
        lv_display_delete(g_display);
        g_display = NULL;
    }
    lv_deinit();
}

const char* doodad_host_last_error(void) {
    return g_last_error;
}

int doodad_host_start_wasm(const char* path) {
    if (!g_runtime_ready) {
        set_error("WAMR is not initialized");
        return 0;
    }
    release_wasm();

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        set_error("unable to open app.wasm");
        doodad_lvgl_ui_show_error(&g_ui, "MODULE LOAD FAILED");
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        set_error("unable to seek app.wasm");
        return 0;
    }
    const long file_size = ftell(file);
    if (file_size <= 0 || file_size > kMaximumModuleBytes
        || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        set_error("app.wasm size outside 1..262144 bytes");
        doodad_lvgl_ui_show_error(&g_ui, "MODULE LOAD FAILED");
        return 0;
    }

    g_module_bytes = (uint8_t*)malloc((size_t)file_size);
    if (g_module_bytes == NULL
        || fread(g_module_bytes, 1, (size_t)file_size, file)
               != (size_t)file_size) {
        fclose(file);
        release_wasm();
        set_error("unable to read app.wasm");
        doodad_lvgl_ui_show_error(&g_ui, "MODULE LOAD FAILED");
        return 0;
    }
    fclose(file);

    char error_buffer[192];
    memset(error_buffer, 0, sizeof(error_buffer));
    g_module = wasm_runtime_load(
        g_module_bytes, (uint32_t)file_size, error_buffer, sizeof(error_buffer));
    if (g_module == NULL) {
        set_error(error_buffer);
        doodad_lvgl_ui_show_error(&g_ui, "MODULE LOAD FAILED");
        return 0;
    }

    g_instance = wasm_runtime_instantiate(
        g_module,
        kWasmStackBytes,
        kWasmHeapBytes,
        error_buffer,
        sizeof(error_buffer));
    if (g_instance == NULL) {
        set_error(error_buffer);
        doodad_lvgl_ui_show_error(&g_ui, "MODULE INSTANTIATE FAILED");
        return 0;
    }

    wasm_function_inst_t app_start =
        wasm_runtime_lookup_function(g_instance, "app_start");
    if (app_start == NULL) {
        set_error("app_start export not found");
        doodad_lvgl_ui_show_error(&g_ui, "APP_START NOT FOUND");
        return 0;
    }

    g_execution_environment =
        wasm_runtime_create_exec_env(g_instance, kExecEnvStackBytes);
    if (g_execution_environment == NULL) {
        set_error("execution environment allocation failed");
        doodad_lvgl_ui_show_error(&g_ui, "MODULE INSTANTIATE FAILED");
        return 0;
    }
    g_handle_event =
        wasm_runtime_lookup_function(g_instance, "handle_event");

    doodad_lvgl_ui_show_shell(&g_ui, "WASM RUNNING", "DEV");
    g_semantic_mount_called = false;
    g_pending_event_size = 0;
    if (!wasm_runtime_call_wasm(
            g_execution_environment, app_start, 0, NULL)) {
        const char* exception = wasm_runtime_get_exception(g_instance);
        set_error(exception != NULL ? exception : "guest trapped");
        if (strstr(g_last_error, "INVALID GUEST STRING") == NULL) {
            doodad_lvgl_ui_show_error(&g_ui, "GUEST TRAP");
        }
        return 0;
    }
    if (!g_semantic_mount_called) {
        set_error("guest returned without mounting AppSpec");
        doodad_lvgl_ui_show_error(&g_ui, "GUEST TRAP");
        return 0;
    }
    doodad_host_render_now();
    return 1;
}

void doodad_host_render_now(void) {
    if (g_display != NULL) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(g_display);
    }
}

const uint16_t* doodad_host_framebuffer(void) {
    return g_framebuffer;
}

size_t doodad_host_framebuffer_pixels(void) {
    return sizeof(g_framebuffer) / sizeof(g_framebuffer[0]);
}

void doodad_host_show_catalog(int story) {
    if (g_display == NULL) {
        return;
    }
    m3e_catalog_show(lv_screen_active(), story);
    doodad_host_render_now();
}

int doodad_host_show_appspec(const uint8_t* bytes, size_t size) {
    char error[192];
    memset(error, 0, sizeof(error));
    if (!m3e_appspec_render_canonical_cbor(
            lv_screen_active(), bytes, size, error, sizeof(error))) {
        set_error(error);
        return 0;
    }
    doodad_host_render_now();
    return 1;
}

static lv_obj_t* first_clickable(lv_obj_t* parent) {
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, (int32_t)index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)
            && !lv_obj_has_state(child, LV_STATE_DISABLED)) {
            return child;
        }
        lv_obj_t* nested = first_clickable(child);
        if (nested != NULL) return nested;
    }
    return NULL;
}

int doodad_host_click_first_action(void) {
    lv_obj_t* object = first_clickable(lv_screen_active());
    if (object == NULL) {
        set_error("no clickable semantic action");
        return 0;
    }
    const lv_result_t result =
        lv_obj_send_event(object, LV_EVENT_CLICKED, NULL);
    const int dispatched =
        g_pending_event_size == 0
            ? 0
            : dispatch_guest_event(
                  g_pending_event, g_pending_event_size);
    g_pending_event_size = 0;
    doodad_host_render_now();
    return result == LV_RESULT_OK && dispatched;
}

static lv_flex_align_t flex_align(int align) {
    switch (align) {
        case DOODAD_ALIGN_START:
            return LV_FLEX_ALIGN_START;
        case DOODAD_ALIGN_END:
            return LV_FLEX_ALIGN_END;
        case DOODAD_ALIGN_STRETCH:
            return LV_FLEX_ALIGN_SPACE_EVENLY;
        case DOODAD_ALIGN_CENTER:
        default:
            return LV_FLEX_ALIGN_CENTER;
    }
}

static lv_obj_t* add_stack(
    lv_obj_t* parent, int direction, int align, int gap, bool fill_parent) {
    lv_obj_t* stack = lv_obj_create(parent);
    lv_obj_remove_style_all(stack);
    lv_obj_set_style_bg_opa(stack, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stack, 0, 0);
    lv_obj_set_style_pad_all(stack, 0, 0);
    lv_obj_set_style_pad_gap(stack, gap, 0);
    lv_obj_set_scrollbar_mode(stack, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(
        stack,
        direction == DOODAD_DIRECTION_ROW ? LV_FLEX_FLOW_ROW
                                          : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        stack, flex_align(align), flex_align(align), flex_align(align));
    if (fill_parent) {
        lv_obj_set_size(stack, LV_PCT(100), LV_PCT(100));
    } else {
        lv_obj_set_width(stack, LV_PCT(100));
        lv_obj_set_height(stack, LV_SIZE_CONTENT);
    }
    return stack;
}

void* doodad_host_ui_begin_document(int direction, int align, int gap) {
    lv_obj_t* content = doodad_lvgl_ui_content(&g_ui);
    lv_obj_clean(content);
    return add_stack(content, direction, align, gap, true);
}

void* doodad_host_ui_add_stack(
    void* parent, int direction, int align, int gap) {
    return add_stack((lv_obj_t*)parent, direction, align, gap, false);
}

void* doodad_host_ui_add_text(void* parent, const char* text, int style) {
    lv_obj_t* label = lv_label_create((lv_obj_t*)parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F7FB), 0);
    switch (style) {
        case DOODAD_TEXT_STYLE_DISPLAY:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
            break;
        case DOODAD_TEXT_STYLE_TITLE:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
            break;
        case DOODAD_TEXT_STYLE_CAPTION:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x41D9B2), 0);
            break;
        case DOODAD_TEXT_STYLE_MUTED:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x92A4BC), 0);
            break;
        case DOODAD_TEXT_STYLE_BODY:
        default:
            lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
            break;
    }
    return label;
}

void* doodad_host_ui_add_button(
    void* parent, const char* identifier, const char* label, int disabled) {
    (void)identifier;
    lv_obj_t* button = lv_button_create((lv_obj_t*)parent);
    lv_obj_set_size(button, 160, 32);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_radius(button, 8, 0);
    if (disabled) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }
    lv_obj_t* text = lv_label_create(button);
    lv_label_set_text(text, label);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_12, 0);
    lv_obj_center(text);
    return button;
}

void* doodad_host_ui_add_progress(
    void* parent, const char* label, int value, int maximum) {
    lv_obj_t* group =
        add_stack((lv_obj_t*)parent, DOODAD_DIRECTION_COLUMN, DOODAD_ALIGN_CENTER, 4, false);
    if (label != NULL && label[0] != '\0') {
        doodad_host_ui_add_text(group, label, DOODAD_TEXT_STYLE_MUTED);
    }
    lv_obj_t* bar = lv_bar_create(group);
    lv_obj_set_size(bar, 170, 10);
    lv_bar_set_range(bar, 0, maximum);
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x41D9B2), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    return group;
}
