#include "m3e/services/exact_scheduler.hpp"

#include <cstring>
#include <limits>

namespace m3e::services {
namespace {

bool copy_id(std::array<char, 49>& destination, const char* source) {
    if (source == nullptr) return false;
    const auto length = std::strlen(source);
    if (length == 0 || length >= destination.size()) return false;
    std::memcpy(destination.data(), source, length + 1);
    return true;
}

bool same_id(const std::array<char, 49>& id, const char* candidate) {
    return candidate != nullptr && std::strcmp(id.data(), candidate) == 0;
}

bool can_add(std::uint64_t value, std::uint64_t increment) {
    return increment <= std::numeric_limits<std::uint64_t>::max() - value;
}

}  // namespace

void ExactScheduler::changed() {
    if (generation_ != std::numeric_limits<std::uint32_t>::max()) {
        ++generation_;
    }
}

ScheduleRecord* ExactScheduler::find_mutable(const char* id) {
    if (id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_id(records_[index].id, id)) return &records_[index];
    }
    return nullptr;
}

const ScheduleRecord* ExactScheduler::find(const char* id) const {
    if (id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_id(records_[index].id, id)) return &records_[index];
    }
    return nullptr;
}

bool ExactScheduler::schedule_after(
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    if (duration_ms == 0 || !can_add(scenario_now_ms, duration_ms)) {
        return false;
    }
    auto* record = find_mutable(id);
    if (record == nullptr) {
        if (size_ >= kCapacity) return false;
        record = &records_[size_];
        if (!copy_id(record->id, id)) return false;
        ++size_;
    } else if (
        record->state == ScheduleState::scheduled ||
        record->state == ScheduleState::firing) {
        return false;
    }
    record->deadline_scenario_ms = scenario_now_ms + duration_ms;
    record->original_duration_ms = duration_ms;
    ++record->revision;
    record->state = ScheduleState::scheduled;
    changed();
    return true;
}

bool ExactScheduler::snooze(
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    auto* record = find_mutable(id);
    if (record == nullptr || record->state != ScheduleState::firing ||
        duration_ms == 0 || !can_add(scenario_now_ms, duration_ms)) {
        return false;
    }
    record->deadline_scenario_ms = scenario_now_ms + duration_ms;
    record->original_duration_ms = duration_ms;
    ++record->revision;
    record->state = ScheduleState::scheduled;
    changed();
    return true;
}

bool ExactScheduler::cancel(const char* id) {
    auto* record = find_mutable(id);
    if (record == nullptr ||
        (record->state != ScheduleState::scheduled &&
         record->state != ScheduleState::firing)) {
        return false;
    }
    record->state = ScheduleState::cancelled;
    ++record->revision;
    changed();
    return true;
}

bool ExactScheduler::acknowledge(const char* id) {
    auto* record = find_mutable(id);
    if (record == nullptr || record->state != ScheduleState::firing) {
        return false;
    }
    record->state = ScheduleState::acknowledged;
    ++record->revision;
    changed();
    return true;
}

std::size_t ExactScheduler::poll(
    std::uint64_t scenario_now_ms,
    DueDelivery* deliveries,
    std::size_t delivery_capacity) {
    if (deliveries == nullptr && delivery_capacity != 0) return 0;
    std::size_t emitted = 0;
    for (std::size_t index = 0; index < size_; ++index) {
        auto& record = records_[index];
        if (record.state != ScheduleState::scheduled ||
            record.deadline_scenario_ms > scenario_now_ms) {
            continue;
        }
        if (emitted >= delivery_capacity ||
            record.fire_count == std::numeric_limits<std::uint32_t>::max()) {
            break;
        }
        record.state = ScheduleState::firing;
        ++record.revision;
        ++record.fire_count;
        auto& delivery = deliveries[emitted++];
        delivery.id = record.id;
        delivery.deadline_scenario_ms = record.deadline_scenario_ms;
        delivery.revision = record.revision;
        delivery.ordinal = record.fire_count;
        changed();
    }
    return emitted;
}

bool ExactScheduler::record_is_valid(const ScheduleRecord& record) {
    if (record.state == ScheduleState::empty ||
        record.id[0] == '\0' ||
        record.id.back() != '\0' ||
        std::strlen(record.id.data()) >= record.id.size()) {
        return false;
    }
    if ((record.state == ScheduleState::scheduled ||
         record.state == ScheduleState::firing) &&
        (record.original_duration_ms == 0 || record.revision == 0)) {
        return false;
    }
    if (record.state == ScheduleState::firing && record.fire_count == 0) {
        return false;
    }
    return true;
}

bool ExactScheduler::restore(const SchedulerJournal& journal) {
    if (journal.schema_version != SchedulerJournal::kSchemaVersion ||
        journal.count > kCapacity) {
        return false;
    }
    for (std::size_t index = 0; index < journal.count; ++index) {
        if (!record_is_valid(journal.records[index])) return false;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (journal.records[index].id ==
                journal.records[earlier].id) {
                return false;
            }
        }
    }
    records_ = journal.records;
    size_ = journal.count;
    generation_ = journal.generation;
    return true;
}

SchedulerJournal ExactScheduler::journal() const {
    SchedulerJournal result{};
    result.generation = generation_;
    result.count = static_cast<std::uint8_t>(size_);
    result.records = records_;
    return result;
}

std::uint64_t ExactScheduler::remaining_ms(
    const char* id,
    std::uint64_t scenario_now_ms) const {
    const auto* record = find(id);
    if (record == nullptr || record->state != ScheduleState::scheduled ||
        record->deadline_scenario_ms <= scenario_now_ms) {
        return 0;
    }
    return record->deadline_scenario_ms - scenario_now_ms;
}

std::size_t ExactScheduler::size() const {
    return size_;
}

std::uint32_t ExactScheduler::generation() const {
    return generation_;
}

}  // namespace m3e::services
