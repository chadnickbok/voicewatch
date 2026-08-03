#pragma once

#include <cstdint>

// Shared host-owned station connection. Credentials remain native firmware
// configuration and are never exposed to Wasm guests.
bool network_service_init();
bool network_service_connect(std::uint32_t timeout_ms = 20'000);
bool network_service_connected();
void network_service_sync_time(std::uint32_t timeout_ms = 5'000);
