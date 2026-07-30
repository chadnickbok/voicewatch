#pragma once

#include <array>
#include <cstdint>

#include "m3e/generated/core_tokens.hpp"

namespace m3e {

std::int16_t shape_radius_dp(
    generated::ShapeRole role,
    std::int16_t width_dp,
    std::int16_t height_dp);

std::int16_t interpolate_radius_dp(
    std::int16_t from_dp,
    std::int16_t to_dp,
    std::uint16_t progress_q16);

struct ButtonGroupLayout {
    std::array<std::int16_t, 3> visual_widths_dp;
    std::uint8_t count;
};

ButtonGroupLayout button_group_layout(
    std::int16_t total_width_dp,
    std::uint8_t count,
    std::int8_t emphasized_index = -1,
    std::int16_t growth_dp = 24);

}  // namespace m3e
