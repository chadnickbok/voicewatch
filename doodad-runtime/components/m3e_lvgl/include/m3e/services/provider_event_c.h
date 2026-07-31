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

#ifdef __cplusplus
}
#endif
