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
#include "m3e/services/exact_scheduler_c.h"
#include "m3e/services/provider_event_c.h"
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
    kMaximumProviderEventBytes = 1024,
    kMaximumTimerDurationMs = 7 * 24 * 60 * 60 * 1000,
};

static uint16_t g_framebuffer[kSurfaceWidth * kSurfaceHeight];
static uint16_t g_draw_buffer[kSurfaceWidth * kDrawRows];
static lv_display_t* g_display;
static doodad_lvgl_ui_t g_ui;

static wasm_module_t g_module;
static wasm_module_inst_t g_instance;
static wasm_exec_env_t g_execution_environment;
static wasm_function_inst_t g_handle_event;
static wasm_function_inst_t g_handle_provider_event;
static uint8_t* g_module_bytes;
static bool g_runtime_ready;
static bool g_lvgl_ready;
static bool g_semantic_mount_called;
static uint8_t g_pending_event[512];
static size_t g_pending_event_size;
static char g_last_error[256];
static m3e_exact_scheduler_handle g_scheduler;
static uint64_t g_scenario_ms;
static uint64_t g_provider_revision;
static uint64_t g_provider_request_id;
static uint64_t g_semantic_event_count;
static uint64_t g_provider_request_count;
static bool g_display_awake;
static bool g_weather_request_pending;
static uint8_t g_weather_cycle;

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

static int dispatch_guest_handler(
    wasm_function_inst_t handler,
    const char* handler_name,
    const uint8_t* bytes,
    size_t length) {
    if (g_instance == NULL || g_execution_environment == NULL
        || handler == NULL || length == 0
        || length > kMaximumProviderEventBytes) {
        set_error("guest handler unavailable");
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
        g_execution_environment, handler, 2, arguments);
    wasm_runtime_module_free(g_instance, guest_pointer);
    if (!called) {
        const char* exception = wasm_runtime_get_exception(g_instance);
        set_error(exception != NULL ? exception : handler_name);
        return 0;
    }
    uint64_t packed_result = 0;
    memcpy(&packed_result, arguments, sizeof(packed_result));
    if (packed_result == 0) {
        // Navigation may synchronously mount another AppSpec and require no
        // follow-up patch batch.
        doodad_host_render_now();
        return 1;
    }
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

static int dispatch_guest_event(const uint8_t* bytes, size_t length) {
    if (length > sizeof(g_pending_event)) {
        set_error("guest UI event exceeds maximum");
        return 0;
    }
    return dispatch_guest_handler(
        g_handle_event, "handle_event trapped", bytes, length);
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
    ++g_semantic_event_count;
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

static bool copy_guest_service_id(
    wasm_exec_env_t environment,
    uint32_t pointer,
    uint32_t length,
    char output[49]) {
    if (length == 0 || length > 48) return false;
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(environment);
    if (!wasm_runtime_validate_app_addr(instance, pointer, length)) {
        return false;
    }
    const uint8_t* bytes =
        (const uint8_t*)wasm_runtime_addr_app_to_native(instance, pointer);
    if (bytes == NULL) return false;
    for (uint32_t index = 0; index < length; ++index) {
        const uint8_t byte = bytes[index];
        if (!((byte >= 'a' && byte <= 'z') ||
              (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '-' || byte == '_')) {
            return false;
        }
    }
    memcpy(output, bytes, length);
    output[length] = '\0';
    return true;
}

static uint64_t host_timer_schedule_after(
    wasm_exec_env_t environment,
    uint32_t pointer,
    uint32_t length,
    uint32_t duration_ms) {
    char id[49] = {0};
    if (g_scheduler == NULL ||
        duration_ms == 0 ||
        duration_ms > kMaximumTimerDurationMs ||
        !copy_guest_service_id(environment, pointer, length, id)) {
        return 0;
    }
    return m3e_exact_scheduler_schedule_after(
        g_scheduler, id, duration_ms, g_scenario_ms);
}

static int32_t host_timer_cancel(
    wasm_exec_env_t environment,
    uint32_t pointer,
    uint32_t length) {
    char id[49] = {0};
    if (g_scheduler == NULL ||
        !copy_guest_service_id(environment, pointer, length, id)) {
        return 0;
    }
    return m3e_exact_scheduler_cancel(g_scheduler, id);
}

static int32_t host_timer_acknowledge(
    wasm_exec_env_t environment,
    uint32_t pointer,
    uint32_t length) {
    char id[49] = {0};
    if (g_scheduler == NULL ||
        !copy_guest_service_id(environment, pointer, length, id)) {
        return 0;
    }
    return m3e_exact_scheduler_acknowledge(g_scheduler, id);
}

static uint64_t host_provider_request(
    wasm_exec_env_t environment,
    uint32_t provider_pointer,
    uint32_t provider_length,
    uint32_t operation_pointer,
    uint32_t operation_length,
    uint32_t payload_pointer,
    uint32_t payload_length) {
    char provider[49] = {0};
    char operation[49] = {0};
    wasm_module_inst_t instance =
        wasm_runtime_get_module_inst(environment);
    if (g_provider_request_id == UINT64_MAX ||
        !copy_guest_service_id(
            environment,
            provider_pointer,
            provider_length,
            provider) ||
        !copy_guest_service_id(
            environment,
            operation_pointer,
            operation_length,
            operation) ||
        payload_length > 512 ||
        (payload_length != 0 &&
         !wasm_runtime_validate_app_addr(
             instance, payload_pointer, payload_length))) {
        return 0;
    }
    const bool weather =
        strcmp(provider, "weather") == 0 &&
        strcmp(operation, "refresh") == 0;
    const bool fixture = strcmp(provider, "fixture") == 0;
    if ((!weather && !fixture) ||
        (weather && g_weather_request_pending)) {
        return 0;
    }
    if (weather) g_weather_request_pending = true;
    ++g_provider_request_count;
    return ++g_provider_request_id;
}

#define DEFINE_BOUND_PROVIDER_REQUEST(                                \
    function_name, provider_name, prefix_one, prefix_two)             \
    static uint64_t function_name(                                    \
        wasm_exec_env_t environment,                                  \
        uint32_t operation_pointer,                                   \
        uint32_t operation_length,                                    \
        uint32_t payload_pointer,                                     \
        uint32_t payload_length) {                                    \
        char operation[49] = {0};                                     \
        wasm_module_inst_t instance =                                 \
            wasm_runtime_get_module_inst(environment);                \
        if (g_provider_request_id == UINT64_MAX ||                    \
            !copy_guest_service_id(                                   \
                environment, operation_pointer,                       \
                operation_length, operation) ||                       \
            payload_length > 512 ||                                   \
            (payload_length != 0 &&                                   \
             !wasm_runtime_validate_app_addr(                         \
                 instance, payload_pointer, payload_length))) {       \
            return 0;                                                 \
        }                                                             \
        const bool prefix_one_ok =                                    \
            strncmp(operation, prefix_one, strlen(prefix_one)) == 0;  \
        const bool prefix_two_ok =                                    \
            prefix_two[0] != '\0' &&                                  \
            strncmp(operation, prefix_two, strlen(prefix_two)) == 0;  \
        if (!prefix_one_ok && !prefix_two_ok) return 0;                \
        (void)provider_name;                                          \
        ++g_provider_request_count;                                   \
        return ++g_provider_request_id;                               \
    }

DEFINE_BOUND_PROVIDER_REQUEST(
    host_calendar_request, "calendar", "calendar.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_audio_request, "audio", "voice-notes.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_medication_request, "medication", "medication.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sensor_request, "sensor", "sensor.", "sensor-recorder.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sleep_request, "sleep", "sleep.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_media_request, "media", "media.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_navigation_request, "navigation", "navigation.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_transit_request, "transit", "transit.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_home_request, "home", "home.", "smart-home.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_sports_request, "sports", "sports.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_wallet_request, "wallet", "wallet.", "")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_remote_request, "remote", "remote.", "remote-control.")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_workout_request, "workout", "workout.", "complete_set")
DEFINE_BOUND_PROVIDER_REQUEST(
    host_game_request, "game", "snake.", "")

#undef DEFINE_BOUND_PROVIDER_REQUEST

static NativeSymbol g_native_symbols[] = {
    {
        .symbol = "ui_mount",
        .func_ptr = (void*)host_ui_mount,
        .signature = "(ii)i",
        .attachment = NULL,
    },
    {
        .symbol = "timer_schedule_after",
        .func_ptr = (void*)host_timer_schedule_after,
        .signature = "(iii)I",
        .attachment = NULL,
    },
    {
        .symbol = "timer_cancel",
        .func_ptr = (void*)host_timer_cancel,
        .signature = "(ii)i",
        .attachment = NULL,
    },
    {
        .symbol = "timer_acknowledge",
        .func_ptr = (void*)host_timer_acknowledge,
        .signature = "(ii)i",
        .attachment = NULL,
    },
    {
        .symbol = "provider_request",
        .func_ptr = (void*)host_provider_request,
        .signature = "(iiiiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "calendar_request",
        .func_ptr = (void*)host_calendar_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "audio_request",
        .func_ptr = (void*)host_audio_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "medication_request",
        .func_ptr = (void*)host_medication_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "sensor_request",
        .func_ptr = (void*)host_sensor_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "sleep_request",
        .func_ptr = (void*)host_sleep_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "media_request",
        .func_ptr = (void*)host_media_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "navigation_request",
        .func_ptr = (void*)host_navigation_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "transit_request",
        .func_ptr = (void*)host_transit_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "home_request",
        .func_ptr = (void*)host_home_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "sports_request",
        .func_ptr = (void*)host_sports_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "wallet_request",
        .func_ptr = (void*)host_wallet_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "remote_request",
        .func_ptr = (void*)host_remote_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "workout_request",
        .func_ptr = (void*)host_workout_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
    {
        .symbol = "game_request",
        .func_ptr = (void*)host_game_request,
        .signature = "(iiii)I",
        .attachment = NULL,
    },
};

static void release_wasm(void) {
    g_handle_event = NULL;
    g_handle_provider_event = NULL;
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

static int initialize_wamr(void) {
    RuntimeInitArgs arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.mem_alloc_type = Alloc_With_Allocator;
    arguments.mem_alloc_option.allocator.malloc_func = (void*)os_malloc;
    arguments.mem_alloc_option.allocator.realloc_func = (void*)os_realloc;
    arguments.mem_alloc_option.allocator.free_func = (void*)os_free;
    if (!wasm_runtime_full_init(&arguments)) {
        set_error("WAMR initialization failed");
        return 0;
    }
    if (!wasm_runtime_register_natives(
            "doodad",
            g_native_symbols,
            sizeof(g_native_symbols) / sizeof(g_native_symbols[0]))) {
        set_error("WAMR native ABI registration failed");
        wasm_runtime_destroy();
        return 0;
    }
    g_runtime_ready = true;
    return 1;
}

static void reset_native_shell(void) {
    if (g_display == NULL) return;
    lv_obj_t* screen = lv_screen_active();
    lv_obj_clean(screen);
    doodad_lvgl_ui_init(&g_ui, screen);
    doodad_lvgl_ui_show_shell(&g_ui, "STARTING", "DEV");
}

int doodad_host_create(void) {
    memset(g_last_error, 0, sizeof(g_last_error));
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    g_scenario_ms = 0;
    g_provider_revision = 0;
    g_provider_request_id = 0;
    g_semantic_event_count = 0;
    g_provider_request_count = 0;
    g_display_awake = true;
    g_weather_request_pending = false;
    g_weather_cycle = 0;
    g_scheduler = m3e_exact_scheduler_create();
    if (g_scheduler == NULL) {
        set_error("exact scheduler allocation failed");
        return 0;
    }

    if (!g_lvgl_ready) {
        lv_init();
        lv_tick_set_cb(tick_milliseconds);
        g_lvgl_ready = true;
    }
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

    if (!initialize_wamr()) {
        doodad_lvgl_ui_show_error(&g_ui, "WAMR INIT FAILED");
        return 0;
    }
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
    // LVGL's global registries are not reliably reentrant across
    // deinit/reinit in every pinned desktop build. Keep the process-global
    // core initialized while deleting each test display and guest runtime.
    m3e_exact_scheduler_destroy(g_scheduler);
    g_scheduler = NULL;
}

const char* doodad_host_last_error(void) {
    return g_last_error;
}

int doodad_host_start_wasm(const char* path) {
    if (!g_runtime_ready) {
        set_error("WAMR is not initialized");
        return 0;
    }
    const bool replacing_guest = g_module != NULL;
    release_wasm();
    if (replacing_guest) {
        wasm_runtime_destroy();
        g_runtime_ready = false;
        if (!initialize_wamr()) {
            doodad_lvgl_ui_show_error(&g_ui, "WAMR INIT FAILED");
            return 0;
        }
    }
    // AppSpec owns and clears the full screen, so every prior g_ui child
    // pointer becomes invalid after the first guest mount. Rebuild the
    // trusted loading shell before touching those pointers on replacement.
    reset_native_shell();

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
    g_handle_provider_event =
        wasm_runtime_lookup_function(g_instance, "handle_provider_event");

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
    if (g_display != NULL && g_display_awake) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(g_display);
    }
}

void doodad_host_set_display_awake(int awake) {
    g_display_awake = awake != 0;
    if (g_display_awake) doodad_host_render_now();
}

int doodad_host_display_awake(void) {
    return g_display_awake ? 1 : 0;
}

int doodad_host_advance_time(uint64_t milliseconds) {
    if (milliseconds > UINT64_MAX - g_scenario_ms) {
        set_error("scenario clock overflow");
        return 0;
    }
    g_scenario_ms += milliseconds;
    m3e_due_delivery due[8] = {0};
    (void)m3e_exact_scheduler_poll(
        g_scheduler, g_scenario_ms, due, 8);

    m3e_schedule_record records[8] = {0};
    const size_t count = m3e_exact_scheduler_records(
        g_scheduler, records, 8, g_scenario_ms);
    for (size_t index = 0; index < count; ++index) {
        if (g_handle_provider_event == NULL) {
            set_error("guest has no handle_provider_event export");
            return 0;
        }
        uint8_t envelope[256];
        if (g_provider_revision == UINT64_MAX) {
            set_error("provider event encoding failed");
            return 0;
        }
        const size_t envelope_length =
            m3e_encode_timer_provider_event(
            &records[index],
            ++g_provider_revision,
            g_scenario_ms,
            envelope,
            sizeof(envelope));
        if (envelope_length == 0 ||
            !dispatch_guest_handler(
                g_handle_provider_event,
                "handle_provider_event trapped",
                envelope,
                envelope_length)) {
            return 0;
        }
    }
    return 1;
}

uint64_t doodad_host_scenario_time(void) {
    return g_scenario_ms;
}

int doodad_host_deliver_provider(void) {
    if (!g_weather_request_pending) {
        set_error("no provider request is pending");
        return 0;
    }
    if (g_handle_provider_event == NULL ||
        g_provider_revision == UINT64_MAX) {
        set_error("guest provider handler unavailable");
        return 0;
    }
    g_weather_request_pending = false;
    g_weather_cycle = (uint8_t)((g_weather_cycle + 1) % 3);

    int32_t temperature = 720;
    const char* condition = "Clear";
    const char* detail = "High 76 - Low 59 - Rain 10%";
    uint64_t data_revision = 1;
    uint64_t age_minutes = 12;
    uint8_t freshness = 1;
    if (g_weather_cycle == 2) {
        condition = "Offline";
        detail = "Forecast unavailable - cached data";
        age_minutes = 18;
        freshness = 2;
    } else if (g_weather_cycle == 0) {
        temperature = 710;
        condition = "Clear";
        detail = "High 75 - Low 58 - Rain 5%";
        data_revision = 2;
        age_minutes = 0;
        freshness = 0;
    }

    uint8_t envelope[512];
    const size_t envelope_length =
        m3e_encode_weather_provider_event(
            temperature,
            condition,
            detail,
            "San Francisco",
            data_revision,
            age_minutes,
            ++g_provider_revision,
            freshness,
            g_scenario_ms,
            envelope,
            sizeof(envelope));
    if (envelope_length == 0 ||
        !dispatch_guest_handler(
            g_handle_provider_event,
            "handle_provider_event trapped",
            envelope,
            envelope_length)) {
        return 0;
    }
    return 1;
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

static int send_first_semantic_click(lv_obj_t* parent) {
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, (int32_t)index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE)
            && !lv_obj_has_state(child, LV_STATE_DISABLED)) {
            g_pending_event_size = 0;
            const lv_result_t result =
                lv_obj_send_event(child, LV_EVENT_CLICKED, NULL);
            if (g_pending_event_size != 0) {
                return result == LV_RESULT_OK ? 1 : -1;
            }
        }
        const int nested = send_first_semantic_click(child);
        if (nested != 0) return nested;
    }
    return 0;
}

static int finish_semantic_click(int event_result) {
    if (event_result == 0) {
        set_error("no clickable semantic action");
        return 0;
    }
    const int dispatched =
        g_pending_event_size == 0
            ? 0
            : dispatch_guest_event(
                  g_pending_event, g_pending_event_size);
    g_pending_event_size = 0;
    doodad_host_render_now();
    if (event_result < 0) {
        set_error("LVGL action callback rejected the event");
        return 0;
    }
    if (!dispatched) {
        if (g_last_error[0] == '\0') {
            set_error("clickable object had no semantic action");
        }
        return 0;
    }
    return 1;
}

int doodad_host_click_first_action(void) {
    return finish_semantic_click(
        send_first_semantic_click(lv_screen_active()));
}

static lv_obj_t* find_button_with_label(
    lv_obj_t* parent, const char* label) {
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(parent, (int32_t)index);
        if (lv_obj_check_type(child, &lv_button_class)
            && !lv_obj_has_state(child, LV_STATE_DISABLED)) {
            const uint32_t child_count = lv_obj_get_child_count(child);
            for (uint32_t nested = 0; nested < child_count; ++nested) {
                lv_obj_t* candidate =
                    lv_obj_get_child(child, (int32_t)nested);
                if (lv_obj_check_type(candidate, &lv_label_class)
                    && strcmp(lv_label_get_text(candidate), label) == 0) {
                    return child;
                }
            }
        }
        lv_obj_t* nested = find_button_with_label(child, label);
        if (nested != NULL) return nested;
    }
    return NULL;
}

int doodad_host_click_button(const char* label) {
    if (label == NULL || label[0] == '\0') {
        set_error("button label is empty");
        return 0;
    }
    lv_obj_t* button =
        find_button_with_label(lv_screen_active(), label);
    if (button == NULL) {
        set_error("button label not found");
        return 0;
    }
    g_pending_event_size = 0;
    lv_result_t result =
        lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
    if (result == LV_RESULT_OK && g_pending_event_size == 0) {
        result = lv_obj_send_event(button, LV_EVENT_RELEASED, NULL);
    }
    return finish_semantic_click(
        result == LV_RESULT_OK && g_pending_event_size != 0 ? 1 : -1);
}

static lv_obj_t* find_node(lv_obj_t* parent, const char* node_id) {
    // AppSpec text nodes are labels whose user data is the stable node id.
    // Other framework widgets may store non-string context pointers in user
    // data, so never interpret arbitrary objects' user data as text.
    if (lv_obj_check_type(parent, &lv_label_class)) {
        const char* id = (const char*)lv_obj_get_user_data(parent);
        if (id != NULL && strcmp(id, node_id) == 0) {
            return parent;
        }
    }
    const uint32_t count = lv_obj_get_child_count(parent);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* result =
            find_node(lv_obj_get_child(parent, (int32_t)index), node_id);
        if (result != NULL) return result;
    }
    return NULL;
}

const char* doodad_host_node_text(const char* node_id) {
    if (node_id == NULL || node_id[0] == '\0') return NULL;
    const char* mounted = m3e_appspec_mounted_text(node_id, 0);
    if (mounted != NULL) return mounted;
    lv_obj_t* object = find_node(lv_screen_active(), node_id);
    if (object == NULL) return NULL;
    if (lv_obj_check_type(object, &lv_label_class)) {
        return lv_label_get_text(object);
    }
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < count; ++index) {
        lv_obj_t* child = lv_obj_get_child(object, (int32_t)index);
        if (lv_obj_check_type(child, &lv_label_class)) {
            return lv_label_get_text(child);
        }
    }
    return NULL;
}

size_t doodad_host_semantic_snapshot(
    char* output, size_t output_size) {
    return m3e_appspec_semantic_snapshot(output, output_size);
}

size_t doodad_host_mounted_node_count(void) {
    return m3e_appspec_mounted_node_count();
}

size_t doodad_host_mounted_event_count(void) {
    return m3e_appspec_mounted_event_count();
}

static void collect_lvgl_metrics(
    lv_obj_t* object,
    size_t depth,
    size_t* count,
    size_t* maximum_depth) {
    if (object == NULL) return;
    ++*count;
    if (depth > *maximum_depth) *maximum_depth = depth;
    const uint32_t children = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < children; ++index) {
        collect_lvgl_metrics(
            lv_obj_get_child(object, (int32_t)index),
            depth + 1,
            count,
            maximum_depth);
    }
}

size_t doodad_host_lvgl_object_count(void) {
    size_t count = 0;
    size_t depth = 0;
    collect_lvgl_metrics(
        lv_screen_active(), 0, &count, &depth);
    return count;
}

size_t doodad_host_lvgl_max_depth(void) {
    size_t count = 0;
    size_t depth = 0;
    collect_lvgl_metrics(
        lv_screen_active(), 0, &count, &depth);
    return depth;
}

uint64_t doodad_host_semantic_event_count(void) {
    return g_semantic_event_count;
}

uint64_t doodad_host_provider_request_count(void) {
    return g_provider_request_count;
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
