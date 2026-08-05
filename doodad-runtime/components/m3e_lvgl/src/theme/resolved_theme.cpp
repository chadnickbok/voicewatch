#include "m3e/theme/resolved_theme.hpp"

namespace m3e {

namespace {

ColorRgb888 rgb24(std::uint32_t value) {
    return {
        static_cast<std::uint8_t>((value >> 16) & 0xffU),
        static_cast<std::uint8_t>((value >> 8) & 0xffU),
        static_cast<std::uint8_t>(value & 0xffU),
    };
}

ColorRgb888 scale(ColorRgb888 color, std::uint8_t numerator, std::uint8_t denominator) {
    return {
        static_cast<std::uint8_t>(color.red * numerator / denominator),
        static_cast<std::uint8_t>(color.green * numerator / denominator),
        static_cast<std::uint8_t>(color.blue * numerator / denominator),
    };
}

ColorRgb888 accessible_on(ColorRgb888 background) {
    constexpr ColorRgb888 black{0, 0, 0};
    constexpr ColorRgb888 white{255, 255, 255};
    return contrast_ratio_rgb565(black, background) >=
            contrast_ratio_rgb565(white, background)
        ? black
        : white;
}

void set_role(ResolvedTheme& theme, generated::ColorRole role, ColorRgb888 color) {
    theme.color.roles[static_cast<std::size_t>(role)] =
        ResolvedColor{color, quantize_rgb565(color)};
}

}  // namespace

ResolvedColorScheme resolve_color_scheme(
    const generated::WearColorScheme& source) {
    ResolvedColorScheme resolved{};
    for (std::size_t index = 0; index < generated::kColorRoleCount; ++index) {
        const auto role = static_cast<generated::ColorRole>(index);
        const auto color = source.get(role);
        resolved.roles[index] = ResolvedColor{color, quantize_rgb565(color)};
    }
    return resolved;
}

ResolvedTheme baseline_dark_theme() {
    return ResolvedTheme{
        ThemeMetadata{
            1,
            0,
            "wear-material-1.6.2-baseline-dark",
            "androidx.wear.compose:compose-material3:1.6.2",
        },
        resolve_color_scheme(generated::kBaselineDarkColorScheme),
    };
}

ResolvedTheme seeded_dark_theme(std::uint32_t seed_rgb) {
    auto theme = baseline_dark_theme();
    theme.metadata = {1, seed_rgb, "personal-seeded-dark-v1", "signed-manifest.identity.theme_seed"};
    const auto primary = rgb24(seed_rgb);
    const auto primary_container = scale(primary, 1, 2);
    const ColorRgb888 secondary{primary.green, primary.blue, primary.red};
    const auto secondary_container = scale(secondary, 1, 2);
    const ColorRgb888 tertiary{primary.blue, primary.red, primary.green};
    const auto tertiary_container = scale(tertiary, 1, 2);
    using generated::ColorRole;
    set_role(theme, ColorRole::primary, primary);
    set_role(theme, ColorRole::primary_container, primary_container);
    set_role(theme, ColorRole::primary_dim, scale(primary, 3, 4));
    set_role(theme, ColorRole::on_primary, accessible_on(primary));
    set_role(theme, ColorRole::on_primary_container, accessible_on(primary_container));
    set_role(theme, ColorRole::secondary, secondary);
    set_role(theme, ColorRole::secondary_container, secondary_container);
    set_role(theme, ColorRole::secondary_dim, scale(secondary, 3, 4));
    set_role(theme, ColorRole::on_secondary, accessible_on(secondary));
    set_role(theme, ColorRole::on_secondary_container, accessible_on(secondary_container));
    set_role(theme, ColorRole::tertiary, tertiary);
    set_role(theme, ColorRole::tertiary_container, tertiary_container);
    set_role(theme, ColorRole::tertiary_dim, scale(tertiary, 3, 4));
    set_role(theme, ColorRole::on_tertiary, accessible_on(tertiary));
    set_role(theme, ColorRole::on_tertiary_container, accessible_on(tertiary_container));
    return validate_resolved_theme(theme).valid() ? theme : baseline_dark_theme();
}

ThemeValidation validate_resolved_theme(
    const ResolvedTheme& theme,
    double minimum_contrast) {
    ThemeValidation result{
        theme.metadata.schema_version > 0 &&
            theme.metadata.theme_id != nullptr &&
            theme.metadata.theme_id[0] != '\0' &&
            theme.metadata.source_artifact != nullptr,
        true,
        0,
    };
    for (const auto& role : theme.color.roles) {
        if (role.rgb565.value != quantize_rgb565(role.rgb888).value) {
            result.rgb565_consistent = false;
        }
    }
    using generated::ColorRole;
    struct ContrastPair {
        ColorRole foreground;
        ColorRole background;
    };
    constexpr ContrastPair pairs[] = {
        {ColorRole::on_background, ColorRole::background},
        {ColorRole::on_primary, ColorRole::primary},
        {ColorRole::on_primary_container, ColorRole::primary_container},
        {ColorRole::on_secondary, ColorRole::secondary},
        {ColorRole::on_secondary_container, ColorRole::secondary_container},
        {ColorRole::on_tertiary, ColorRole::tertiary},
        {ColorRole::on_tertiary_container, ColorRole::tertiary_container},
        {ColorRole::on_error, ColorRole::error},
        {ColorRole::on_error_container, ColorRole::error_container},
        {ColorRole::on_surface, ColorRole::surface_container_low},
        {ColorRole::on_surface, ColorRole::surface_container},
        {ColorRole::on_surface, ColorRole::surface_container_high},
    };
    for (const auto pair : pairs) {
        if (contrast_ratio_rgb565(
                theme.color.get(pair.foreground).rgb888,
                theme.color.get(pair.background).rgb888) <
            minimum_contrast) {
            ++result.contrast_failures;
        }
    }
    return result;
}

}  // namespace m3e
