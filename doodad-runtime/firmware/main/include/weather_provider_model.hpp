#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace doodad::weather {

constexpr std::uint8_t map_wmo_condition(
    int code,
    bool is_day,
    std::int32_t temperature_tenths,
    bool imperial,
    std::uint16_t wind_speed_tenths) {
    const auto hot_threshold = imperial ? 1000 : 380;
    const auto wind_threshold = imperial ? 400 : 640;
    if (temperature_tenths >= hot_threshold && code <= 3) return 14;
    if (wind_speed_tenths >= wind_threshold && code <= 3) return 13;
    if (code == 0) return is_day ? 0 : 1;
    if (code == 1 || code == 2) return is_day ? 2 : 3;
    if (code == 3) return 5;
    if (code == 45 || code == 48) return 6;
    if (code == 51 || code == 53 || code == 55) return 7;
    if (code == 56 || code == 57 || code == 66 || code == 67) return 12;
    if (code == 61 || code == 63 || code == 80 || code == 81) return 8;
    if (code == 65 || code == 82) return 9;
    if (code == 71 || code == 73 || code == 75 || code == 77 ||
        code == 85 || code == 86) {
        return 11;
    }
    if (code == 95 || code == 96 || code == 99) return 10;
    return 15;
}

// Sunday=0, matching weather.snapshot.v2.
constexpr std::uint8_t weekday(int year, unsigned month, unsigned day) {
    if (month < 3) {
        month += 12;
        --year;
    }
    const auto century = year / 100;
    const auto year_of_century = year % 100;
    const auto h =
        (static_cast<int>(day) +
         (13 * (static_cast<int>(month) + 1)) / 5 +
         year_of_century + year_of_century / 4 + century / 4 +
         5 * century) %
        7;
    return static_cast<std::uint8_t>((h + 6) % 7);
}

inline std::array<std::uint8_t, 13> resample_fifteen_to_five(
    const std::array<std::uint8_t, 5>& input) {
    std::array<std::uint8_t, 13> output{};
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const auto start = static_cast<int>(input[index]);
        const auto delta = static_cast<int>(input[index + 1]) - start;
        for (int fraction = 0; fraction < 3; ++fraction) {
            const auto numerator = delta * fraction;
            const auto adjustment = numerator >= 0 ? 1 : -1;
            output[cursor++] = static_cast<std::uint8_t>(std::clamp(
                start + (numerator + adjustment) / 3,
                0,
                100));
        }
    }
    output[cursor] = input.back();
    return output;
}

struct RainWindow {
    std::int16_t minutes_until_rain = -1;
    std::uint16_t duration_minutes = 0;
};

inline RainWindow rain_window(
    const std::array<std::uint8_t, 13>& probabilities,
    const std::array<float, 5>& amounts) {
    std::array<bool, 13> raining{};
    for (std::size_t index = 0; index < raining.size(); ++index) {
        const auto amount_index = std::min<std::size_t>(index / 3, 4);
        raining[index] =
            amounts[amount_index] > 0.01F || probabilities[index] >= 60;
    }
    std::size_t first = raining.size();
    for (std::size_t index = 0; index < raining.size(); ++index) {
        if (raining[index]) {
            first = index;
            break;
        }
    }
    if (first == raining.size()) return {};
    auto final = first;
    while (final + 1 < raining.size() && raining[final + 1]) ++final;
    return {
        static_cast<std::int16_t>(first * 5),
        static_cast<std::uint16_t>((final - first + 1) * 5),
    };
}

}  // namespace doodad::weather
