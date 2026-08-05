#pragma once

#include <cstddef>
#include <cstdint>

#include "app_image.hpp"
#include "m3e/appspec/runtime.hpp"

struct AppRuntimeIdentity {
    // The personal-bundle contract currently allows 64-byte app IDs,
    // 64-byte semantic-version strings, and a 64-character payload SHA-256.
    char app_id[97]{};
    char semantic_version[65]{};
    char generation[65]{};
};

enum class AppRuntimeFailureKind : std::uint8_t {
    none = 0,
    ui_event = 1,
    provider_event = 2,
    timer_event = 3,
};

struct AppRuntimeFailure {
    std::uint64_t sequence = 0;
    AppRuntimeFailureKind kind = AppRuntimeFailureKind::none;
    AppRuntimeIdentity identity{};
};

bool app_runtime_init();
bool run_app(const AppImage& image);
bool app_runtime_current_identity(AppRuntimeIdentity& identity);
// Consumes the single bounded failure latch. The copied generation identity
// lets the runtime manager reject a stale report defensively. The failed
// guest remains quarantined until run_app() replaces it.
bool app_runtime_poll_failure(AppRuntimeFailure& failure);
// Invalidates the mounted UI only when `document_app_id` is the exact string
// pointer owned by that WireDocument. Display calls this before freeing a
// document so callbacks cannot retain authority through allocator reuse.
void app_runtime_invalidate_ui_mount(const char* document_app_id);
bool app_post_ui_event(const m3e::appspec::UiEvent& event);
// One boot-only trusted injection uses value equality because it is not
// emitted by an LVGL binding and therefore has no WireDocument string pointer.
// Ordinary callbacks must always use app_post_ui_event's pointer-only origin.
bool app_post_embedded_ui_event(const m3e::appspec::UiEvent& event);
void app_runtime_update(std::uint32_t maximum_wait_ms);
