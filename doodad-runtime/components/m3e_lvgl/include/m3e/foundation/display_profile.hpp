#pragma once

#include <cstdint>

namespace m3e {

using DensityQ8_8 = std::uint16_t;

enum class ScreenShape : std::uint8_t {
    square,
    round,
};

enum class InputKind : std::uint8_t {
    touch,
    crown,
    buttons,
};

struct InsetsDp {
    std::int16_t top;
    std::int16_t right;
    std::int16_t bottom;
    std::int16_t left;
};

struct DisplayProfile {
    const char* id;
    std::uint16_t physical_width_px;
    std::uint16_t physical_height_px;
    std::uint16_t viewport_width_px;
    std::uint16_t viewport_height_px;
    std::int16_t viewport_origin_x_px;
    std::int16_t viewport_origin_y_px;
    std::uint16_t logical_width_dp;
    std::uint16_t logical_height_dp;
    DensityQ8_8 density_q8_8;
    ScreenShape shape;
    InputKind input;
    InsetsDp safe_insets;
    bool supports_edge_button;
    bool supports_curved_text;
};

constexpr DensityQ8_8 density_q8_8(std::uint16_t numerator,
                                   std::uint16_t denominator) {
    return static_cast<DensityQ8_8>(
        (static_cast<std::uint32_t>(numerator) * 256U + denominator / 2U)
        / denominator);
}

std::int32_t dp_edge_to_px(std::int32_t edge_dp,
                           DensityQ8_8 density);
std::int32_t dp_span_to_px(std::int32_t start_dp,
                           std::int32_t length_dp,
                           DensityQ8_8 density);
std::int32_t logical_x_to_physical_px(const DisplayProfile& profile,
                                      std::int32_t x_dp);
std::int32_t logical_y_to_physical_px(const DisplayProfile& profile,
                                      std::int32_t y_dp);
bool profile_is_valid(const DisplayProfile& profile);

extern const DisplayProfile watch_square_192;
extern const DisplayProfile twatch_ultra_portrait;
extern const DisplayProfile cores3_watch_preview;
extern const DisplayProfile wear_round_192_reference;
extern const DisplayProfile wear_large_225_reference;

const DisplayProfile* find_display_profile(const char* id);

}  // namespace m3e
