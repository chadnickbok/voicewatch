#pragma once

#include <cstddef>
#include <cstdint>

namespace m3e::appspec {
struct CommandBatch;
struct WireDocument;
}
namespace m3e::os {
struct DomainSurfaceSnapshot;
}

constexpr std::size_t kDisplayAgentTaskCapacity = 3;

struct DisplayAgentTask {
    char task_id[65]{};
    char title[25]{};
    char status[25]{};
    char elapsed[8]{};
    char context_label[13]{};
    char context[49]{};
    char stages[4][13]{};
    std::uint8_t completed_stage_count = 0;
    std::uint8_t active_stage = 0;
    std::uint8_t progress_percent = 0;
    std::uint32_t primary_color_rgb = 0x7241ff;
    std::uint8_t icon = 0;
};

bool display_init();
void display_shell(const char* status, const char* source);
// These functions always consume the owned object. Their result reflects the
// UI-thread renderer result, not merely successful queue admission.
bool display_mount_appspec(m3e::appspec::WireDocument* owned_document);
bool display_apply_command_batch(
    m3e::appspec::CommandBatch* owned_batch);
// Synchronously closes trusted overlays and releases the outgoing app surface
// before the runtime replaces the resident guest.
bool display_prepare_app_switch(const char* theme_seed = nullptr);
void display_error(const char* stage);
void display_show_catalog(int story);
void display_show_system_home();
bool display_publish_surfaces(
    const m3e::os::DomainSurfaceSnapshot& snapshot);
bool display_publish_agent_state(
    std::uint8_t voice_phase,
    std::uint8_t running_count,
    bool focused_question,
    bool review_ready,
    bool completion_pending,
    std::uint8_t install_state,
    const char* transcript,
    const char* response,
    const DisplayAgentTask* tasks = nullptr,
    std::size_t task_count = 0,
    bool task_status_changed = false);
bool display_publish_voice_level(std::uint8_t level);
bool display_publish_install_state(
    std::uint8_t phase,
    const char* detail = nullptr);
bool display_publish_app_ready(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* icon,
    const char* theme_seed,
    const char* payload_sha256);
bool display_note_app_running(
    const char* app_id,
    const char* semantic_version,
    const char* payload_sha256);
bool display_publish_app_rollback(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* icon,
    const char* theme_seed,
    const char* payload_sha256);
bool display_publish_app_current_recovery(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* icon,
    const char* theme_seed,
    const char* payload_sha256);
bool display_refresh_installed_apps();
void display_update();
