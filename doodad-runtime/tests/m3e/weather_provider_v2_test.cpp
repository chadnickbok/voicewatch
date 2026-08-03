#include "m3e/services/provider_event_c.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

m3e_weather_snapshot_v2 baseline() {
    m3e_weather_snapshot_v2 snapshot{};
    snapshot.location = "San Francisco";
    snapshot.local_weekday = 6;
    snapshot.local_minute = 609;
    snapshot.current.temperature_tenths = 620;
    snapshot.current.feels_like_tenths = 590;
    snapshot.current.high_tenths = 670;
    snapshot.current.low_tenths = 540;
    snapshot.current.condition = 2;
    snapshot.current.precipitation_percent = 0;
    snapshot.current.humidity_percent = 49;
    snapshot.current.wind_speed_tenths = 80;
    snapshot.current.wind_direction_degrees = 270;
    snapshot.current.uv_index_tenths = 30;
    snapshot.current.sunrise_local_minute = 372;
    snapshot.current.sunset_local_minute = 1205;
    snapshot.current.has_feels_like = 1;
    snapshot.current.has_high = 1;
    snapshot.current.has_low = 1;
    snapshot.current.has_precipitation = 1;
    snapshot.current.has_humidity = 1;
    snapshot.current.has_wind_speed = 1;
    snapshot.current.has_wind_direction = 1;
    snapshot.current.has_uv_index = 1;
    snapshot.current.has_sunrise = 1;
    snapshot.current.has_sunset = 1;
    snapshot.hour_count = M3E_WEATHER_MAX_HOURS;
    for (std::size_t index = 0; index < snapshot.hour_count; ++index) {
        snapshot.hours[index].local_minute =
            static_cast<std::uint16_t>(609 + index * 60);
        snapshot.hours[index].temperature_tenths =
            static_cast<std::int32_t>(620 + index * 10);
        snapshot.hours[index].precipitation_percent = 0;
        snapshot.hours[index].condition = index < 2 ? 2 : 0;
        snapshot.hours[index].has_precipitation = 1;
    }
    snapshot.day_count = M3E_WEATHER_MAX_DAYS;
    for (std::size_t index = 0; index < snapshot.day_count; ++index) {
        snapshot.days[index].weekday =
            static_cast<std::uint8_t>((6 + index) % 7);
        snapshot.days[index].low_tenths = 540;
        snapshot.days[index].high_tenths = 670;
        snapshot.days[index].precipitation_percent = 0;
        snapshot.days[index].condition = 2;
        snapshot.days[index].has_precipitation = 1;
    }
    snapshot.minutes_until_rain = -1;
    snapshot.rain_duration_minutes = 0;
    snapshot.units = 1;
    snapshot.data_revision = 42;
    snapshot.cache_age_minutes = 0;
    return snapshot;
}

}  // namespace

int main() {
    auto snapshot = baseline();
    std::array<std::uint8_t, 768> first{};
    std::array<std::uint8_t, 768> second{};
    const auto first_size = m3e_encode_weather_provider_event_v2(
        &snapshot, 7, 0, 1234, first.data(), first.size());
    const auto second_size = m3e_encode_weather_provider_event_v2(
        &snapshot, 7, 0, 1234, second.data(), second.size());
    assert(first_size > 0 && first_size <= first.size());
    assert(first_size == second_size);
    assert(std::memcmp(first.data(), second.data(), first_size) == 0);

    const char event_id[] = "weather.snapshot.v2";
    bool found_event_id = false;
    for (std::size_t index = 0;
         index + sizeof(event_id) - 1 <= first_size;
         ++index) {
        if (std::memcmp(first.data() + index, event_id,
                        sizeof(event_id) - 1) == 0) {
            found_event_id = true;
            break;
        }
    }
    assert(found_event_id);

    auto invalid = snapshot;
    invalid.current.humidity_percent = 101;
    assert(m3e_encode_weather_provider_event_v2(
               &invalid, 8, 0, 1234, first.data(), first.size()) == 0);
    invalid = snapshot;
    invalid.hour_count = M3E_WEATHER_MAX_HOURS + 1;
    assert(m3e_encode_weather_provider_event_v2(
               &invalid, 8, 0, 1234, first.data(), first.size()) == 0);

    return 0;
}
