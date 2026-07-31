#include "m3e/services/exact_scheduler_c.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "m3e/services/exact_scheduler.hpp"

namespace {

using Scheduler = m3e::services::ExactScheduler;

Scheduler* scheduler(m3e_exact_scheduler_handle handle) {
    return static_cast<Scheduler*>(handle);
}

}  // namespace

extern "C" {

m3e_exact_scheduler_handle m3e_exact_scheduler_create(void) {
    return new (std::nothrow) Scheduler{};
}

void m3e_exact_scheduler_destroy(m3e_exact_scheduler_handle handle) {
    delete scheduler(handle);
}

uint64_t m3e_exact_scheduler_schedule_after(
    m3e_exact_scheduler_handle handle,
    const char* id,
    uint64_t duration_ms,
    uint64_t scenario_now_ms) {
    auto* value = scheduler(handle);
    if (value == nullptr ||
        !value->schedule_after(id, duration_ms, scenario_now_ms)) {
        return 0;
    }
    const auto* record = value->find(id);
    return record == nullptr ? 0 : record->deadline_scenario_ms;
}

int m3e_exact_scheduler_cancel(
    m3e_exact_scheduler_handle handle,
    const char* id) {
    auto* value = scheduler(handle);
    return value != nullptr && value->cancel(id) ? 1 : 0;
}

int m3e_exact_scheduler_acknowledge(
    m3e_exact_scheduler_handle handle,
    const char* id) {
    auto* value = scheduler(handle);
    return value != nullptr && value->acknowledge(id) ? 1 : 0;
}

size_t m3e_exact_scheduler_poll(
    m3e_exact_scheduler_handle handle,
    uint64_t scenario_now_ms,
    m3e_due_delivery* deliveries,
    size_t delivery_capacity) {
    auto* value = scheduler(handle);
    if (value == nullptr || deliveries == nullptr ||
        delivery_capacity == 0) {
        return 0;
    }
    std::array<m3e::services::DueDelivery, Scheduler::kCapacity> native{};
    const auto bounded =
        std::min(delivery_capacity, native.size());
    const auto count = value->poll(
        scenario_now_ms, native.data(), bounded);
    for (std::size_t index = 0; index < count; ++index) {
        std::memcpy(
            deliveries[index].id,
            native[index].id.data(),
            native[index].id.size());
        deliveries[index].deadline_scenario_ms =
            native[index].deadline_scenario_ms;
        deliveries[index].revision = native[index].revision;
        deliveries[index].ordinal = native[index].ordinal;
    }
    return count;
}

size_t m3e_exact_scheduler_records(
    m3e_exact_scheduler_handle handle,
    m3e_schedule_record* records,
    size_t record_capacity,
    uint64_t scenario_now_ms) {
    auto* value = scheduler(handle);
    if (value == nullptr || records == nullptr || record_capacity == 0) {
        return 0;
    }
    const auto journal = value->journal();
    const auto count =
        std::min<std::size_t>(journal.count, record_capacity);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& source = journal.records[index];
        auto& destination = records[index];
        std::memcpy(
            destination.id, source.id.data(), source.id.size());
        destination.deadline_scenario_ms =
            source.deadline_scenario_ms;
        destination.original_duration_ms =
            source.original_duration_ms;
        destination.revision = source.revision;
        destination.fire_count = source.fire_count;
        destination.state = static_cast<uint8_t>(source.state);
        (void)scenario_now_ms;
    }
    return count;
}

}  // extern "C"
