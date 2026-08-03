#pragma once

#include <cstdint>

#include "lvgl.h"

#include "m3e/generated/weather_tokens.hpp"

LV_FONT_DECLARE(m3e_weather_micro_10);
LV_FONT_DECLARE(m3e_weather_label_14);
LV_FONT_DECLARE(m3e_weather_row_18);
LV_FONT_DECLARE(m3e_weather_metric_28);
LV_FONT_DECLARE(m3e_weather_headline_32);
LV_FONT_DECLARE(m3e_weather_hero_68);
LV_FONT_DECLARE(m3e_weather_micro_13);
LV_FONT_DECLARE(m3e_weather_label_18);
LV_FONT_DECLARE(m3e_weather_row_23);
LV_FONT_DECLARE(m3e_weather_metric_36);
LV_FONT_DECLARE(m3e_weather_headline_42);

namespace m3e {

void set_weather_font_scale_milli(std::uint16_t scale_milli);
std::uint16_t weather_font_scale_milli();

inline const lv_font_t* weather_font(
    generated::WeatherTypographyRole role) {
    using generated::WeatherTypographyRole;
    if (weather_font_scale_milli() == 1300) {
        switch (role) {
            case WeatherTypographyRole::micro: return &m3e_weather_micro_13;
            case WeatherTypographyRole::label: return &m3e_weather_label_18;
            case WeatherTypographyRole::row: return &m3e_weather_row_23;
            case WeatherTypographyRole::metric: return &m3e_weather_metric_36;
            case WeatherTypographyRole::headline: return &m3e_weather_headline_42;
            case WeatherTypographyRole::hero: return &m3e_weather_hero_68;
            case WeatherTypographyRole::count: break;
        }
    }
    switch (role) {
        case WeatherTypographyRole::micro: return &m3e_weather_micro_10;
        case WeatherTypographyRole::label: return &m3e_weather_label_14;
        case WeatherTypographyRole::row: return &m3e_weather_row_18;
        case WeatherTypographyRole::metric: return &m3e_weather_metric_28;
        case WeatherTypographyRole::headline: return &m3e_weather_headline_32;
        case WeatherTypographyRole::hero: return &m3e_weather_hero_68;
        case WeatherTypographyRole::count: break;
    }
    return &m3e_weather_label_14;
}

}  // namespace m3e
