#include "display.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "M5Unified.h"
#include "app_runner.hpp"
#include "doodad_lvgl_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/renderer.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/catalog/catalog.h"
#include "m3e/os/shell_state.hpp"
#include "m3e/os/surface_registry.hpp"
#include "m3e/theme/resolved_theme.hpp"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::int32_t kDrawRows = 40;
constexpr std::uint16_t kPhysicalBackground = 0x0841;
constexpr std::uint32_t kDisplaySpiFrequencyHz = 80 * 1000 * 1000;
constexpr std::int64_t kTelemetryIntervalMicroseconds = 2 * 1000 * 1000;
constexpr std::size_t kUiQueueDepth = 16;

enum class UiCommandType : std::uint8_t {
    shell,
    appspec,
    command_batch,
    error,
    catalog,
    system_home,
    surface_publish,
};

struct UiCommand {
    UiCommandType type;
    char primary[129];
    char secondary[32];
    std::size_t length;
    int story;
    m3e::appspec::WireDocument* document;
    m3e::appspec::CommandBatch* batch;
    m3e::os::DomainSurfaceSnapshot* surfaces;
};

bool g_display_ready = false;
TaskHandle_t g_ui_task = nullptr;
QueueHandle_t g_ui_queue = nullptr;
lv_display_t* g_lvgl_display = nullptr;
lv_indev_t* g_touch_input = nullptr;
doodad_lvgl_ui_t g_ui{};
m3e::StyleRegistry g_appspec_styles{};
m3e::appspec::WireDocument* g_active_document = nullptr;
std::uint16_t g_draw_buffer_a[DOODAD_SURFACE_WIDTH * kDrawRows]
    __attribute__((aligned(4)));
std::uint16_t g_draw_buffer_b[DOODAD_SURFACE_WIDTH * kDrawRows]
    __attribute__((aligned(4)));
lv_display_t* g_pending_flush = nullptr;
std::int64_t g_pending_flush_started_us = 0;
std::uint32_t g_window_frames = 0;
std::uint32_t g_window_renders = 0;
std::uint32_t g_window_flushes = 0;
std::uint64_t g_window_pixels = 0;
std::uint64_t g_window_flush_us = 0;
std::uint32_t g_window_max_flush_us = 0;
std::uint64_t g_window_render_us = 0;
std::uint32_t g_window_max_render_us = 0;
std::int64_t g_render_started_us = 0;
std::int64_t g_window_started_us = 0;
std::uint32_t g_window_touch_presses = 0;
bool g_touch_pressed = false;
lv_point_t g_last_touch_point{0, 0};
m3e::os::ShellState g_shell{};
m3e::os::SurfaceRegistry g_surface_registry{};
bool g_shell_active = false;

std::uint32_t tick_milliseconds() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

void complete_pending_flush() {
    if (g_pending_flush == nullptr || M5.Display.dmaBusy()) {
        return;
    }
    const auto duration = static_cast<std::uint32_t>(
        esp_timer_get_time() - g_pending_flush_started_us);
    ++g_window_flushes;
    g_window_flush_us += duration;
    g_window_max_flush_us =
        std::max(g_window_max_flush_us, duration);
    auto* completed = g_pending_flush;
    g_pending_flush = nullptr;
    lv_display_flush_ready(completed);
}

void flush_display(
    lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map) {
    if (g_pending_flush != nullptr) {
        ESP_LOGE(kTag, "[display] overlapping DMA flush");
        M5.Display.waitDMA();
        complete_pending_flush();
    }
    const auto width = area->x2 - area->x1 + 1;
    const auto height = area->y2 - area->y1 + 1;
    const auto x_offset =
        (M5.Display.width() - DOODAD_SURFACE_WIDTH) / 2;

    g_pending_flush = display;
    g_pending_flush_started_us = esp_timer_get_time();
    g_window_pixels +=
        static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height);
    M5.Display.pushImageDMA(
        x_offset + area->x1,
        area->y1,
        width,
        height,
        reinterpret_cast<const std::uint16_t*>(pixel_map));
    complete_pending_flush();
}

void display_event(lv_event_t* event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            g_render_started_us = esp_timer_get_time();
            break;
        case LV_EVENT_RENDER_READY: {
            const auto duration = static_cast<std::uint32_t>(
                esp_timer_get_time() - g_render_started_us);
            ++g_window_renders;
            g_window_render_us += duration;
            g_window_max_render_us =
                std::max(g_window_max_render_us, duration);
            break;
        }
        case LV_EVENT_REFR_READY:
            ++g_window_frames;
            break;
        default:
            break;
    }
}

void read_touch(lv_indev_t*, lv_indev_data_t* data) {
    bool pressed = false;
    if (M5.Touch.getCount() > 0) {
        const auto detail = M5.Touch.getDetail(0);
        const auto x_offset =
            (M5.Display.width() - DOODAD_SURFACE_WIDTH) / 2;
        const auto logical_x = detail.x - x_offset;
        const auto logical_y = detail.y;
        if (logical_x >= 0 && logical_x < DOODAD_SURFACE_WIDTH &&
            logical_y >= 0 && logical_y < DOODAD_SURFACE_HEIGHT) {
            g_last_touch_point.x = logical_x;
            g_last_touch_point.y = logical_y;
            pressed = true;
        }
    }
    if (pressed && !g_touch_pressed) {
        ++g_window_touch_presses;
    }
    g_touch_pressed = pressed;
    data->point = g_last_touch_point;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void render_now() {
    if (g_display_ready) {
        lv_refr_now(g_lvgl_display);
    }
}

bool on_ui_task() {
    return xTaskGetCurrentTaskHandle() == g_ui_task;
}

bool enqueue(const UiCommand& command) {
    if (g_ui_queue == nullptr ||
        xQueueSend(g_ui_queue, &command, 0) != pdTRUE) {
        ESP_LOGE(kTag, "[display] UI command queue overflow");
        return false;
    }
    return true;
}

void shell_now(const char* status, const char* source) {
    doodad_lvgl_ui_show_shell(&g_ui, status, source);
    render_now();
}

void error_now(const char* stage);

void forward_app_event(
    const m3e::appspec::UiEvent& event,
    void*) {
    if (!app_post_ui_event(event)) {
        ESP_LOGE(kTag, "[display] semantic event rejected");
    }
}

bool appspec_now(m3e::appspec::WireDocument* document) {
    if (document == nullptr) return false;
    if (!g_appspec_styles.initialized() &&
        !g_appspec_styles.initialize(m3e::baseline_dark_theme())) {
        delete document;
        error_now("THEME INIT FAILED");
        return false;
    }
    m3e::appspec::Renderer renderer(g_appspec_styles);
    if (!renderer.mount(
            lv_screen_active(),
            *document,
            forward_app_event,
            nullptr)) {
        delete document;
        error_now("APPSPEC RENDER FAILED");
        return false;
    }
    delete g_active_document;
    g_active_document = document;
    render_now();
    return true;
}

bool command_batch_now(m3e::appspec::CommandBatch* batch) {
    if (batch == nullptr) return false;
    if (g_active_document == nullptr) {
        delete batch;
        ESP_LOGE(kTag, "[display] no mounted AppSpec for CommandBatch");
        return false;
    }
    const auto applied = m3e::appspec::apply_ui_command_batch(
        *batch, *g_active_document);
    delete batch;
    if (!applied.ok()) {
        ESP_LOGE(
            kTag,
            "[display] CommandBatch rejected: %s command=%u",
            m3e::appspec::command_error_name(applied.error),
            static_cast<unsigned>(applied.command_index));
        return false;
    }
    render_now();
    return true;
}

void error_now(const char* stage) {
    doodad_lvgl_ui_show_error(&g_ui, stage);
    render_now();
}

void catalog_now(int story) {
    m3e_catalog_show(lv_screen_active(), story);
    render_now();
}

int shell_story() {
    const auto& snapshot = g_shell.snapshot();
    switch (snapshot.overlay) {
        case m3e::os::Overlay::voice:
            switch (snapshot.voice_phase) {
                case m3e::os::VoicePhase::listening:
                    return M3E_CATALOG_STORY_OS_VOICE;
                case m3e::os::VoicePhase::transcribing:
                case m3e::os::VoicePhase::clarifying:
                    return M3E_CATALOG_STORY_OS_VOICE_THINKING;
                case m3e::os::VoicePhase::reviewing:
                    return M3E_CATALOG_STORY_OS_VOICE_REVIEW;
                case m3e::os::VoicePhase::building:
                    return M3E_CATALOG_STORY_OS_VOICE_BUILD;
                case m3e::os::VoicePhase::completed:
                    return M3E_CATALOG_STORY_OS_VOICE_RESULT;
                case m3e::os::VoicePhase::error:
                    return M3E_CATALOG_STORY_OS_ERROR;
                case m3e::os::VoicePhase::idle:
                    return M3E_CATALOG_STORY_OS_VOICE;
            }
            break;
        case m3e::os::Overlay::notification:
            return M3E_CATALOG_STORY_OS_NOTIFICATION;
        case m3e::os::Overlay::permission_review:
            return M3E_CATALOG_STORY_OS_PERMISSION_REVIEW;
        case m3e::os::Overlay::action_review:
            return M3E_CATALOG_STORY_OS_ACTION_REVIEW;
        case m3e::os::Overlay::error:
            return M3E_CATALOG_STORY_OS_ERROR;
        case m3e::os::Overlay::none:
            break;
    }
    switch (snapshot.surface) {
        case m3e::os::Surface::watch_face:
            return M3E_CATALOG_STORY_OS_HOME;
        case m3e::os::Surface::live_cards:
            return M3E_CATALOG_STORY_OS_LIVE_CARDS;
        case m3e::os::Surface::launcher:
            return M3E_CATALOG_STORY_OS_LAUNCHER;
        case m3e::os::Surface::control_center:
            return M3E_CATALOG_STORY_OS_CONTROL_CENTER;
        case m3e::os::Surface::app_manager:
            return M3E_CATALOG_STORY_OS_APP_MANAGER;
        case m3e::os::Surface::app_detail:
            return M3E_CATALOG_STORY_OS_APP_DETAIL;
        case m3e::os::Surface::install_progress:
            return M3E_CATALOG_STORY_OS_INSTALL_PROGRESS;
        case m3e::os::Surface::crash_recovery:
            return M3E_CATALOG_STORY_OS_CRASH_RECOVERY;
        case m3e::os::Surface::app:
            return M3E_CATALOG_STORY_OS_HOME;
    }
    return M3E_CATALOG_STORY_OS_HOME;
}

void render_shell_now() {
    catalog_now(shell_story());
}

void system_home_now() {
    if (!g_shell.initialize()) {
        error_now("SYSTEM SHELL FAILED");
        return;
    }
    g_surface_registry.sync_shell_counts(g_shell);
    g_shell_active = true;
    render_shell_now();
}

bool surface_publish_now(
    m3e::os::DomainSurfaceSnapshot* snapshot) {
    if (snapshot == nullptr) return false;
    const auto published = g_surface_registry.publish(*snapshot);
    delete snapshot;
    if (!published) {
        ESP_LOGW(kTag, "[system] rejected surface publication");
        return false;
    }
    g_surface_registry.sync_shell_counts(g_shell);
    if (g_shell_active && g_shell.snapshot().display_awake) {
        render_shell_now();
    }
    return true;
}

void dispatch_system_input(m3e::os::Input input) {
    if (!g_shell_active) return;
    const auto intent = m3e::os::map_input(input);
    if (intent == m3e::os::Intent::none ||
        !g_shell.dispatch(intent)) {
        return;
    }
    const auto& snapshot = g_shell.snapshot();
    M5.Display.setBrightness(snapshot.display_awake ? 96 : 0);
    if (snapshot.display_awake) {
        render_shell_now();
    }
    ESP_LOGI(
        kTag,
        "[system] input=%u intent=%u surface=%u overlay=%u generation=%u",
        static_cast<unsigned>(input),
        static_cast<unsigned>(intent),
        static_cast<unsigned>(snapshot.surface),
        static_cast<unsigned>(snapshot.overlay),
        static_cast<unsigned>(snapshot.generation));
}

void handle_system_inputs() {
    if (M5.BtnB.wasHold()) {
        dispatch_system_input(m3e::os::Input::button_b_hold);
    } else if (M5.BtnB.wasClicked()) {
        dispatch_system_input(m3e::os::Input::button_b);
    }
    if (M5.BtnA.wasClicked()) {
        dispatch_system_input(m3e::os::Input::button_a);
    }
    if (M5.BtnC.wasClicked()) {
        dispatch_system_input(m3e::os::Input::button_c);
    }
    if (M5.BtnPWR.wasClicked()) {
        dispatch_system_input(m3e::os::Input::power_button);
    }
}

void drain_ui_commands() {
    UiCommand command{};
    while (xQueueReceive(g_ui_queue, &command, 0) == pdTRUE) {
        switch (command.type) {
            case UiCommandType::shell:
                shell_now(command.primary, command.secondary);
                break;
            case UiCommandType::appspec:
                appspec_now(command.document);
                break;
            case UiCommandType::command_batch:
                command_batch_now(command.batch);
                break;
            case UiCommandType::error:
                error_now(command.primary);
                break;
            case UiCommandType::catalog:
                catalog_now(command.story);
                break;
            case UiCommandType::system_home:
                system_home_now();
                break;
            case UiCommandType::surface_publish:
                surface_publish_now(command.surfaces);
                break;
        }
    }
}

}  // namespace

bool display_init() {
    g_ui_task = xTaskGetCurrentTaskHandle();
    g_ui_queue = xQueueCreate(kUiQueueDepth, sizeof(UiCommand));
    if (g_ui_queue == nullptr) {
        ESP_LOGE(kTag, "[display] UI command queue allocation failed");
        return false;
    }
    auto config = M5.config();
    config.clear_display = true;
    config.internal_imu = false;
    config.internal_rtc = false;
    config.internal_mic = false;
    config.internal_spk = false;
    config.external_display_value = 0;
    config.fallback_board = m5::board_t::board_M5StackCoreS3SE;

    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(96);
    auto* display_bus = M5.Display.getPanel()->getBus();
    if (display_bus == nullptr) {
        ESP_LOGE(kTag, "[display] panel bus unavailable");
        return false;
    }
    display_bus->setClock(kDisplaySpiFrequencyHz);
    ESP_LOGI(
        kTag,
        "[display] SPI write clock requested=%u actual=%u",
        static_cast<unsigned>(kDisplaySpiFrequencyHz),
        static_cast<unsigned>(display_bus->getClock()));
    M5.Display.initDMA();

    const auto board = M5.getBoard();
    const bool supported_board =
        board == m5::board_t::board_M5StackCoreS3
        || board == m5::board_t::board_M5StackCoreS3SE;
    const bool supported_size =
        M5.Display.width() >= DOODAD_SURFACE_WIDTH
        && M5.Display.height() >= DOODAD_SURFACE_HEIGHT;
    if (!supported_board || !supported_size) {
        ESP_LOGE(
            kTag,
            "[host] display init failed (board=%d, size=%dx%d)",
            static_cast<int>(board),
            M5.Display.width(),
            M5.Display.height());
        return false;
    }

    // The portable app surface is always 240x240. CoreS3's extra horizontal
    // pixels are host-owned gutters, never additional app layout space.
    M5.Display.fillScreen(kPhysicalBackground);

    lv_init();
    lv_tick_set_cb(tick_milliseconds);
    g_lvgl_display =
        lv_display_create(DOODAD_SURFACE_WIDTH, DOODAD_SURFACE_HEIGHT);
    if (g_lvgl_display == nullptr) {
        ESP_LOGE(kTag, "[host] LVGL display creation failed");
        return false;
    }
    lv_display_set_color_format(g_lvgl_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        g_lvgl_display,
        g_draw_buffer_a,
        g_draw_buffer_b,
        sizeof(g_draw_buffer_a),
        LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_lvgl_display, flush_display);
    lv_display_add_event_cb(
        g_lvgl_display, display_event, LV_EVENT_RENDER_START, nullptr);
    lv_display_add_event_cb(
        g_lvgl_display, display_event, LV_EVENT_RENDER_READY, nullptr);
    lv_display_add_event_cb(
        g_lvgl_display, display_event, LV_EVENT_REFR_READY, nullptr);
    g_touch_input = lv_indev_create();
    if (g_touch_input == nullptr) {
        ESP_LOGE(kTag, "[host] LVGL touch input creation failed");
        return false;
    }
    lv_indev_set_type(g_touch_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_input, read_touch);
    lv_indev_set_display(g_touch_input, g_lvgl_display);

    doodad_lvgl_ui_init(&g_ui, lv_screen_active());
    doodad_lvgl_ui_show_shell(&g_ui, "STARTING", "NATIVE");
    g_display_ready = true;
    g_window_started_us = esp_timer_get_time();
    render_now();
    return true;
}

void display_shell(const char* status, const char* source) {
    if (!g_display_ready) {
        return;
    }
    if (on_ui_task()) {
        shell_now(status, source);
        return;
    }
    UiCommand command{
        UiCommandType::shell,
        {},
        {},
        0,
        0,
        nullptr,
        nullptr,
        nullptr,
    };
    std::strncpy(command.primary, status, sizeof(command.primary) - 1);
    std::strncpy(command.secondary, source, sizeof(command.secondary) - 1);
    enqueue(command);
}

bool display_mount_appspec(
    m3e::appspec::WireDocument* owned_document) {
    if (!g_display_ready || owned_document == nullptr) {
        return false;
    }
    if (on_ui_task()) {
        return appspec_now(owned_document);
    }
    UiCommand command{
        UiCommandType::appspec,
        {},
        {},
        0,
        0,
        owned_document,
        nullptr,
        nullptr,
    };
    return enqueue(command);
}

bool display_apply_command_batch(
    m3e::appspec::CommandBatch* owned_batch) {
    if (!g_display_ready || owned_batch == nullptr) {
        return false;
    }
    if (on_ui_task()) {
        return command_batch_now(owned_batch);
    }
    UiCommand command{
        UiCommandType::command_batch,
        {},
        {},
        0,
        0,
        nullptr,
        owned_batch,
        nullptr,
    };
    return enqueue(command);
}

void display_error(const char* stage) {
    if (!g_display_ready) {
        return;
    }
    if (on_ui_task()) {
        error_now(stage);
        return;
    }
    UiCommand command{
        UiCommandType::error,
        {},
        {},
        0,
        0,
        nullptr,
        nullptr,
        nullptr,
    };
    std::strncpy(command.primary, stage, sizeof(command.primary) - 1);
    enqueue(command);
}

void display_show_catalog(int story) {
    if (!g_display_ready) {
        return;
    }
    if (on_ui_task()) {
        catalog_now(story);
        return;
    }
    UiCommand command{
        UiCommandType::catalog,
        {},
        {},
        0,
        story,
        nullptr,
        nullptr,
        nullptr,
    };
    enqueue(command);
}

void display_show_system_home() {
    if (!g_display_ready) {
        return;
    }
    if (on_ui_task()) {
        system_home_now();
        return;
    }
    UiCommand command{
        UiCommandType::system_home,
        {},
        {},
        0,
        0,
        nullptr,
        nullptr,
        nullptr,
    };
    enqueue(command);
}

bool display_publish_surfaces(
    const m3e::os::DomainSurfaceSnapshot& snapshot) {
    if (!g_display_ready) return false;
    auto* copy =
        new (std::nothrow) m3e::os::DomainSurfaceSnapshot(snapshot);
    if (copy == nullptr) return false;
    if (on_ui_task()) {
        return surface_publish_now(copy);
    }
    UiCommand command{
        UiCommandType::surface_publish,
        {},
        {},
        0,
        0,
        nullptr,
        nullptr,
        copy,
    };
    if (!enqueue(command)) {
        delete copy;
        return false;
    }
    return true;
}

void display_update() {
    if (!g_display_ready) {
        return;
    }
    if (!on_ui_task()) {
        ESP_LOGE(kTag, "[display] display_update called off UI task");
        return;
    }
    drain_ui_commands();
    M5.update();
    handle_system_inputs();
    complete_pending_flush();
    lv_timer_handler();
    complete_pending_flush();

    const auto now = esp_timer_get_time();
    const auto elapsed = now - g_window_started_us;
    if (elapsed >= kTelemetryIntervalMicroseconds) {
        const auto elapsed_seconds =
            static_cast<double>(elapsed) / 1000.0 / 1000.0;
        const auto fps =
            static_cast<double>(g_window_frames) / elapsed_seconds;
        const auto average_flush_us =
            g_window_flushes == 0
            ? 0
            : static_cast<std::uint32_t>(
                g_window_flush_us / g_window_flushes);
        const auto average_render_us =
            g_window_renders == 0
            ? 0
            : static_cast<std::uint32_t>(
                g_window_render_us / g_window_renders);
        ESP_LOGI(
            kTag,
            "[display] fps=%.1f frames=%u flushes=%u "
            "pixels=%llu avg_render_us=%u max_render_us=%u "
            "avg_flush_us=%u max_flush_us=%u touch_presses=%u dma=%s",
            fps,
            static_cast<unsigned>(g_window_frames),
            static_cast<unsigned>(g_window_flushes),
            static_cast<unsigned long long>(g_window_pixels),
            static_cast<unsigned>(average_render_us),
            static_cast<unsigned>(g_window_max_render_us),
            static_cast<unsigned>(average_flush_us),
            static_cast<unsigned>(g_window_max_flush_us),
            static_cast<unsigned>(g_window_touch_presses),
            M5.Display.dmaBusy() ? "busy" : "idle");
        g_window_frames = 0;
        g_window_renders = 0;
        g_window_flushes = 0;
        g_window_pixels = 0;
        g_window_flush_us = 0;
        g_window_max_flush_us = 0;
        g_window_render_us = 0;
        g_window_max_render_us = 0;
        g_window_touch_presses = 0;
        g_window_started_us = now;
    }
}
