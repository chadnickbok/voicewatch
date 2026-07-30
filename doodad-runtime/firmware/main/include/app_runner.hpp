#pragma once

#include <cstdint>

#include "app_image.hpp"
#include "m3e/appspec/runtime.hpp"

bool app_runtime_init();
bool run_app(const AppImage& image);
bool app_post_ui_event(const m3e::appspec::UiEvent& event);
void app_runtime_update(std::uint32_t maximum_wait_ms);
