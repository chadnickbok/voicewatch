#pragma once

#include <cstdint>

#include "lvgl.h"

namespace m3e::appspec {

constexpr std::int32_t kMaximumCanvasLogicalEdge = 192;
constexpr std::int32_t kMaximumCanvasDisplayListBytes = 128;
constexpr std::int32_t kMaximumCanvasPaletteBytes = 64;

bool validate_canvas_display_list(
    const char* display_list,
    const char* palette,
    std::int32_t logical_width,
    std::int32_t logical_height);

bool render_canvas_display_list(
    lv_obj_t* canvas,
    const char* display_list,
    const char* palette,
    std::int32_t logical_width,
    std::int32_t logical_height);

}  // namespace m3e::appspec
