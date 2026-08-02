#include <cassert>
#include <cstddef>
#include <cstdint>

#include "m3e/assets/weather_fonts.hpp"
#include "m3e/generated/weather_icons.hpp"
#include "m3e/generated/weather_tokens.hpp"

int main() {
    using namespace m3e::generated;

    static_assert(kWeatherColors.size() == 30);
    static_assert(kWeatherTypography.size() == 6);
    static_assert(kWeatherShapes.size() == 10);
    static_assert(kWeatherIcons.size() == 33);

    const auto background = kWeatherColors[
        static_cast<std::size_t>(WeatherColorRole::background)];
    assert(background.rgb888.red == 2);
    assert(background.rgb888.green == 8);
    assert(background.rgb888.blue == 23);
    assert(background.rgb565 == 0x0042);

    const auto primary = kWeatherColors[
        static_cast<std::size_t>(WeatherColorRole::primary)];
    assert(primary.rgb565 == 0x9DBF);

    const auto hero = kWeatherTypography[
        static_cast<std::size_t>(WeatherTypographyRole::hero)];
    assert(hero.size_px == 68);
    assert(hero.line_height_px == 73);
    m3e::set_weather_font_scale_milli(1000);
    assert(m3e::weather_font(WeatherTypographyRole::hero)
        == &m3e_weather_hero_68);
    assert(m3e::weather_font(WeatherTypographyRole::micro)
        == &m3e_weather_micro_10);
    m3e::set_weather_font_scale_milli(1300);
    assert(m3e::weather_font(WeatherTypographyRole::hero)
        == &m3e_weather_hero_68);
    assert(m3e::weather_font(WeatherTypographyRole::micro)
        == &m3e_weather_micro_13);
    assert(m3e::weather_font(WeatherTypographyRole::label)
        == &m3e_weather_label_18);
    assert(m3e::weather_font(WeatherTypographyRole::metric)
        == &m3e_weather_metric_36);
    m3e::set_weather_font_scale_milli(1000);

    const auto metric_c = kWeatherShapes[
        static_cast<std::size_t>(WeatherShapeRole::metric_c)];
    assert(metric_c.kind == WeatherShapeKind::cut_corners);
    assert(metric_c.corners_dp[0] == 10);

    const auto bottom_action = kWeatherShapes[
        static_cast<std::size_t>(WeatherShapeRole::bottom_action)];
    assert(bottom_action.kind == WeatherShapeKind::full);
    assert(bottom_action.inset_dp == 6);

    for (std::int8_t code = 0; code <= 15; ++code) {
        const auto& icon = kWeatherIcons[static_cast<std::size_t>(code)];
        assert(icon.condition_code == code);
    }
    const auto rain = kWeatherIcons[
        static_cast<std::size_t>(WeatherIcon::condition_rain)];
    assert(rain.source == WeatherIconSource::meteocons_flat);
    assert(rain.render == WeatherIconRender::multicolor);
    assert(!rain.has_tint);
    assert(rain.asset_stem == "rain");

    const auto refresh = kWeatherIcons[
        static_cast<std::size_t>(WeatherIcon::utility_refresh)];
    assert(refresh.source == WeatherIconSource::material_symbols_rounded);
    assert(refresh.render == WeatherIconRender::mask);
    assert(refresh.has_tint);
    assert(refresh.tint_role == WeatherColorRole::on_surface_variant);
    assert(refresh.asset_stem == "refresh");

    return 0;
}
