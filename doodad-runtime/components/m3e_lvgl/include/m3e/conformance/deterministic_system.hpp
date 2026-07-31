#pragma once

#include <cstdint>

namespace m3e::conformance {

enum class AppState : std::uint8_t {
    stopped,
    foreground,
    background,
    suspended,
    crashed,
};

enum class DisplayState : std::uint8_t {
    awake,
    asleep,
};

enum class ConnectivityState : std::uint8_t {
    online,
    degraded,
    offline,
};

struct SystemSnapshot {
    std::uint64_t scenario_ms;
    std::uint64_t uptime_ms;
    std::int64_t wall_time_ms;
    std::int16_t timezone_offset_minutes;
    std::uint32_t boot_generation;
    AppState app_state;
    DisplayState display_state;
    ConnectivityState connectivity;
};

class DeterministicSystem {
public:
    explicit DeterministicSystem(
        std::int64_t wall_time_ms = 0,
        std::int16_t timezone_offset_minutes = 0);

    [[nodiscard]] const SystemSnapshot& snapshot() const;

    // Advances scenario, boot-uptime, and wall clocks together.
    [[nodiscard]] bool advance(std::uint64_t milliseconds);

    // Changes civil time without disturbing monotonic scenario or uptime clocks.
    void set_wall_time(std::int64_t wall_time_ms);
    [[nodiscard]] bool set_timezone_offset(std::int16_t minutes);

    [[nodiscard]] bool set_app_state(AppState state);
    void set_display_state(DisplayState state);
    void set_connectivity(ConnectivityState state);

    // Advances scenario and wall time by the simulated downtime, then starts a
    // new boot with zero uptime and conservative lifecycle defaults.
    [[nodiscard]] bool reboot(std::uint64_t downtime_ms);

private:
    [[nodiscard]] static bool can_transition(
        AppState from,
        AppState to);
    [[nodiscard]] bool can_advance(
        std::uint64_t milliseconds,
        bool include_uptime) const;

    SystemSnapshot snapshot_;
};

}  // namespace m3e::conformance
