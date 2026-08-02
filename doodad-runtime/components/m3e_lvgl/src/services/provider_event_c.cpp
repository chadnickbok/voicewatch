#include "m3e/services/provider_event_c.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

class Writer {
public:
    Writer(std::uint8_t* bytes, std::size_t capacity)
        : bytes_(bytes), capacity_(capacity) {}

    bool unsigned_integer(std::uint8_t major, std::uint64_t value) {
        if (value < 24) {
            return byte(static_cast<std::uint8_t>((major << 5U) | value));
        }
        if (value <= UINT8_MAX) {
            return byte(static_cast<std::uint8_t>((major << 5U) | 24U)) &&
                   byte(static_cast<std::uint8_t>(value));
        }
        if (value <= UINT16_MAX) {
            return byte(static_cast<std::uint8_t>((major << 5U) | 25U)) &&
                   byte(static_cast<std::uint8_t>(value >> 8U)) &&
                   byte(static_cast<std::uint8_t>(value));
        }
        if (value <= UINT32_MAX) {
            if (!byte(static_cast<std::uint8_t>((major << 5U) | 26U))) {
                return false;
            }
            for (int shift = 24; shift >= 0; shift -= 8) {
                if (!byte(static_cast<std::uint8_t>(value >> shift))) {
                    return false;
                }
            }
            return true;
        }
        if (!byte(static_cast<std::uint8_t>((major << 5U) | 27U))) {
            return false;
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            if (!byte(static_cast<std::uint8_t>(value >> shift))) {
                return false;
            }
        }
        return true;
    }

    bool text(const char* value) {
        if (value == nullptr) return false;
        const auto length = std::strlen(value);
        return unsigned_integer(3, length) &&
               copy(
                   reinterpret_cast<const std::uint8_t*>(value),
                   length);
    }

    bool signed_integer(std::int64_t value) {
        if (value >= 0) {
            return unsigned_integer(0, static_cast<std::uint64_t>(value));
        }
        return unsigned_integer(
            1, static_cast<std::uint64_t>(-1 - value));
    }

    bool array(std::size_t length) {
        return unsigned_integer(4, length);
    }

    bool null_value() {
        return unsigned_integer(7, 22);
    }

    bool bytes(const std::uint8_t* value, std::size_t length) {
        return value != nullptr &&
               unsigned_integer(2, length) &&
               copy(value, length);
    }

    std::size_t size() const { return length_; }

private:
    bool byte(std::uint8_t value) {
        if (bytes_ == nullptr || length_ >= capacity_) return false;
        bytes_[length_++] = value;
        return true;
    }

    bool copy(const std::uint8_t* value, std::size_t length) {
        if (value == nullptr || length > capacity_ - length_) {
            return false;
        }
        std::memcpy(bytes_ + length_, value, length);
        length_ += length;
        return true;
    }

    std::uint8_t* bytes_;
    std::size_t capacity_;
    std::size_t length_ = 0;
};

std::size_t encode_timer_payload(
    const m3e_schedule_record& record,
    std::uint64_t observed_scenario_ms,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (record.state < 1 || record.state > 4) return 0;
    const auto state = static_cast<std::uint8_t>(record.state - 1);
    const auto remaining =
        record.state == 1 &&
                record.deadline_scenario_ms > observed_scenario_ms
            ? record.deadline_scenario_ms - observed_scenario_ms
            : 0;
    Writer writer(output, output_capacity);
    if (!writer.unsigned_integer(5, 5) ||
        !writer.unsigned_integer(0, 0) ||
        !writer.text(record.id) ||
        !writer.unsigned_integer(0, 1) ||
        !writer.unsigned_integer(0, state) ||
        !writer.unsigned_integer(0, 2) ||
        !writer.unsigned_integer(0, remaining) ||
        !writer.unsigned_integer(0, 3) ||
        !writer.unsigned_integer(0, record.deadline_scenario_ms) ||
        !writer.unsigned_integer(0, 4) ||
        !writer.unsigned_integer(0, record.fire_count)) {
        return 0;
    }
    return writer.size();
}

bool optional_signed(
    Writer& writer,
    bool present,
    std::int64_t value) {
    return present ? writer.signed_integer(value) : writer.null_value();
}

bool optional_unsigned(
    Writer& writer,
    bool present,
    std::uint64_t value) {
    return present ? writer.unsigned_integer(0, value) : writer.null_value();
}

bool valid_flag(std::uint8_t value) {
    return value <= 1;
}

bool valid_weather_snapshot(const m3e_weather_snapshot_v2& snapshot) {
    if (snapshot.location == nullptr) return false;
    const auto location_length = std::strlen(snapshot.location);
    if (location_length == 0 || location_length > 48 ||
        snapshot.local_weekday > 6 || snapshot.local_minute > 1439 ||
        snapshot.current.condition > 15 ||
        snapshot.hour_count == 0 ||
        snapshot.hour_count > M3E_WEATHER_MAX_HOURS ||
        snapshot.day_count == 0 ||
        snapshot.day_count > M3E_WEATHER_MAX_DAYS ||
        snapshot.minutes_until_rain < -1 ||
        snapshot.minutes_until_rain > 1439 ||
        snapshot.rain_duration_minutes > 1440 ||
        snapshot.units > 1) {
        return false;
    }
    const auto& current = snapshot.current;
    const std::uint8_t flags[] = {
        current.has_feels_like,
        current.has_high,
        current.has_low,
        current.has_precipitation,
        current.has_humidity,
        current.has_wind_speed,
        current.has_wind_direction,
        current.has_uv_index,
        current.has_sunrise,
        current.has_sunset,
    };
    for (const auto flag : flags) {
        if (!valid_flag(flag)) return false;
    }
    if ((current.has_precipitation &&
         current.precipitation_percent > 100) ||
        (current.has_humidity && current.humidity_percent > 100) ||
        (current.has_wind_direction &&
         current.wind_direction_degrees > 359) ||
        (current.has_sunrise && current.sunrise_local_minute > 1439) ||
        (current.has_sunset && current.sunset_local_minute > 1439)) {
        return false;
    }
    for (std::size_t index = 0; index < snapshot.hour_count; ++index) {
        const auto& hour = snapshot.hours[index];
        if (hour.local_minute > 1439 || hour.condition > 15 ||
            !valid_flag(hour.has_precipitation) ||
            (hour.has_precipitation && hour.precipitation_percent > 100)) {
            return false;
        }
    }
    for (std::size_t index = 0; index < snapshot.day_count; ++index) {
        const auto& day = snapshot.days[index];
        if (day.weekday > 6 || day.condition > 15 ||
            !valid_flag(day.has_precipitation) ||
            (day.has_precipitation && day.precipitation_percent > 100)) {
            return false;
        }
    }
    for (const auto sample : snapshot.precipitation) {
        if (sample > 100) return false;
    }
    return true;
}

std::size_t encode_weather_payload_v2(
    const m3e_weather_snapshot_v2& snapshot,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (!valid_weather_snapshot(snapshot)) return 0;
    Writer writer(output, output_capacity);
    const auto& current = snapshot.current;
    if (!writer.unsigned_integer(5, 13) ||
        !writer.unsigned_integer(0, 0) ||
        !writer.unsigned_integer(0, 2) ||
        !writer.unsigned_integer(0, 1) ||
        !writer.text(snapshot.location) ||
        !writer.unsigned_integer(0, 2) ||
        !writer.unsigned_integer(0, snapshot.local_weekday) ||
        !writer.unsigned_integer(0, 3) ||
        !writer.unsigned_integer(0, snapshot.local_minute) ||
        !writer.unsigned_integer(0, 4) ||
        !writer.array(12) ||
        !writer.signed_integer(current.temperature_tenths) ||
        !optional_signed(writer, current.has_feels_like, current.feels_like_tenths) ||
        !writer.unsigned_integer(0, current.condition) ||
        !optional_signed(writer, current.has_high, current.high_tenths) ||
        !optional_signed(writer, current.has_low, current.low_tenths) ||
        !optional_unsigned(writer, current.has_precipitation,
                           current.precipitation_percent) ||
        !optional_unsigned(writer, current.has_humidity,
                           current.humidity_percent) ||
        !optional_unsigned(writer, current.has_wind_speed,
                           current.wind_speed_tenths) ||
        !optional_unsigned(writer, current.has_wind_direction,
                           current.wind_direction_degrees) ||
        !optional_unsigned(writer, current.has_uv_index,
                           current.uv_index_tenths) ||
        !optional_unsigned(writer, current.has_sunrise,
                           current.sunrise_local_minute) ||
        !optional_unsigned(writer, current.has_sunset,
                           current.sunset_local_minute) ||
        !writer.unsigned_integer(0, 5) ||
        !writer.array(snapshot.hour_count)) {
        return 0;
    }
    for (std::size_t index = 0; index < snapshot.hour_count; ++index) {
        const auto& hour = snapshot.hours[index];
        if (!writer.array(4) ||
            !writer.unsigned_integer(0, hour.local_minute) ||
            !writer.signed_integer(hour.temperature_tenths) ||
            !optional_unsigned(writer, hour.has_precipitation,
                               hour.precipitation_percent) ||
            !writer.unsigned_integer(0, hour.condition)) {
            return 0;
        }
    }
    if (!writer.unsigned_integer(0, 6) ||
        !writer.array(snapshot.day_count)) {
        return 0;
    }
    for (std::size_t index = 0; index < snapshot.day_count; ++index) {
        const auto& day = snapshot.days[index];
        if (!writer.array(5) ||
            !writer.unsigned_integer(0, day.weekday) ||
            !writer.signed_integer(day.low_tenths) ||
            !writer.signed_integer(day.high_tenths) ||
            !optional_unsigned(writer, day.has_precipitation,
                               day.precipitation_percent) ||
            !writer.unsigned_integer(0, day.condition)) {
            return 0;
        }
    }
    if (!writer.unsigned_integer(0, 7) ||
        !writer.array(M3E_WEATHER_PRECIPITATION_SAMPLES)) {
        return 0;
    }
    for (const auto sample : snapshot.precipitation) {
        if (!writer.unsigned_integer(0, sample)) return 0;
    }
    if (!writer.unsigned_integer(0, 8) ||
        !writer.signed_integer(snapshot.minutes_until_rain) ||
        !writer.unsigned_integer(0, 9) ||
        !writer.unsigned_integer(0, snapshot.rain_duration_minutes) ||
        !writer.unsigned_integer(0, 10) ||
        !writer.unsigned_integer(0, snapshot.units) ||
        !writer.unsigned_integer(0, 11) ||
        !writer.unsigned_integer(0, snapshot.data_revision) ||
        !writer.unsigned_integer(0, 12) ||
        !writer.unsigned_integer(0, snapshot.cache_age_minutes)) {
        return 0;
    }
    return writer.size();
}

}  // namespace

extern "C" size_t m3e_encode_timer_provider_event(
    const m3e_schedule_record* record,
    uint64_t provider_revision,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity) {
    if (record == nullptr || provider_revision == 0 ||
        output == nullptr) {
        return 0;
    }
    std::uint8_t payload[160]{};
    const auto payload_size = encode_timer_payload(
        *record,
        observed_scenario_ms,
        payload,
        sizeof(payload));
    if (payload_size == 0) return 0;

    return m3e_encode_provider_event(
        "exact_scheduler",
        "timer.changed",
        provider_revision,
        0,
        observed_scenario_ms,
        payload,
        payload_size,
        output,
        output_capacity);
}

extern "C" size_t m3e_encode_provider_event(
    const char* provider_id,
    const char* event_id,
    uint64_t provider_revision,
    uint8_t freshness,
    uint64_t observed_scenario_ms,
    const uint8_t* payload,
    size_t payload_size,
    uint8_t* output,
    size_t output_capacity) {
    if (provider_id == nullptr || event_id == nullptr ||
        provider_revision == 0 || freshness > 3 ||
        payload == nullptr || payload_size > 512 ||
        output == nullptr) {
        return 0;
    }
    Writer writer(output, output_capacity);
    if (!writer.unsigned_integer(5, 7) ||
        !writer.unsigned_integer(0, 0) ||
        !writer.unsigned_integer(0, 1) ||
        !writer.unsigned_integer(0, 1) ||
        !writer.text(provider_id) ||
        !writer.unsigned_integer(0, 2) ||
        !writer.text(event_id) ||
        !writer.unsigned_integer(0, 3) ||
        !writer.unsigned_integer(0, provider_revision) ||
        !writer.unsigned_integer(0, 4) ||
        !writer.unsigned_integer(0, freshness) ||
        !writer.unsigned_integer(0, 5) ||
        !writer.unsigned_integer(0, observed_scenario_ms) ||
        !writer.unsigned_integer(0, 6) ||
        !writer.bytes(payload, payload_size)) {
        return 0;
    }
    return writer.size();
}

extern "C" size_t m3e_encode_weather_provider_event(
    int32_t temperature_tenths_f,
    const char* condition,
    const char* detail,
    const char* location,
    uint64_t data_revision,
    uint64_t cache_age_minutes,
    uint64_t provider_revision,
    uint8_t freshness,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity) {
    std::uint8_t payload[256]{};
    Writer writer(payload, sizeof(payload));
    if (!writer.unsigned_integer(5, 6) ||
        !writer.unsigned_integer(0, 0) ||
        !writer.signed_integer(temperature_tenths_f) ||
        !writer.unsigned_integer(0, 1) ||
        !writer.text(condition) ||
        !writer.unsigned_integer(0, 2) ||
        !writer.text(detail) ||
        !writer.unsigned_integer(0, 3) ||
        !writer.text(location) ||
        !writer.unsigned_integer(0, 4) ||
        !writer.unsigned_integer(0, data_revision) ||
        !writer.unsigned_integer(0, 5) ||
        !writer.unsigned_integer(0, cache_age_minutes)) {
        return 0;
    }
    return m3e_encode_provider_event(
        "weather",
        "weather.snapshot",
        provider_revision,
        freshness,
        observed_scenario_ms,
        payload,
        writer.size(),
        output,
        output_capacity);
}

extern "C" size_t m3e_encode_weather_provider_event_v2(
    const m3e_weather_snapshot_v2* snapshot,
    uint64_t provider_revision,
    uint8_t freshness,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity) {
    if (snapshot == nullptr || provider_revision == 0 ||
        freshness > 3 || output == nullptr) {
        return 0;
    }
    std::uint8_t payload[512]{};
    const auto payload_size = encode_weather_payload_v2(
        *snapshot, payload, sizeof(payload));
    if (payload_size == 0 || payload_size > sizeof(payload)) return 0;
    return m3e_encode_provider_event(
        "weather",
        "weather.snapshot.v2",
        provider_revision,
        freshness,
        observed_scenario_ms,
        payload,
        payload_size,
        output,
        output_capacity);
}
