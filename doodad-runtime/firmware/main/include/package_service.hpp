#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "personal_bundle.hpp"

namespace doodad::packages {

enum class InstallPhase : std::uint8_t {
    none = 0,
    downloading = 1,
    installing = 2,
    ready = 3,
    failed = 4,
};

struct AppReadyOffer {
    std::array<char, 257> url{};
    std::array<char, (kSha256Bytes * 2) + 1> bundle_sha256{};
    std::uint32_t bundle_bytes = 0;
};

struct CatalogEntry {
    std::array<char, kMaximumPersonalAppIdBytes + 1> app_id{};
    std::array<char, kMaximumPersonalAppNameBytes + 1> name{};
    std::array<char, kMaximumPersonalAppVersionBytes + 1> semantic_version{};
    std::array<char, kMaximumPersonalIconBytes + 1> icon{};
    std::array<char, kThemeSeedBytes + 1> theme_seed{};
    std::array<char, (kSha256Bytes * 2) + 1> payload_sha256{};
    bool has_previous = false;
    bool rollback_available = false;
};

struct CatalogSnapshot {
    static constexpr std::size_t kCapacity = 32;
    std::array<CatalogEntry, kCapacity> apps{};
    std::size_t count = 0;
    std::uint32_t revision = 0;
};

struct LaunchRequest {
    std::array<char, kMaximumPersonalAppIdBytes + 1> app_id{};
    std::array<char, kMaximumPersonalAppNameBytes + 1> name{};
    std::array<char, kMaximumPersonalAppVersionBytes + 1> semantic_version{};
    std::array<char, kMaximumPersonalIconBytes + 1> icon{};
    std::array<char, kThemeSeedBytes + 1> theme_seed{};
    std::array<char, (kSha256Bytes * 2) + 1> payload_sha256{};
};

// Mounts the onboard package filesystem, restores the owner-bound registry,
// and starts the non-blocking HTTP installer task. A failure leaves the native
// shell and embedded recovery guest usable.
bool package_service_init();
bool package_service_mounted();

// Safe from the WebSocket callback: validates bounded fields and copies one
// offer onto the installer queue without performing network or filesystem IO.
bool package_service_offer(const AppReadyOffer& offer);

bool package_service_catalog(CatalogSnapshot& snapshot);
bool package_service_request_launch(
    const char* app_id,
    const char* semantic_version,
    const char* payload_sha256);
bool package_service_poll_launch(LaunchRequest& request);

// Runtime-thread operations. Loading copies a verified, installed Wasm image
// into caller-owned memory; no filesystem pointer escapes the service lock.
bool package_service_load(
    const LaunchRequest& request,
    std::vector<std::uint8_t>& storage);
bool package_service_mark_running(const LaunchRequest& request);

// If `failed` still names the installed current generation, atomically moves
// the registry back once and returns the generation that should be reloaded.
bool package_service_rollback(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    LaunchRequest& previous);

// Handles a failure attributed to a resident generation that is no longer
// registry current. Persists exact-tuple quarantine without swapping slots and
// returns the distinct, launchable current generation. Persistence failure is
// fail-closed and returns no target.
bool package_service_recover_current(
    const char* app_id,
    const char* failed_semantic_version,
    const char* failed_payload_sha256,
    LaunchRequest& current);

}  // namespace doodad::packages
