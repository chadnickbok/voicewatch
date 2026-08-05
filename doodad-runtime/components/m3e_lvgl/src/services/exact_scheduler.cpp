#include "m3e/services/exact_scheduler.hpp"

#include <cstring>
#include <limits>

namespace m3e::services {
namespace {

constexpr char kLegacyOwnerAppId[] = "dev.doodad.legacy";

template <std::size_t Size>
bool copy_text(std::array<char, Size>& destination, const char* source) {
    if (source == nullptr) return false;
    const auto length = std::strlen(source);
    if (length == 0 || length >= destination.size()) return false;
    std::memcpy(destination.data(), source, length + 1);
    return true;
}

template <std::size_t Size>
bool same_text(
    const std::array<char, Size>& stored,
    const char* candidate) {
    return candidate != nullptr &&
        std::strcmp(stored.data(), candidate) == 0;
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

ScheduleRecord* ExactScheduler::find_mutable_for_app(
    const char* owner_app_id,
    const char* id) {
    if (owner_app_id == nullptr || id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_text(records_[index].owner_app_id, owner_app_id) &&
            same_text(records_[index].id, id)) {
            return &records_[index];
        }
    }
    return nullptr;
}

const ScheduleRecord* ExactScheduler::find_for_app(
    const char* owner_app_id,
    const char* id) const {
    if (owner_app_id == nullptr || id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_text(records_[index].owner_app_id, owner_app_id) &&
            same_text(records_[index].id, id)) {
            return &records_[index];
        }
    }
    return nullptr;
}

const ScheduleRecord* ExactScheduler::find(const char* id) const {
    return find_for_app(kLegacyOwnerAppId, id);
}

bool ExactScheduler::schedule_after_for_app(
    const char* owner_app_id,
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    if (duration_ms == 0 || !can_add(scenario_now_ms, duration_ms)) {
        return false;
    }
    auto* record = find_mutable_for_app(owner_app_id, id);
    if (record == nullptr) {
        if (size_ >= kCapacity) return false;
        record = &records_[size_];
        if (!copy_text(record->owner_app_id, owner_app_id) ||
            !copy_text(record->id, id)) {
            *record = {};
            return false;
        }
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

bool ExactScheduler::schedule_after(
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    return schedule_after_for_app(
        kLegacyOwnerAppId, id, duration_ms, scenario_now_ms);
}

bool ExactScheduler::snooze_for_app(
    const char* owner_app_id,
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    auto* record = find_mutable_for_app(owner_app_id, id);
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

bool ExactScheduler::snooze(
    const char* id,
    std::uint64_t duration_ms,
    std::uint64_t scenario_now_ms) {
    return snooze_for_app(
        kLegacyOwnerAppId, id, duration_ms, scenario_now_ms);
}

bool ExactScheduler::cancel_for_app(
    const char* owner_app_id,
    const char* id) {
    auto* record = find_mutable_for_app(owner_app_id, id);
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

bool ExactScheduler::cancel(const char* id) {
    return cancel_for_app(kLegacyOwnerAppId, id);
}

bool ExactScheduler::acknowledge_for_app(
    const char* owner_app_id,
    const char* id) {
    auto* record = find_mutable_for_app(owner_app_id, id);
    if (record == nullptr || record->state != ScheduleState::firing) {
        return false;
    }
    record->state = ScheduleState::acknowledged;
    ++record->revision;
    changed();
    return true;
}

bool ExactScheduler::acknowledge(const char* id) {
    return acknowledge_for_app(kLegacyOwnerAppId, id);
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
        delivery.owner_app_id = record.owner_app_id;
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
        record.owner_app_id[0] == '\0' ||
        record.owner_app_id.back() != '\0' ||
        std::strlen(record.owner_app_id.data()) >=
            record.owner_app_id.size() ||
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
            if (journal.records[index].owner_app_id ==
                    journal.records[earlier].owner_app_id &&
                journal.records[index].id ==
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

std::uint64_t ExactScheduler::remaining_ms_for_app(
    const char* owner_app_id,
    const char* id,
    std::uint64_t scenario_now_ms) const {
    const auto* record = find_for_app(owner_app_id, id);
    if (record == nullptr || record->state != ScheduleState::scheduled ||
        record->deadline_scenario_ms <= scenario_now_ms) {
        return 0;
    }
    return record->deadline_scenario_ms - scenario_now_ms;
}

std::uint64_t ExactScheduler::remaining_ms(
    const char* id,
    std::uint64_t scenario_now_ms) const {
    return remaining_ms_for_app(
        kLegacyOwnerAppId, id, scenario_now_ms);
}

std::size_t ExactScheduler::size() const {
    return size_;
}

std::uint32_t ExactScheduler::generation() const {
    return generation_;
}

}  // namespace m3e::services
