#include <cassert>
#include <cstddef>
#include <cstdint>

#include "m3e/foundation/color.hpp"
#include "m3e/generated/core_tokens.hpp"
#include "m3e/theme/resolved_theme.hpp"

namespace {

using m3e::generated::ColorRole;
using m3e::generated::TypographyRole;

void expect_color(
    ColorRole role,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue) {
    const auto color = m3e::generated::kBaselineDarkColorScheme.get(role);
    assert(color.red == red);
    assert(color.green == green);
    assert(color.blue == blue);
}

void expect_contrast(ColorRole foreground, ColorRole background, double minimum) {
    const auto& scheme = m3e::generated::kBaselineDarkColorScheme;
    const auto ratio = m3e::contrast_ratio_rgb565(
        scheme.get(foreground), scheme.get(background));
    assert(ratio >= minimum);
}

}  // namespace

int main() {
    static_assert(m3e::generated::kColorRoleCount == 29);
    static_assert(m3e::generated::kTypographyRoleCount == 21);
    static_assert(m3e::generated::kShapeTokens.size() == 7);
    static_assert(m3e::generated::kMotionDurationsMs.size() == 16);

    expect_color(ColorRole::background, 0, 0, 0);
    expect_color(ColorRole::primary, 233, 221, 255);
    expect_color(ColorRole::primary_dim, 208, 188, 255);
    expect_color(ColorRole::primary_container, 77, 61, 118);
    expect_color(ColorRole::surface_container_low, 39, 36, 48);
    expect_color(ColorRole::surface_container, 51, 46, 60);
    expect_color(ColorRole::surface_container_high, 73, 68, 83);

    const auto display_large = m3e::generated::kTypographyTokens[
        static_cast<std::size_t>(TypographyRole::display_large)];
    assert(display_large.size_sp_q8_8 == 40 * 256);
    assert(display_large.line_height_sp_q8_8 == 44 * 256);
    assert(display_large.weight == 500);
    assert(display_large.width == 110);

    const auto numeral_large = m3e::generated::kTypographyTokens[
        static_cast<std::size_t>(TypographyRole::numeral_large)];
    assert(numeral_large.size_sp_q8_8 == 50 * 256);
    assert(numeral_large.prominent_weight == 780);

    const auto arc_large = m3e::generated::kTypographyTokens[
        static_cast<std::size_t>(TypographyRole::arc_large)];
    assert(arc_large.is_arc);
    assert(arc_large.tracking_top_sp_q8_8 == 102);
    assert(arc_large.tracking_bottom_sp_q8_8 == 410);

    expect_contrast(ColorRole::on_background, ColorRole::background, 7.0);
    expect_contrast(ColorRole::on_primary, ColorRole::primary, 7.0);
    expect_contrast(
        ColorRole::on_primary_container,
        ColorRole::primary_container,
        7.0);
    expect_contrast(ColorRole::on_secondary, ColorRole::secondary, 7.0);
    expect_contrast(ColorRole::on_tertiary, ColorRole::tertiary, 7.0);
    expect_contrast(ColorRole::on_error, ColorRole::error, 7.0);
    expect_contrast(
        ColorRole::on_surface,
        ColorRole::surface_container_high,
        7.0);

    const auto theme = m3e::baseline_dark_theme();
    assert(theme.metadata.schema_version == 1);
    assert(m3e::validate_resolved_theme(theme, 7.0).valid());
    for (std::size_t index = 0;
         index < m3e::generated::kColorRoleCount;
         ++index) {
        const auto role = static_cast<ColorRole>(index);
        const auto source =
            m3e::generated::kBaselineDarkColorScheme.get(role);
        assert(theme.color.roles[index].rgb565.value
            == m3e::quantize_rgb565(source).value);
    }
    auto invalid_theme = theme;
    invalid_theme.color.roles[
        static_cast<std::size_t>(ColorRole::primary)].rgb565.value = 0;
    assert(!m3e::validate_resolved_theme(invalid_theme).valid());
    return 0;
}
