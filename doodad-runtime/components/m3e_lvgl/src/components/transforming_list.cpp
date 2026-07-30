#include "m3e/components/transforming_list.hpp"

#include <algorithm>
#include <cstdlib>

namespace m3e {

TransformingItemGeometry transforming_item_geometry(
    std::int16_t item_center_y_px,
    std::int16_t viewport_height_px,
    std::int16_t base_height_px,
    bool reduced_motion) {
    const auto viewport_center = viewport_height_px / 2;
    const auto distance = static_cast<std::int32_t>(
        std::abs(item_center_y_px - viewport_center));
    const auto half = std::max<std::int32_t>(1, viewport_height_px / 2);
    const auto normalized_q8_8 =
        std::min<std::int32_t>(256, distance * 256 / half);
    const auto minimum_scale = reduced_motion ? 230 : 184;
    const auto scale = static_cast<std::int32_t>(
        256 - (256 - minimum_scale) * normalized_q8_8 / 256);
    const auto opacity = static_cast<std::int32_t>(
        255 - (reduced_motion ? 50 : 110) * normalized_q8_8 / 256);
    const auto height =
        std::max<std::int32_t>(1, base_height_px * scale / 256);
    return TransformingItemGeometry{
        static_cast<std::uint16_t>(scale),
        static_cast<std::uint8_t>(opacity),
        static_cast<std::int16_t>(height),
        static_cast<std::int16_t>((base_height_px - height) / 2),
    };
}

std::int32_t preserve_anchor_scroll_offset(
    std::int32_t old_scroll_offset,
    std::int32_t old_anchor_y,
    std::int32_t new_anchor_y) {
    return old_scroll_offset + (new_anchor_y - old_anchor_y);
}

}  // namespace m3e
