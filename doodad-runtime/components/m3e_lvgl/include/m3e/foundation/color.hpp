#pragma once

#include <cstdint>

#include "m3e/generated/core_tokens.hpp"

namespace m3e {

using ColorRgb888 = generated::ColorRgb888;

struct ColorRgb565 {
    std::uint16_t value;
};

constexpr ColorRgb565 quantize_rgb565(ColorRgb888 color) {
    return ColorRgb565{generated::to_rgb565(color)};
}

constexpr ColorRgb888 expand_rgb565(ColorRgb565 color) {
    const auto red5 = static_cast<std::uint8_t>((color.value >> 11U) & 0x1FU);
    const auto green6 = static_cast<std::uint8_t>((color.value >> 5U) & 0x3FU);
    const auto blue5 = static_cast<std::uint8_t>(color.value & 0x1FU);
    return ColorRgb888{
        static_cast<std::uint8_t>((red5 << 3U) | (red5 >> 2U)),
        static_cast<std::uint8_t>((green6 << 2U) | (green6 >> 4U)),
        static_cast<std::uint8_t>((blue5 << 3U) | (blue5 >> 2U)),
    };
}

double relative_luminance(ColorRgb888 color);
double contrast_ratio(ColorRgb888 foreground, ColorRgb888 background);
double contrast_ratio_rgb565(ColorRgb888 foreground, ColorRgb888 background);

}  // namespace m3e
