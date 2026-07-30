#include "m3e/foundation/color.hpp"

#include <cmath>

namespace m3e {
namespace {

double linear_component(std::uint8_t component) {
    const auto srgb = static_cast<double>(component) / 255.0;
    if (srgb <= 0.04045) {
        return srgb / 12.92;
    }
    return std::pow((srgb + 0.055) / 1.055, 2.4);
}

}  // namespace

double relative_luminance(ColorRgb888 color) {
    return 0.2126 * linear_component(color.red)
        + 0.7152 * linear_component(color.green)
        + 0.0722 * linear_component(color.blue);
}

double contrast_ratio(ColorRgb888 foreground, ColorRgb888 background) {
    const auto foreground_luminance = relative_luminance(foreground);
    const auto background_luminance = relative_luminance(background);
    const auto lighter =
        foreground_luminance > background_luminance
        ? foreground_luminance
        : background_luminance;
    const auto darker =
        foreground_luminance > background_luminance
        ? background_luminance
        : foreground_luminance;
    return (lighter + 0.05) / (darker + 0.05);
}

double contrast_ratio_rgb565(
    ColorRgb888 foreground, ColorRgb888 background) {
    return contrast_ratio(
        expand_rgb565(quantize_rgb565(foreground)),
        expand_rgb565(quantize_rgb565(background)));
}

}  // namespace m3e
