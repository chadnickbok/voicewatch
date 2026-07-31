#include <cassert>
#include <cstdint>
#include <limits>

#include "m3e/conformance/deterministic_system.hpp"
#include "m3e/services/exact_scheduler.hpp"

int main() {
    using m3e::conformance::DeterministicSystem;
    using m3e::services::DueDelivery;
    using m3e::services::ExactScheduler;
    using m3e::services::ScheduleState;

    DeterministicSystem system(1'700'000'000'000, -420);
    ExactScheduler scheduler;
    assert(scheduler.schedule_after(
        "timer.tea", 60'000, system.snapshot().scenario_ms));
    assert(!scheduler.schedule_after(
        "timer.tea", 60'000, system.snapshot().scenario_ms));
    assert(scheduler.remaining_ms("timer.tea", 0) == 60'000);

    // Closing the app, sleeping the display, and editing civil time have no
    // effect on the scenario deadline.
    assert(system.set_app_state(m3e::conformance::AppState::foreground));
    assert(system.set_app_state(m3e::conformance::AppState::background));
    system.set_display_state(m3e::conformance::DisplayState::asleep);
    system.set_wall_time(1'900'000'000'000);
    assert(system.advance(25'000));
    assert(scheduler.remaining_ms(
        "timer.tea", system.snapshot().scenario_ms) == 35'000);

    // Persist before reboot. Downtime advances the durable scenario clock,
    // while boot uptime resets.
    const auto before_reboot = scheduler.journal();
    assert(system.reboot(30'000));
    ExactScheduler restored;
    assert(restored.restore(before_reboot));
    DueDelivery deliveries[2]{};
    assert(restored.poll(
        system.snapshot().scenario_ms, deliveries, 2) == 0);
    assert(system.advance(5'000));
    assert(restored.poll(
        system.snapshot().scenario_ms, deliveries, 2) == 1);
    assert(deliveries[0].ordinal == 1);
    assert(deliveries[0].deadline_scenario_ms == 60'000);

    // Polling again cannot fire the same deadline twice.
    assert(restored.poll(
        system.snapshot().scenario_ms, deliveries, 2) == 0);
    const auto* firing = restored.find("timer.tea");
    assert(firing != nullptr);
    assert(firing->state == ScheduleState::firing);
    assert(firing->fire_count == 1);

    // A journal written in the firing state restores the same delivery state
    // without creating another firing transition.
    ExactScheduler after_second_reboot;
    assert(after_second_reboot.restore(restored.journal()));
    assert(after_second_reboot.poll(
        system.snapshot().scenario_ms, deliveries, 2) == 0);
    assert(after_second_reboot.acknowledge("timer.tea"));
    assert(!after_second_reboot.acknowledge("timer.tea"));

    // Multiple timers, bounded capacity, snooze, and overflow all fail closed.
    ExactScheduler multiple;
    for (std::size_t index = 0; index < ExactScheduler::kCapacity; ++index) {
        char id[] = "timer.0";
        id[6] = static_cast<char>('0' + index);
        assert(multiple.schedule_after(id, index + 1, 0));
    }
    assert(!multiple.schedule_after("timer.overflow", 1, 0));
    assert(!multiple.schedule_after(
        "timer.bad",
        2,
        std::numeric_limits<std::uint64_t>::max() - 1));
    assert(multiple.poll(1, deliveries, 2) == 1);
    assert(multiple.snooze("timer.0", 10, 1));
    assert(multiple.remaining_ms("timer.0", 1) == 10);

    return 0;
}
