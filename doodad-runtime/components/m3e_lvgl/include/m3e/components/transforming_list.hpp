#pragma once

#include <cstdint>

namespace m3e {

struct TransformingItemGeometry {
    std::uint16_t scale_q8_8;
    std::uint8_t opacity;
    std::int16_t transformed_height_px;
    std::int16_t y_offset_px;
};

TransformingItemGeometry transforming_item_geometry(
    std::int16_t item_center_y_px,
    std::int16_t viewport_height_px,
    std::int16_t base_height_px,
    bool reduced_motion = false);

std::int32_t preserve_anchor_scroll_offset(
    std::int32_t old_scroll_offset,
    std::int32_t old_anchor_y,
    std::int32_t new_anchor_y);

}  // namespace m3e
