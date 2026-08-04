#include "display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "app_runner.hpp"
#include "board.hpp"
#include "doodad_lvgl_ui.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
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
#include "voice_service.hpp"

namespace {

constexpr char kTag[] = "doodad";
constexpr std::int32_t kDrawRows = 40;
constexpr std::size_t kDrawBufferPixels = DOODAD_SURFACE_WIDTH * kDrawRows;
constexpr std::size_t kDrawBufferBytes = kDrawBufferPixels * sizeof(std::uint16_t);
constexpr std::uint16_t kPhysicalBackground = 0x0841;
constexpr std::int64_t kTelemetryIntervalMicroseconds = 2 * 1000 * 1000;
constexpr std::size_t kUiQueueDepth = 8;

enum class UiCommandType : std::uint8_t {
    shell,
    appspec,
    command_batch,
    error,
    catalog,
    system_home,
    surface_publish,
    agent_state,
    voice_level,
};

struct UiCommand {
    UiCommandType type;
    char primary[161];
    char secondary[161];
    std::size_t length;
    int story;
    m3e::appspec::WireDocument* document;
    m3e::appspec::CommandBatch* batch;
    m3e::os::DomainSurfaceSnapshot* surfaces;
    std::uint8_t voice_phase;
    std::uint8_t running_count;
    bool focused_question;
    bool review_ready;
    bool completion_pending;
    std::uint8_t install_state;
    std::uint8_t voice_level;
};

enum class PendingVoiceAction : std::uint8_t {
    none,
    primary,
    cancel,
};

bool g_display_ready = false;
TaskHandle_t g_ui_task = nullptr;
QueueHandle_t g_ui_queue = nullptr;
lv_display_t* g_lvgl_display = nullptr;
lv_indev_t* g_touch_input = nullptr;
doodad_lvgl_ui_t g_ui{};
m3e::StyleRegistry g_appspec_styles{};
m3e::appspec::WireDocument* g_active_document = nullptr;
std::uint16_t* g_draw_buffer_a = nullptr;
std::uint16_t* g_draw_buffer_b = nullptr;
std::uint32_t g_window_frames = 0;
std::uint32_t g_window_renders = 0;
std::uint32_t g_window_flushes = 0;
std::uint64_t g_window_pixels = 0;
std::uint64_t g_window_flush_us = 0;
std::uint32_t g_window_max_flush_us = 0;
std::uint64_t g_window_render_us = 0;
std::uint32_t g_window_max_render_us = 0;
std::uint32_t g_lifetime_frames = 0;
std::uint32_t g_lifetime_renders = 0;
std::uint32_t g_lifetime_flushes = 0;
std::uint64_t g_lifetime_pixels = 0;
std::uint64_t g_lifetime_flush_us = 0;
std::uint32_t g_lifetime_max_flush_us = 0;
std::uint64_t g_lifetime_render_us = 0;
std::uint32_t g_lifetime_max_render_us = 0;
std::int64_t g_render_started_us = 0;
std::int64_t g_window_started_us = 0;
std::uint32_t g_window_touch_presses = 0;
bool g_touch_pressed = false;
lv_point_t g_last_touch_point{0, 0};
m3e::os::ShellState g_shell{};
m3e::os::SurfaceRegistry* g_surface_registry = nullptr;
bool g_shell_active = false;
m3e_voice_runtime_view_t g_voice_view{};
char g_voice_transcript[161]{};
char g_voice_response[161]{};
PendingVoiceAction g_pending_voice_action = PendingVoiceAction::none;
char g_visual_scene[49]{};
std::uint32_t g_visual_revision = 0;
std::uint32_t g_visual_frame_hash = 2166136261U;
bool g_visual_pending = false;

void stage_visual_scene(const char* scene) {
    std::strncpy(
        g_visual_scene,
        scene == nullptr ? "unknown" : scene,
        sizeof(g_visual_scene) - 1);
    g_visual_scene[sizeof(g_visual_scene) - 1] = '\0';
    ++g_visual_revision;
    g_visual_pending = true;
}

std::uint32_t tick_milliseconds() {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

std::uint32_t object_count(lv_obj_t* root) {
    if (root == nullptr) {
        return 0;
    }
    std::uint32_t count = 1;
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        count += object_count(lv_obj_get_child(root, index));
    }
    return count;
}

void flush_display(
    lv_display_t* display, const lv_area_t* area, std::uint8_t* pixel_map) {
    const auto width = area->x2 - area->x1 + 1;
    const auto height = area->y2 - area->y1 + 1;
    const auto started_us = esp_timer_get_time();
    g_window_pixels +=
        static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height);
    g_lifetime_pixels +=
        static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height);
    const auto* hash_bytes = reinterpret_cast<const std::uint8_t*>(pixel_map);
    const auto hash_size = static_cast<std::size_t>(width) * height * 2;
    for (std::size_t index = 0; index < hash_size; ++index) {
        g_visual_frame_hash ^= hash_bytes[index];
        g_visual_frame_hash *= 16777619U;
    }
    // LVGL owns pixel_map and may reuse it as soon as flush_ready is called.
    // The adapter does not return until the board-specific transfer has
    // consumed the complete LVGL strip.
    if (!doodad::board::display_flush(
        doodad::board::viewport_x() + area->x1,
        area->y1,
        width,
        height,
        reinterpret_cast<const std::uint16_t*>(pixel_map))) {
        ESP_LOGE(kTag, "[display] board flush failed");
    }
    const auto duration =
        static_cast<std::uint32_t>(esp_timer_get_time() - started_us);
    ++g_window_flushes;
    g_window_flush_us += duration;
    g_window_max_flush_us =
        std::max(g_window_max_flush_us, duration);
    ++g_lifetime_flushes;
    g_lifetime_flush_us += duration;
    g_lifetime_max_flush_us =
        std::max(g_lifetime_max_flush_us, duration);
    lv_display_flush_ready(display);
}

void display_event(lv_event_t* event) {
    switch (lv_event_get_code(event)) {
        case LV_EVENT_RENDER_START:
            g_render_started_us = esp_timer_get_time();
            g_visual_frame_hash = 2166136261U;
            break;
        case LV_EVENT_RENDER_READY: {
            const auto duration = static_cast<std::uint32_t>(
                esp_timer_get_time() - g_render_started_us);
            ++g_window_renders;
            g_window_render_us += duration;
            g_window_max_render_us =
                std::max(g_window_max_render_us, duration);
            ++g_lifetime_renders;
            g_lifetime_render_us += duration;
            g_lifetime_max_render_us =
                std::max(g_lifetime_max_render_us, duration);
            break;
        }
        case LV_EVENT_REFR_READY:
            ++g_window_frames;
            ++g_lifetime_frames;
            if (g_visual_pending) {
                ESP_LOGI(
                    kTag,
                    "[visual] device_id=%s scene=%s revision=%u frame_hash=%08lx",
                    doodad::board::identity().device_id,
                    g_visual_scene,
                    static_cast<unsigned>(g_visual_revision),
                    static_cast<unsigned long>(g_visual_frame_hash));
                g_visual_pending = false;
            }
            break;
        default:
            break;
    }
}

void read_touch(lv_indev_t*, lv_indev_data_t* data) {
    doodad::board::TouchPoint point{};
    const bool pressed = doodad::board::touch_read(point);
    if (pressed) {
        g_last_touch_point.x = point.x;
        g_last_touch_point.y = point.y;
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
    stage_visual_scene("boot-shell");
    doodad_lvgl_ui_show_shell(&g_ui, status, source);
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
    if (g_shell_active &&
        g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        // The trusted system voice surface owns the display while it is open.
        // A voice-aware guest may still receive provider events, but it must
        // not replace or invalidate native overlay objects.
        delete document;
        return true;
    }
    if (!g_appspec_styles.initialized() &&
        !g_appspec_styles.initialize(m3e::baseline_dark_theme())) {
        delete document;
        error_now("THEME INIT FAILED");
        return false;
    }
    stage_visual_scene("appspec");
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
    return true;
}

bool command_batch_now(m3e::appspec::CommandBatch* batch) {
    if (batch == nullptr) return false;
    if (g_shell_active &&
        g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        delete batch;
        return true;
    }
    if (g_active_document == nullptr) {
        delete batch;
        ESP_LOGE(kTag, "[display] no mounted AppSpec for CommandBatch");
        return false;
    }
    stage_visual_scene("appspec-update");
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
    // Text and value patches can invalidate a transparent child without
    // invalidating the surface behind it. Repaint the composed app surface so
    // a label update cannot flush an otherwise-clear draw buffer over its
    // parent button/card.
    lv_obj_invalidate(lv_screen_active());
    return true;
}

void error_now(const char* stage) {
    stage_visual_scene("error");
    doodad::board::haptic(8);
    doodad_lvgl_ui_show_error(&g_ui, stage);
}

void catalog_now(int story) {
    char scene[49]{};
    std::snprintf(scene, sizeof(scene), "catalog-%d", story);
    stage_visual_scene(scene);
    m3e_catalog_show(lv_screen_active(), story);
    if (story == M3E_CATALOG_STORY_COLOR_BARS) {
        // Board marker in the unused edge of the black calibration patch:
        // CoreS3 has one white cell and T-Watch has two. This lets the camera
        // label panels by content even if their physical positions are swapped.
        const auto marker_count =
            std::strcmp(doodad::board::identity().board, "cores3") == 0 ? 1 : 2;
        for (int index = 0; index < marker_count; ++index) {
            auto* marker = lv_obj_create(lv_screen_active());
            lv_obj_remove_style_all(marker);
            lv_obj_set_pos(marker, 207 + index * 7, 12);
            lv_obj_set_size(marker, 4, 4);
            lv_obj_set_style_bg_color(marker, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

void render_background_badge() {
    const auto& activity = g_shell.snapshot().background;
    if (activity.running_count == 0 && !activity.focused_question &&
        !activity.review_ready && !activity.completion_pending &&
        activity.install_state == m3e::os::BackgroundInstallState::none) {
        return;
    }
    auto* badge = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(badge);
    lv_obj_set_size(badge, 58, 30);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -12, 10);
    lv_obj_set_style_radius(badge, 15, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x2D2648), 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(0xC8B6FF), 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    auto* label = lv_label_create(badge);
    char text[12]{};
    if (activity.focused_question) {
        std::snprintf(text, sizeof(text), "?  %u", activity.running_count);
    } else if (activity.completion_pending || activity.review_ready) {
        std::snprintf(text, sizeof(text), "!  %u", activity.running_count);
    } else {
        std::snprintf(text, sizeof(text), "\xE2\x80\xA2  %u", activity.running_count);
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF3EDFF), 0);
    lv_obj_center(label);
}

int shell_story() {
    const auto& snapshot = g_shell.snapshot();
    switch (snapshot.overlay) {
        case m3e::os::Overlay::voice:
            switch (snapshot.voice_phase) {
                case m3e::os::VoicePhase::listening:
                    return M3E_CATALOG_STORY_OS_VOICE;
                case m3e::os::VoicePhase::thinking:
                case m3e::os::VoicePhase::clarifying:
                    return M3E_CATALOG_STORY_OS_VOICE_THINKING;
                case m3e::os::VoicePhase::speaking:
                    return M3E_CATALOG_STORY_OS_VOICE_RESULT;
                case m3e::os::VoicePhase::error:
                    return M3E_CATALOG_STORY_OS_ERROR;
                case m3e::os::VoicePhase::idle:
                case m3e::os::VoicePhase::ready:
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
    const char* visual_scene = "system-shell";
    if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        switch (g_shell.snapshot().voice_phase) {
            case m3e::os::VoicePhase::listening: visual_scene = "voice-listening"; break;
            case m3e::os::VoicePhase::thinking: visual_scene = "voice-thinking"; break;
            case m3e::os::VoicePhase::speaking: visual_scene = "voice-speaking"; break;
            case m3e::os::VoicePhase::error: visual_scene = "voice-error"; break;
            default: visual_scene = "voice-ready"; break;
        }
    } else if (g_shell.snapshot().surface == m3e::os::Surface::watch_face) {
        visual_scene = "home";
    }
    stage_visual_scene(visual_scene);
    if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        m3e_catalog_show_voice_runtime(
            lv_screen_active(),
            static_cast<int>(g_shell.snapshot().voice_phase),
            g_voice_transcript,
            g_voice_response,
            &g_voice_view);
        // Rendering the trusted surface replaces the mounted guest tree.
        // Its document must no longer be eligible for later command batches.
        delete g_active_document;
        g_active_document = nullptr;
        if (g_voice_view.primary_action != nullptr) {
            lv_obj_add_event_cb(
                g_voice_view.primary_action,
                [](lv_event_t* event) {
                    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
                        g_pending_voice_action = PendingVoiceAction::primary;
                    }
                },
                LV_EVENT_CLICKED,
                nullptr);
        }
        if (g_voice_view.cancel_action != nullptr) {
            lv_obj_add_event_cb(
                g_voice_view.cancel_action,
                [](lv_event_t* event) {
                    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
                        g_pending_voice_action = PendingVoiceAction::cancel;
                    }
                },
                LV_EVENT_CLICKED,
                nullptr);
        }
    } else {
        g_voice_view = {};
        catalog_now(shell_story());
    }
    render_background_badge();
}

bool force_voice_phase(m3e::os::VoicePhase phase) {
    if (g_shell.set_voice_phase(phase)) return true;
    if (!g_shell.set_voice_phase(m3e::os::VoicePhase::idle)) return false;
    if (phase == m3e::os::VoicePhase::idle) return true;
    if (phase == m3e::os::VoicePhase::ready) {
        return g_shell.set_voice_phase(phase);
    }
    if (!g_shell.set_voice_phase(m3e::os::VoicePhase::listening)) return false;
    return phase == m3e::os::VoicePhase::listening ||
        g_shell.set_voice_phase(phase);
}

void show_voice_error(const char* message) {
    if (g_shell.snapshot().overlay != m3e::os::Overlay::voice) {
        g_shell.show_overlay(m3e::os::Overlay::voice);
    }
    std::strncpy(
        g_voice_response,
        message == nullptr ? "Voice unavailable" : message,
        sizeof(g_voice_response) - 1);
    force_voice_phase(m3e::os::VoicePhase::error);
    render_shell_now();
}

void perform_voice_action(PendingVoiceAction action) {
    if (!g_shell_active || action == PendingVoiceAction::none) return;
    if (action == PendingVoiceAction::cancel) {
        voice_service_request("system.voice.cancel", 0);
        if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
            g_shell.dismiss_overlay();
            render_shell_now();
        }
        return;
    }

    const auto phase = g_shell.snapshot().voice_phase;
    const char* operation = "system.voice.activate";
    auto next = m3e::os::VoicePhase::listening;
    if (phase == m3e::os::VoicePhase::listening) {
        operation = "system.voice.finish";
        next = m3e::os::VoicePhase::thinking;
    } else if (phase == m3e::os::VoicePhase::speaking) {
        operation = "system.voice.interrupt";
    } else if (phase != m3e::os::VoicePhase::ready &&
               phase != m3e::os::VoicePhase::idle &&
               g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        return;
    }

    if (!voice_service_request(operation, 0, 30'000)) {
        show_voice_error("Voice service unavailable");
        return;
    }
    if (g_shell.snapshot().overlay != m3e::os::Overlay::voice) {
        g_shell.show_overlay(m3e::os::Overlay::voice);
    }
    if (next == m3e::os::VoicePhase::listening) {
        g_voice_transcript[0] = '\0';
        g_voice_response[0] = '\0';
    }
    force_voice_phase(next);
    render_shell_now();
}

void voice_level_now(std::uint8_t level) {
    if (g_shell.snapshot().overlay != m3e::os::Overlay::voice ||
        g_shell.snapshot().voice_phase != m3e::os::VoicePhase::listening ||
        g_voice_view.level_ring == nullptr ||
        !lv_obj_is_valid(g_voice_view.level_ring)) {
        return;
    }
    const auto bounded = std::min<std::uint8_t>(level, 100);
    lv_obj_set_style_border_width(
        g_voice_view.level_ring, 3 + bounded / 25, 0);
    lv_obj_set_style_border_opa(
        g_voice_view.level_ring,
        static_cast<lv_opa_t>(LV_OPA_30 + bounded * 150 / 100),
        0);
    lv_obj_invalidate(g_voice_view.level_ring);
}

void system_home_now() {
    stage_visual_scene("home");
    if (!g_shell.initialize()) {
        error_now("SYSTEM SHELL FAILED");
        return;
    }
    g_surface_registry->sync_shell_counts(g_shell);
    g_shell_active = true;
    render_shell_now();
}

bool surface_publish_now(
    m3e::os::DomainSurfaceSnapshot* snapshot) {
    if (snapshot == nullptr) return false;
    const auto published = g_surface_registry->publish(*snapshot);
    delete snapshot;
    if (!published) {
        ESP_LOGW(kTag, "[system] rejected surface publication");
        return false;
    }
    g_surface_registry->sync_shell_counts(g_shell);
    if (g_shell_active && g_shell.snapshot().display_awake) {
        render_shell_now();
    }
    return true;
}

void agent_state_now(const UiCommand& command) {
    bool initialized_now = false;
    if (!g_shell_active) {
        if (!g_shell.initialize()) {
            ESP_LOGE(kTag, "[system] agent state could not initialize shell");
            return;
        }
        g_surface_registry->sync_shell_counts(g_shell);
        g_shell_active = true;
        initialized_now = true;
    }
    const auto generation_before = g_shell.snapshot().generation;
    const bool text_changed =
        std::strcmp(g_voice_transcript, command.primary) != 0 ||
        std::strcmp(g_voice_response, command.secondary) != 0;
    std::strncpy(
        g_voice_transcript, command.primary, sizeof(g_voice_transcript) - 1);
    std::strncpy(
        g_voice_response, command.secondary, sizeof(g_voice_response) - 1);
    const auto requested = command.voice_phase <=
            static_cast<std::uint8_t>(m3e::os::VoicePhase::ready)
        ? static_cast<m3e::os::VoicePhase>(command.voice_phase)
        : m3e::os::VoicePhase::error;
    if (requested == m3e::os::VoicePhase::idle) {
        if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
            g_shell.dismiss_overlay();
        }
    } else if (requested != m3e::os::VoicePhase::ready ||
               g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
        if (g_shell.snapshot().overlay != m3e::os::Overlay::voice) {
            g_shell.show_overlay(m3e::os::Overlay::voice);
        }
        force_voice_phase(requested);
    }
    const auto install_state = command.install_state <=
            static_cast<std::uint8_t>(
                m3e::os::BackgroundInstallState::failed)
        ? static_cast<m3e::os::BackgroundInstallState>(command.install_state)
        : m3e::os::BackgroundInstallState::failed;
    const auto previous_question = g_shell.snapshot().background.focused_question;
    const auto previous_completion = g_shell.snapshot().background.completion_pending;
    g_shell.publish_background_activity(
        command.running_count,
        command.focused_question,
        command.review_ready,
        command.completion_pending,
        install_state);
    if (command.focused_question && !previous_question) {
        doodad::board::haptic(10);
    } else if (command.completion_pending && !previous_completion) {
        doodad::board::haptic(47);
    }
    if (g_shell_active && g_shell.snapshot().display_awake &&
        (initialized_now || text_changed ||
         g_shell.snapshot().generation != generation_before)) {
        render_shell_now();
    }
}

void dispatch_system_input(m3e::os::Input input) {
    if (!g_shell_active) return;
    const auto intent = m3e::os::map_input(input);
    if (intent == m3e::os::Intent::none ||
        !g_shell.dispatch(intent)) {
        return;
    }
    doodad::board::haptic(1);
    const auto& snapshot = g_shell.snapshot();
    doodad::board::display_set_brightness(
        snapshot.display_awake
            ? doodad::board::display_default_brightness()
            : 0);
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
    const auto inputs = doodad::board::take_input();
    if (inputs.button_b_held) {
        perform_voice_action(PendingVoiceAction::primary);
    } else if (inputs.button_b_clicked) {
        if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
            perform_voice_action(PendingVoiceAction::primary);
        } else {
            dispatch_system_input(m3e::os::Input::button_b);
        }
    }
    if (inputs.button_a_clicked) {
        if (g_shell.snapshot().overlay == m3e::os::Overlay::voice) {
            perform_voice_action(PendingVoiceAction::cancel);
        } else {
            dispatch_system_input(m3e::os::Input::button_a);
        }
    }
    if (inputs.button_c_clicked) {
        dispatch_system_input(m3e::os::Input::button_c);
    }
    if (inputs.power_clicked) {
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
            case UiCommandType::agent_state:
                agent_state_now(command);
                break;
            case UiCommandType::voice_level:
                voice_level_now(command.voice_level);
                break;
        }
    }
}

}  // namespace

bool display_init() {
    g_ui_task = xTaskGetCurrentTaskHandle();
    auto* surface_storage = heap_caps_calloc(
        1,
        sizeof(m3e::os::SurfaceRegistry),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (surface_storage == nullptr) {
        ESP_LOGE(kTag, "[display] surface registry allocation failed");
        return false;
    }
    g_surface_registry =
        new (surface_storage) m3e::os::SurfaceRegistry{};
    g_ui_queue = xQueueCreateWithCaps(
        kUiQueueDepth,
        sizeof(UiCommand),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_ui_queue == nullptr) {
        ESP_LOGE(kTag, "[display] UI command queue allocation failed");
        return false;
    }
    if (!doodad::board::init()) {
        ESP_LOGE(kTag, "[host] board init failed");
        return false;
    }
    const bool supported_size =
        doodad::board::display_width() >= DOODAD_SURFACE_WIDTH
        && doodad::board::display_height() >= DOODAD_SURFACE_HEIGHT;
    if (!supported_size) {
        ESP_LOGE(
            kTag,
            "[host] display init failed (board=%s, size=%ldx%ld)",
            doodad::board::identity().board,
            static_cast<long>(doodad::board::display_width()),
            static_cast<long>(doodad::board::display_height()));
        return false;
    }

    // The portable app surface is always 240x240. CoreS3's extra horizontal
    // pixels are host-owned gutters, never additional app layout space.
    doodad::board::display_fill(kPhysicalBackground);

    lv_init();
    lv_tick_set_cb(tick_milliseconds);
    g_lvgl_display =
        lv_display_create(DOODAD_SURFACE_WIDTH, DOODAD_SURFACE_HEIGHT);
    if (g_lvgl_display == nullptr) {
        ESP_LOGE(kTag, "[host] LVGL display creation failed");
        return false;
    }
    const auto draw_buffer_caps = doodad::board::draw_buffer_caps();
    g_draw_buffer_a = static_cast<std::uint16_t*>(
        heap_caps_aligned_alloc(4, kDrawBufferBytes, draw_buffer_caps));
    g_draw_buffer_b = static_cast<std::uint16_t*>(
        heap_caps_aligned_alloc(4, kDrawBufferBytes, draw_buffer_caps));
    if (g_draw_buffer_a == nullptr || g_draw_buffer_b == nullptr) {
        ESP_LOGE(
            kTag,
            "[host] draw-buffer allocation failed (%u bytes each)",
            static_cast<unsigned>(kDrawBufferBytes));
        heap_caps_free(g_draw_buffer_a);
        heap_caps_free(g_draw_buffer_b);
        g_draw_buffer_a = nullptr;
        g_draw_buffer_b = nullptr;
        return false;
    }
    ESP_LOGI(
        kTag,
        "[display] LVGL draw buffers caps=0x%lx (%u bytes each)",
        static_cast<unsigned long>(draw_buffer_caps),
        static_cast<unsigned>(kDrawBufferBytes));
    lv_display_set_color_format(g_lvgl_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(
        g_lvgl_display,
        g_draw_buffer_a,
        g_draw_buffer_b,
        kDrawBufferBytes,
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

bool display_publish_agent_state(
    std::uint8_t voice_phase,
    std::uint8_t running_count,
    bool focused_question,
    bool review_ready,
    bool completion_pending,
    std::uint8_t install_state,
    const char* transcript,
    const char* response) {
    UiCommand command{};
    command.type = UiCommandType::agent_state;
    command.voice_phase = voice_phase;
    command.running_count = running_count;
    command.focused_question = focused_question;
    command.review_ready = review_ready;
    command.completion_pending = completion_pending;
    command.install_state = install_state;
    std::strncpy(
        command.primary, transcript == nullptr ? "" : transcript,
        sizeof(command.primary) - 1);
    std::strncpy(
        command.secondary, response == nullptr ? "" : response,
        sizeof(command.secondary) - 1);
    if (!g_display_ready) return false;
    if (on_ui_task()) {
        agent_state_now(command);
        return true;
    }
    return enqueue(command);
}

bool display_publish_voice_level(std::uint8_t level) {
    if (!g_display_ready) return false;
    UiCommand command{};
    command.type = UiCommandType::voice_level;
    command.voice_level = std::min<std::uint8_t>(level, 100);
    if (on_ui_task()) {
        voice_level_now(command.voice_level);
        return true;
    }
    return enqueue(command);
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
    doodad::board::update();
    handle_system_inputs();
    lv_timer_handler();
    if (g_pending_voice_action != PendingVoiceAction::none) {
        const auto action = g_pending_voice_action;
        g_pending_voice_action = PendingVoiceAction::none;
        perform_voice_action(action);
    }

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
        const auto internal_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        const auto psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        const auto lifetime_average_render_us =
            g_lifetime_renders == 0
            ? 0
            : static_cast<std::uint32_t>(
                g_lifetime_render_us / g_lifetime_renders);
        const auto lifetime_average_flush_us =
            g_lifetime_flushes == 0
            ? 0
            : static_cast<std::uint32_t>(
                g_lifetime_flush_us / g_lifetime_flushes);
        ESP_LOGI(
            kTag,
            "[display] fps=%.1f frames=%u flushes=%u "
            "pixels=%llu avg_render_us=%u max_render_us=%u "
            "avg_flush_us=%u max_flush_us=%u touch_presses=%u "
            "objects=%u internal_free=%u internal_min=%u internal_largest=%u "
            "psram_free=%u psram_min=%u psram_largest=%u "
            "total_frames=%u total_flushes=%u total_pixels=%llu "
            "total_avg_render_us=%u total_max_render_us=%u "
            "total_avg_flush_us=%u total_max_flush_us=%u transfer=%s",
            fps,
            static_cast<unsigned>(g_window_frames),
            static_cast<unsigned>(g_window_flushes),
            static_cast<unsigned long long>(g_window_pixels),
            static_cast<unsigned>(average_render_us),
            static_cast<unsigned>(g_window_max_render_us),
            static_cast<unsigned>(average_flush_us),
            static_cast<unsigned>(g_window_max_flush_us),
            static_cast<unsigned>(g_window_touch_presses),
            static_cast<unsigned>(object_count(lv_screen_active())),
            static_cast<unsigned>(heap_caps_get_free_size(internal_caps)),
            static_cast<unsigned>(heap_caps_get_minimum_free_size(internal_caps)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(internal_caps)),
            static_cast<unsigned>(heap_caps_get_free_size(psram_caps)),
            static_cast<unsigned>(heap_caps_get_minimum_free_size(psram_caps)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(psram_caps)),
            static_cast<unsigned>(g_lifetime_frames),
            static_cast<unsigned>(g_lifetime_flushes),
            static_cast<unsigned long long>(g_lifetime_pixels),
            static_cast<unsigned>(lifetime_average_render_us),
            static_cast<unsigned>(g_lifetime_max_render_us),
            static_cast<unsigned>(lifetime_average_flush_us),
            static_cast<unsigned>(g_lifetime_max_flush_us),
            "synchronous");
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
