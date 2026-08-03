#pragma once

#include <stddef.h>
#include <stdint.h>

#include "m3e/services/exact_scheduler_c.h"

#ifdef __cplusplus
extern "C" {
#endif

// Encodes provider-event-v1 with a timer-state-v1 payload.
size_t m3e_encode_provider_event(
    const char* provider_id,
    const char* event_id,
    uint64_t provider_revision,
    uint8_t freshness,
    uint64_t observed_scenario_ms,
    const uint8_t* payload,
    size_t payload_size,
    uint8_t* output,
    size_t output_capacity);

size_t m3e_encode_timer_provider_event(
    const m3e_schedule_record* record,
    uint64_t provider_revision,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity);

// Encodes provider-event-v1 with a voice-state-v1 payload. Voice kind values
// match the native VoiceEventKind enum (connecting through error).
size_t m3e_encode_voice_provider_event(
    uint8_t kind,
    uint64_t request_id,
    uint32_t elapsed_ms,
    uint32_t encoded_frames,
    uint32_t dropped_frames,
    const char* text,
    uint64_t provider_revision,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity);

size_t m3e_encode_weather_provider_event(
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
    size_t output_capacity);

enum {
    M3E_WEATHER_MAX_HOURS = 7,
    M3E_WEATHER_MAX_DAYS = 4,
    M3E_WEATHER_PRECIPITATION_SAMPLES = 13,
};

typedef struct {
    int32_t temperature_tenths;
    int32_t feels_like_tenths;
    int32_t high_tenths;
    int32_t low_tenths;
    uint16_t wind_speed_tenths;
    uint16_t wind_direction_degrees;
    uint16_t uv_index_tenths;
    uint16_t sunrise_local_minute;
    uint16_t sunset_local_minute;
    uint8_t condition;
    uint8_t precipitation_percent;
    uint8_t humidity_percent;
    uint8_t has_feels_like;
    uint8_t has_high;
    uint8_t has_low;
    uint8_t has_precipitation;
    uint8_t has_humidity;
    uint8_t has_wind_speed;
    uint8_t has_wind_direction;
    uint8_t has_uv_index;
    uint8_t has_sunrise;
    uint8_t has_sunset;
} m3e_weather_current_v2;

typedef struct {
    uint16_t local_minute;
    int32_t temperature_tenths;
    uint8_t precipitation_percent;
    uint8_t condition;
    uint8_t has_precipitation;
} m3e_weather_hour_v2;

typedef struct {
    uint8_t weekday;
    int32_t low_tenths;
    int32_t high_tenths;
    uint8_t precipitation_percent;
    uint8_t condition;
    uint8_t has_precipitation;
} m3e_weather_day_v2;

typedef struct {
    const char* location;
    uint8_t local_weekday;
    uint16_t local_minute;
    m3e_weather_current_v2 current;
    m3e_weather_hour_v2 hours[M3E_WEATHER_MAX_HOURS];
    uint8_t hour_count;
    m3e_weather_day_v2 days[M3E_WEATHER_MAX_DAYS];
    uint8_t day_count;
    uint8_t precipitation[M3E_WEATHER_PRECIPITATION_SAMPLES];
    int16_t minutes_until_rain;
    uint16_t rain_duration_minutes;
    uint8_t units;
    uint64_t data_revision;
    uint64_t cache_age_minutes;
} m3e_weather_snapshot_v2;

// Encodes provider-event-v1 with a canonical weather-snapshot-v2 payload.
// The event id is "weather.snapshot.v2". Returns zero for invalid or
// out-of-range input, overflow, or a payload larger than 512 bytes.
size_t m3e_encode_weather_provider_event_v2(
    const m3e_weather_snapshot_v2* snapshot,
    uint64_t provider_revision,
    uint8_t freshness,
    uint64_t observed_scenario_ms,
    uint8_t* output,
    size_t output_capacity);

#ifdef __cplusplus
}
#endif
