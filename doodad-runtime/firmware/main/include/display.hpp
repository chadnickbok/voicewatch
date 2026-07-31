#pragma once

#include <cstddef>

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
void display_update();
