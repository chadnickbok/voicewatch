#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/os/shell_state.hpp"

namespace m3e::os {

enum class SurfaceKind : std::uint8_t {
    app = 0,
    glance = 1,
    complication = 2,
    notification = 3,
    ongoing = 4,
    voice = 5,
};

enum class Freshness : std::uint8_t {
    current,
    stale,
    offline,
    error,
};

constexpr std::uint8_t surface_bit(SurfaceKind kind) {
    return static_cast<std::uint8_t>(
        1U << static_cast<std::uint8_t>(kind));
}

struct SurfaceProjection {
    std::uint64_t revision = 0;
    bool active = false;
    std::array<char, 129> primary{};
    std::array<char, 129> secondary{};
    std::array<char, 65> action_id{};
};

struct DomainSurfaceSnapshot {
    std::array<char, 97> app_id{};
    std::uint64_t domain_revision = 0;
    std::uint64_t observed_at_ms = 0;
    Freshness freshness = Freshness::current;
    std::uint8_t declared_mask = 0;
    std::array<SurfaceProjection, 6> projections{};
};

// Trusted atomic registry for all package projections. A publication is
// rejected unless every declared surface carries the same authoritative
// domain revision; the shell therefore never observes a half-updated app.
class SurfaceRegistry {
public:
    static constexpr std::size_t kCapacity = 24;

    [[nodiscard]] bool publish(
        const DomainSurfaceSnapshot& snapshot);
    [[nodiscard]] const DomainSurfaceSnapshot* find(
        const char* app_id) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t active_count(
        SurfaceKind kind) const;
    [[nodiscard]] bool quarantine(const char* app_id);
    [[nodiscard]] bool restore(const char* app_id);
    [[nodiscard]] bool quarantined(const char* app_id) const;
    void sync_shell_counts(ShellState& shell) const;

private:
    [[nodiscard]] static bool valid(
        const DomainSurfaceSnapshot& snapshot);
    [[nodiscard]] DomainSurfaceSnapshot* find_mutable(
        const char* app_id);

    std::array<DomainSurfaceSnapshot, kCapacity> snapshots_{};
    std::size_t size_ = 0;
    std::array<std::array<char, 97>, kCapacity> quarantined_ids_{};
    std::size_t quarantined_count_ = 0;
};

}  // namespace m3e::os
