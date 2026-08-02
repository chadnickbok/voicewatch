#include <array>
#include <cassert>

#include "weather_provider_model.hpp"

int main() {
    using doodad::weather::map_wmo_condition;
    assert(map_wmo_condition(0, true, 200, false, 10) == 0);
    assert(map_wmo_condition(0, false, 200, false, 10) == 1);
    assert(map_wmo_condition(2, true, 200, false, 10) == 2);
    assert(map_wmo_condition(2, false, 200, false, 10) == 3);
    assert(map_wmo_condition(3, true, 200, false, 10) == 5);
    assert(map_wmo_condition(48, true, 200, false, 10) == 6);
    assert(map_wmo_condition(53, true, 200, false, 10) == 7);
    assert(map_wmo_condition(63, true, 200, false, 10) == 8);
    assert(map_wmo_condition(82, true, 200, false, 10) == 9);
    assert(map_wmo_condition(95, true, 200, false, 10) == 10);
    assert(map_wmo_condition(75, true, 200, false, 10) == 11);
    assert(map_wmo_condition(67, true, 200, false, 10) == 12);
    assert(map_wmo_condition(0, true, 380, false, 10) == 14);
    assert(map_wmo_condition(0, true, 1000, true, 10) == 14);
    assert(map_wmo_condition(0, true, 200, false, 640) == 13);
    assert(map_wmo_condition(0, true, 700, true, 400) == 13);
    assert(map_wmo_condition(42, true, 200, false, 10) == 15);

    assert(doodad::weather::weekday(2026, 8, 1) == 6);
    assert(doodad::weather::weekday(2024, 2, 29) == 4);
    assert(doodad::weather::weekday(2025, 1, 5) == 0);

    const auto precipitation = doodad::weather::resample_fifteen_to_five(
        std::array<std::uint8_t, 5>{0, 30, 60, 30, 0});
    constexpr std::array<std::uint8_t, 13> expected{
        0, 10, 20, 30, 40, 50, 60, 50, 40, 30, 20, 10, 0};
    assert(precipitation == expected);

    auto window = doodad::weather::rain_window(
        precipitation,
        std::array<float, 5>{0.0F, 0.0F, 0.02F, 0.02F, 0.0F});
    assert(window.minutes_until_rain == 30);
    assert(window.duration_minutes == 30);

    window = doodad::weather::rain_window(
        std::array<std::uint8_t, 13>{},
        std::array<float, 5>{});
    assert(window.minutes_until_rain == -1);
    assert(window.duration_minutes == 0);
    return 0;
}
