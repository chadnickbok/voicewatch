#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/foundation/color.hpp"
#include "m3e/generated/core_tokens.hpp"

namespace m3e {

struct ResolvedColor {
    ColorRgb888 rgb888;
    ColorRgb565 rgb565;
};

struct ResolvedColorScheme {
    std::array<ResolvedColor, generated::kColorRoleCount> roles;

    constexpr const ResolvedColor& get(generated::ColorRole role) const {
        return roles[static_cast<std::size_t>(role)];
    }
};

struct ThemeMetadata {
    std::uint16_t schema_version;
    std::uint32_t source_crc32;
    const char* theme_id;
    const char* source_artifact;
};

struct ResolvedTheme {
    ThemeMetadata metadata;
    ResolvedColorScheme color;
};

struct ThemeValidation {
    bool metadata_complete;
    bool rgb565_consistent;
    std::uint8_t contrast_failures;

    constexpr bool valid() const {
        return metadata_complete &&
               rgb565_consistent &&
               contrast_failures == 0;
    }
};

ResolvedColorScheme resolve_color_scheme(
    const generated::WearColorScheme& source);
ResolvedTheme baseline_dark_theme();
ResolvedTheme seeded_dark_theme(std::uint32_t seed_rgb);
ThemeValidation validate_resolved_theme(
    const ResolvedTheme& theme,
    double minimum_contrast = 4.5);

}  // namespace m3e
