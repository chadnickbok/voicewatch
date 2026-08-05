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

bool display_init();
void display_shell(const char* status, const char* source);
// These functions always consume the owned object. Their result reflects the
// UI-thread renderer result, not merely successful queue admission.
bool display_mount_appspec(m3e::appspec::WireDocument* owned_document);
bool display_apply_command_batch(
    m3e::appspec::CommandBatch* owned_batch);
// Synchronously closes trusted overlays and releases the outgoing app surface
// before the runtime replaces the resident guest.
bool display_prepare_app_switch();
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
    const char* response);
bool display_publish_voice_level(std::uint8_t level);
bool display_publish_install_state(
    std::uint8_t phase,
    const char* detail = nullptr);
bool display_publish_app_ready(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* payload_sha256);
bool display_note_app_running(
    const char* app_id,
    const char* semantic_version,
    const char* payload_sha256);
bool display_publish_app_rollback(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* payload_sha256);
bool display_publish_app_current_recovery(
    const char* app_id,
    const char* name,
    const char* semantic_version,
    const char* payload_sha256);
bool display_refresh_installed_apps();
void display_update();
