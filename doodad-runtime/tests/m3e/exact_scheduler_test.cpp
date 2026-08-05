#include <cassert>
#include <cstring>
#include <cstdint>
#include <limits>

#include "m3e/conformance/deterministic_system.hpp"
#include "m3e/services/exact_scheduler.hpp"
#include "m3e/services/exact_scheduler_c.h"

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

    // Timer IDs are local to an app. Two apps can use the same timer ID, but
    // mutations and record selection must never cross the owner boundary.
    ExactScheduler owned;
    assert(owned.schedule_after_for_app(
        "dev.doodad.alpha", "timer.rest", 10, 0));
    assert(owned.schedule_after_for_app(
        "dev.doodad.beta", "timer.rest", 20, 0));
    assert(!owned.schedule_after_for_app(
        "dev.doodad.alpha", "timer.rest", 30, 0));
    assert(owned.size() == 2);
    assert(owned.cancel_for_app("dev.doodad.beta", "timer.rest"));
    assert(!owned.acknowledge_for_app(
        "dev.doodad.alpha", "timer.rest"));
    assert(owned.find_for_app("dev.doodad.alpha", "timer.rest")->state ==
           ScheduleState::scheduled);
    assert(owned.find_for_app("dev.doodad.beta", "timer.rest")->state ==
           ScheduleState::cancelled);
    assert(owned.schedule_after_for_app(
        "dev.doodad.beta", "timer.rest", 20, 0));
    assert(owned.poll(10, deliveries, 2) == 1);
    assert(std::strcmp(
        deliveries[0].owner_app_id.data(), "dev.doodad.alpha") == 0);
    assert(std::strcmp(deliveries[0].id.data(), "timer.rest") == 0);
    assert(owned.acknowledge_for_app(
        "dev.doodad.alpha", "timer.rest"));
    assert(!owned.acknowledge_for_app(
        "dev.doodad.beta", "timer.rest"));
    assert(owned.poll(20, deliveries, 2) == 1);
    assert(std::strcmp(
        deliveries[0].owner_app_id.data(), "dev.doodad.beta") == 0);

    // Ownership survives the deterministic journal round trip. A duplicate
    // local ID is valid across owners but invalid within one owner.
    ExactScheduler restored_owned;
    const auto owned_journal = owned.journal();
    assert(restored_owned.restore(owned_journal));
    assert(restored_owned.find_for_app(
        "dev.doodad.alpha", "timer.rest") != nullptr);
    assert(restored_owned.find_for_app(
        "dev.doodad.beta", "timer.rest") != nullptr);
    auto duplicate_key = owned_journal;
    duplicate_key.records[1].owner_app_id =
        duplicate_key.records[0].owner_app_id;
    duplicate_key.records[1].id = duplicate_key.records[0].id;
    assert(!restored_owned.restore(duplicate_key));

    char maximum_owner[97]{};
    std::memset(maximum_owner, 'a', sizeof(maximum_owner) - 1);
    ExactScheduler owner_limits;
    assert(owner_limits.schedule_after_for_app(
        maximum_owner, "timer.max-owner", 1, 0));
    char oversized_owner[98]{};
    std::memset(oversized_owner, 'a', sizeof(oversized_owner) - 1);
    assert(!owner_limits.schedule_after_for_app(
        oversized_owner, "timer.too-large", 1, 0));
    assert(!owner_limits.schedule_after_for_app(
        "", "timer.empty-owner", 1, 0));

    // The C host-facing selector is the actual provider-routing boundary: a
    // resident app only receives records whose signed manifest ID matches it.
    auto handle = m3e_exact_scheduler_create();
    assert(handle != nullptr);
    assert(m3e_exact_scheduler_schedule_after_for_app(
        handle, "dev.doodad.alpha", "timer.shared", 10, 0) == 10);
    assert(m3e_exact_scheduler_schedule_after_for_app(
        handle, "dev.doodad.beta", "timer.shared", 20, 0) == 20);
    m3e_schedule_record alpha_records[2]{};
    m3e_schedule_record beta_records[2]{};
    assert(m3e_exact_scheduler_records_for_app(
        handle,
        "dev.doodad.alpha",
        alpha_records,
        2,
        0) == 1);
    assert(m3e_exact_scheduler_records_for_app(
        handle,
        "dev.doodad.beta",
        beta_records,
        2,
        0) == 1);
    assert(std::strcmp(
        alpha_records[0].owner_app_id, "dev.doodad.alpha") == 0);
    assert(std::strcmp(
        beta_records[0].owner_app_id, "dev.doodad.beta") == 0);
    assert(alpha_records[0].deadline_scenario_ms == 10);
    assert(beta_records[0].deadline_scenario_ms == 20);
    assert(m3e_exact_scheduler_cancel_for_app(
        handle, "dev.doodad.alpha", "timer.shared") == 1);
    assert(m3e_exact_scheduler_records_for_app(
        handle,
        "dev.doodad.beta",
        beta_records,
        2,
        0) == 1);
    assert(beta_records[0].state ==
           static_cast<std::uint8_t>(ScheduleState::scheduled));
    assert(m3e_exact_scheduler_acknowledge_for_app(
        handle, "dev.doodad.beta", "timer.shared") == 0);
    m3e_exact_scheduler_destroy(handle);

    return 0;
}
