#pragma once

#include <cstdint>

#include "m3e/services/provider_event_c.h"

// Host-owned result from the optional ESP-IDF Weather adapter.  The Wasm
// guest never receives Wi-Fi credentials, coordinates, HTTP, JSON, or cache
// details; app_runner encodes only `snapshot` into weather.snapshot.v2.
struct WeatherProviderResult {
    char location[49]{};
    m3e_weather_snapshot_v2 snapshot{};
    std::uint8_t freshness = 3;
    char condition[32]{};
    char detail[80]{};
};

// Allocates the bounded completion queue.  With the network backend disabled
// this is a no-op so deterministic conformance firmware retains its current
// behavior and footprint.
bool weather_provider_init();

// Starts one asynchronous refresh.  Returns false while another refresh is
// active or when the worker cannot be started.
bool weather_provider_request();

// Non-blocking completion poll.  On success, `result.snapshot.location`
// points at `result.location` and remains valid until result leaves scope.
bool weather_provider_poll(WeatherProviderResult& result);

bool weather_provider_busy();
