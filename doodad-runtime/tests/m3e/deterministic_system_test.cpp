#include <cassert>
#include <cstdint>
#include <limits>

#include "m3e/conformance/deterministic_system.hpp"

int main() {
    using namespace m3e::conformance;

    DeterministicSystem system(1'700'000'000'000, -420);
    assert(system.snapshot().scenario_ms == 0);
    assert(system.snapshot().uptime_ms == 0);
    assert(system.snapshot().boot_generation == 1);
    assert(system.snapshot().app_state == AppState::stopped);
    assert(system.snapshot().display_state == DisplayState::awake);
    assert(system.snapshot().connectivity == ConnectivityState::online);

    assert(system.set_app_state(AppState::foreground));
    assert(system.advance(2'500));
    assert(system.snapshot().scenario_ms == 2'500);
    assert(system.snapshot().uptime_ms == 2'500);
    assert(system.snapshot().wall_time_ms == 1'700'000'002'500);

    system.set_wall_time(1'800'000'000'000);
    assert(system.snapshot().scenario_ms == 2'500);
    assert(system.snapshot().uptime_ms == 2'500);
    assert(system.snapshot().wall_time_ms == 1'800'000'000'000);
    assert(system.set_timezone_offset(330));
    assert(!system.set_timezone_offset(841));

    assert(system.set_app_state(AppState::background));
    system.set_display_state(DisplayState::asleep);
    system.set_connectivity(ConnectivityState::degraded);
    assert(system.snapshot().app_state == AppState::background);
    assert(system.snapshot().display_state == DisplayState::asleep);
    assert(system.snapshot().connectivity == ConnectivityState::degraded);

    assert(system.reboot(5'000));
    assert(system.snapshot().scenario_ms == 7'500);
    assert(system.snapshot().uptime_ms == 0);
    assert(system.snapshot().wall_time_ms == 1'800'000'005'000);
    assert(system.snapshot().boot_generation == 2);
    assert(system.snapshot().app_state == AppState::stopped);
    assert(system.snapshot().display_state == DisplayState::asleep);
    assert(system.snapshot().connectivity == ConnectivityState::offline);

    assert(!system.set_app_state(AppState::background));
    assert(system.set_app_state(AppState::foreground));
    assert(system.set_app_state(AppState::crashed));
    assert(!system.set_app_state(AppState::foreground));
    assert(system.set_app_state(AppState::stopped));

    DeterministicSystem overflow(
        std::numeric_limits<std::int64_t>::max() - 5);
    assert(!overflow.advance(6));
    assert(overflow.snapshot().scenario_ms == 0);
    assert(overflow.snapshot().wall_time_ms ==
           std::numeric_limits<std::int64_t>::max() - 5);

    return 0;
}
