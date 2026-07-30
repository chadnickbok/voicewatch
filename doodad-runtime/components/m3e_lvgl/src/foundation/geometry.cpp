#include "m3e/foundation/geometry.hpp"

#include <algorithm>

namespace m3e {

std::int16_t shape_radius_dp(
    generated::ShapeRole role,
    std::int16_t width_dp,
    std::int16_t height_dp) {
    const auto token =
        generated::kShapeTokens[static_cast<std::size_t>(role)];
    if (token.kind == generated::ShapeKind::full) {
        return std::max<std::int16_t>(
            0, std::min(width_dp, height_dp) / 2);
    }
    return static_cast<std::int16_t>(token.radius_dp_q8_8 / 256U);
}

std::int16_t interpolate_radius_dp(
    std::int16_t from_dp,
    std::int16_t to_dp,
    std::uint16_t progress_q16) {
    const auto delta =
        static_cast<std::int32_t>(to_dp) - static_cast<std::int32_t>(from_dp);
    return static_cast<std::int16_t>(
        from_dp +
        (delta * static_cast<std::int32_t>(progress_q16) + 32767) / 65535);
}

ButtonGroupLayout button_group_layout(
    std::int16_t total_width_dp,
    std::uint8_t count,
    std::int8_t emphasized_index,
    std::int16_t growth_dp) {
    count = std::clamp<std::uint8_t>(count, 1, 3);
    constexpr std::int16_t kGapDp = 4;
    const auto available =
        std::max<std::int16_t>(count, total_width_dp - (count - 1) * kGapDp);
    ButtonGroupLayout result{{0, 0, 0}, count};
    const auto base = static_cast<std::int16_t>(available / count);
    auto remainder = static_cast<std::int16_t>(available % count);
    for (std::uint8_t index = 0; index < count; ++index) {
        result.visual_widths_dp[index] =
            static_cast<std::int16_t>(base + (remainder-- > 0 ? 1 : 0));
    }
    if (emphasized_index < 0 || emphasized_index >= count || count == 1) {
        return result;
    }
    const auto emphasized = static_cast<std::uint8_t>(emphasized_index);
    const auto maximum_growth =
        static_cast<std::int16_t>((count - 1) * std::max(0, base - 24));
    const auto growth =
        std::clamp<std::int16_t>(growth_dp, 0, maximum_growth);
    result.visual_widths_dp[emphasized] += growth;
    if (count == 2) {
        result.visual_widths_dp[1 - emphasized] -= growth;
    } else if (emphasized == 1) {
        const auto left = static_cast<std::int16_t>(growth / 2);
        result.visual_widths_dp[0] -= left;
        result.visual_widths_dp[2] -=
            static_cast<std::int16_t>(growth - left);
    } else {
        const auto neighbor = emphasized == 0 ? 1U : 1U;
        result.visual_widths_dp[neighbor] -= growth;
    }
    return result;
}

}  // namespace m3e
