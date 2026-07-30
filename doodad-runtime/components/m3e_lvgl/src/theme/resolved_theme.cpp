#include "m3e/theme/resolved_theme.hpp"

namespace m3e {

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
