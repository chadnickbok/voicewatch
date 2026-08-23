#include "m3e/foundation/display_profile.hpp"

#include <cstring>

namespace m3e {
namespace {

constexpr DensityQ8_8 kWatchDensity = density_q8_8(5, 4);
constexpr DensityQ8_8 kUltraDensity = density_q8_8(2, 1);
constexpr InsetsDp kNoInsets{0, 0, 0, 0};

std::int32_t round_q8_8(std::int64_t value) {
    if (value >= 0) {
        return static_cast<std::int32_t>((value + 128) / 256);
    }
    return -static_cast<std::int32_t>((-value + 128) / 256);
}

}  // namespace

const DisplayProfile watch_square_192{
    "watch_square_192",
    240,
    240,
    240,
    240,
    0,
    0,
    192,
    192,
    kWatchDensity,
    ScreenShape::square,
    InputKind::touch,
    kNoInsets,
    false,
    false,
};

const DisplayProfile twatch_ultra_portrait{
    "twatch_ultra_410x502",
    410,
    502,
    410,
    502,
    0,
    0,
    205,
    251,
    kUltraDensity,
    ScreenShape::square,
    InputKind::touch,
    kNoInsets,
    true,
    false,
};

const DisplayProfile cores3_watch_preview{
    "cores3_watch_preview",
    320,
    240,
    240,
    240,
    40,
    0,
    192,
    192,
    kWatchDensity,
    ScreenShape::square,
    InputKind::touch,
    kNoInsets,
    false,
    false,
};

const DisplayProfile wear_round_192_reference{
    "wear_round_192_reference",
    240,
    240,
    240,
    240,
    0,
    0,
    192,
    192,
    kWatchDensity,
    ScreenShape::round,
    InputKind::crown,
    kNoInsets,
    true,
    true,
};

const DisplayProfile wear_large_225_reference{
    "wear_large_225_reference",
    281,
    281,
    281,
    281,
    0,
    0,
    225,
    225,
    kWatchDensity,
    ScreenShape::round,
    InputKind::crown,
    kNoInsets,
    true,
    true,
};

std::int32_t dp_edge_to_px(std::int32_t edge_dp,
                           DensityQ8_8 density) {
    return round_q8_8(
        static_cast<std::int64_t>(edge_dp)
        * static_cast<std::int64_t>(density));
}

std::int32_t dp_span_to_px(std::int32_t start_dp,
                           std::int32_t length_dp,
                           DensityQ8_8 density) {
    return dp_edge_to_px(start_dp + length_dp, density)
           - dp_edge_to_px(start_dp, density);
}

std::int32_t logical_x_to_physical_px(const DisplayProfile& profile,
                                      std::int32_t x_dp) {
    return profile.viewport_origin_x_px
           + dp_edge_to_px(x_dp, profile.density_q8_8);
}

std::int32_t logical_y_to_physical_px(const DisplayProfile& profile,
                                      std::int32_t y_dp) {
    return profile.viewport_origin_y_px
           + dp_edge_to_px(y_dp, profile.density_q8_8);
}

bool profile_is_valid(const DisplayProfile& profile) {
    if (profile.id == nullptr || profile.id[0] == '\0'
        || profile.logical_width_dp == 0 || profile.logical_height_dp == 0
        || profile.density_q8_8 == 0 || profile.viewport_width_px == 0
        || profile.viewport_height_px == 0) {
        return false;
    }
    if (profile.viewport_origin_x_px < 0
        || profile.viewport_origin_y_px < 0) {
        return false;
    }
    const auto viewport_right =
        static_cast<std::uint32_t>(profile.viewport_origin_x_px)
        + profile.viewport_width_px;
    const auto viewport_bottom =
        static_cast<std::uint32_t>(profile.viewport_origin_y_px)
        + profile.viewport_height_px;
    if (viewport_right > profile.physical_width_px
        || viewport_bottom > profile.physical_height_px) {
        return false;
    }
    if (dp_edge_to_px(profile.logical_width_dp, profile.density_q8_8)
            != profile.viewport_width_px
        || dp_edge_to_px(profile.logical_height_dp, profile.density_q8_8)
               != profile.viewport_height_px) {
        return false;
    }
    return profile.safe_insets.top >= 0
           && profile.safe_insets.right >= 0
           && profile.safe_insets.bottom >= 0
           && profile.safe_insets.left >= 0;
}

const DisplayProfile* find_display_profile(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    const DisplayProfile* profiles[] = {
        &twatch_ultra_portrait,
        &watch_square_192,
        &cores3_watch_preview,
        &wear_round_192_reference,
        &wear_large_225_reference,
    };
    for (const auto* profile : profiles) {
        if (std::strcmp(id, profile->id) == 0) {
            return profile;
        }
    }
    return nullptr;
}

}  // namespace m3e
