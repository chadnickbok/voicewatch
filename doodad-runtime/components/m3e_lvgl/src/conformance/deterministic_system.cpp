#include "m3e/conformance/deterministic_system.hpp"

#include <limits>

namespace m3e::conformance {
namespace {

constexpr std::int16_t kMaximumTimezoneOffsetMinutes = 14 * 60;

bool can_add_unsigned(std::uint64_t value, std::uint64_t increment) {
    return increment <= std::numeric_limits<std::uint64_t>::max() - value;
}

bool can_add_wall_time(std::int64_t value, std::uint64_t increment) {
    if (increment >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    if (value < 0) {
        return true;
    }
    return static_cast<std::int64_t>(increment) <=
           std::numeric_limits<std::int64_t>::max() - value;
}

}  // namespace

DeterministicSystem::DeterministicSystem(
    std::int64_t wall_time_ms,
    std::int16_t timezone_offset_minutes)
    : snapshot_{
          0,
          0,
          wall_time_ms,
          0,
          1,
          AppState::stopped,
          DisplayState::awake,
          ConnectivityState::online,
      } {
    if (timezone_offset_minutes >= -kMaximumTimezoneOffsetMinutes &&
        timezone_offset_minutes <= kMaximumTimezoneOffsetMinutes) {
        snapshot_.timezone_offset_minutes = timezone_offset_minutes;
    }
}

const SystemSnapshot& DeterministicSystem::snapshot() const {
    return snapshot_;
}

bool DeterministicSystem::can_advance(
    std::uint64_t milliseconds,
    bool include_uptime) const {
    return can_add_unsigned(snapshot_.scenario_ms, milliseconds) &&
           (!include_uptime ||
            can_add_unsigned(snapshot_.uptime_ms, milliseconds)) &&
           can_add_wall_time(snapshot_.wall_time_ms, milliseconds);
}

bool DeterministicSystem::advance(std::uint64_t milliseconds) {
    if (!can_advance(milliseconds, true)) {
        return false;
    }
    snapshot_.scenario_ms += milliseconds;
    snapshot_.uptime_ms += milliseconds;
    snapshot_.wall_time_ms += static_cast<std::int64_t>(milliseconds);
    return true;
}

void DeterministicSystem::set_wall_time(std::int64_t wall_time_ms) {
    snapshot_.wall_time_ms = wall_time_ms;
}

bool DeterministicSystem::set_timezone_offset(std::int16_t minutes) {
    if (minutes < -kMaximumTimezoneOffsetMinutes ||
        minutes > kMaximumTimezoneOffsetMinutes) {
        return false;
    }
    snapshot_.timezone_offset_minutes = minutes;
    return true;
}

bool DeterministicSystem::can_transition(AppState from, AppState to) {
    if (from == to) {
        return true;
    }
    switch (from) {
        case AppState::stopped:
            return to == AppState::foreground;
        case AppState::foreground:
            return to == AppState::background ||
                   to == AppState::suspended ||
                   to == AppState::stopped ||
                   to == AppState::crashed;
        case AppState::background:
            return to == AppState::foreground ||
                   to == AppState::suspended ||
                   to == AppState::stopped ||
                   to == AppState::crashed;
        case AppState::suspended:
            return to == AppState::foreground ||
                   to == AppState::background ||
                   to == AppState::stopped ||
                   to == AppState::crashed;
        case AppState::crashed:
            return to == AppState::stopped;
    }
    return false;
}

bool DeterministicSystem::set_app_state(AppState state) {
    if (!can_transition(snapshot_.app_state, state)) {
        return false;
    }
    snapshot_.app_state = state;
    return true;
}

void DeterministicSystem::set_display_state(DisplayState state) {
    snapshot_.display_state = state;
}

void DeterministicSystem::set_connectivity(ConnectivityState state) {
    snapshot_.connectivity = state;
}

bool DeterministicSystem::reboot(std::uint64_t downtime_ms) {
    if (snapshot_.boot_generation ==
            std::numeric_limits<std::uint32_t>::max() ||
        !can_advance(downtime_ms, false)) {
        return false;
    }
    snapshot_.scenario_ms += downtime_ms;
    snapshot_.wall_time_ms += static_cast<std::int64_t>(downtime_ms);
    snapshot_.uptime_ms = 0;
    ++snapshot_.boot_generation;
    snapshot_.app_state = AppState::stopped;
    snapshot_.display_state = DisplayState::asleep;
    snapshot_.connectivity = ConnectivityState::offline;
    return true;
}

}  // namespace m3e::conformance
