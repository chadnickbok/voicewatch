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
bool display_mount_appspec(m3e::appspec::WireDocument* owned_document);
bool display_apply_command_batch(
    m3e::appspec::CommandBatch* owned_batch);
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
    std::uint8_t install_state);
void display_update();
