#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e::services {

enum class ScheduleState : std::uint8_t {
    empty,
    scheduled,
    firing,
    acknowledged,
    cancelled,
};

struct ScheduleRecord {
    std::array<char, 97> owner_app_id{};
    std::array<char, 49> id{};
    std::uint64_t deadline_scenario_ms = 0;
    std::uint64_t original_duration_ms = 0;
    std::uint64_t revision = 0;
    std::uint32_t fire_count = 0;
    ScheduleState state = ScheduleState::empty;
};

struct SchedulerJournal {
    static constexpr std::uint32_t kSchemaVersion = 2;
    static constexpr std::size_t kCapacity = 8;

    std::uint32_t schema_version = kSchemaVersion;
    std::uint32_t generation = 0;
    std::uint8_t count = 0;
    std::array<ScheduleRecord, kCapacity> records{};
};

struct DueDelivery {
    std::array<char, 97> owner_app_id{};
    std::array<char, 49> id{};
    std::uint64_t deadline_scenario_ms = 0;
    std::uint64_t revision = 0;
    std::uint32_t ordinal = 0;
};

// Fixed-capacity host scheduler whose deadlines use the scenario clock. The
// scenario clock survives simulated reboot and is not affected by civil-time
// or timezone changes, making relative timers deterministic on every target.
class ExactScheduler {
public:
    static constexpr std::size_t kCapacity = SchedulerJournal::kCapacity;

    // Timer IDs are app-local. Owned operations key records by the stable
    // manifest app ID as well as the guest-supplied timer ID, so two resident
    // apps can safely use the same local name.
    [[nodiscard]] bool schedule_after_for_app(
        const char* owner_app_id,
        const char* id,
        std::uint64_t duration_ms,
        std::uint64_t scenario_now_ms);
    [[nodiscard]] bool snooze_for_app(
        const char* owner_app_id,
        const char* id,
        std::uint64_t duration_ms,
        std::uint64_t scenario_now_ms);
    [[nodiscard]] bool cancel_for_app(
        const char* owner_app_id,
        const char* id);
    [[nodiscard]] bool acknowledge_for_app(
        const char* owner_app_id,
        const char* id);

    // Legacy single-app wrappers remain for the native simulator and existing
    // callers. Production multi-app hosts should always use the owned forms.
    [[nodiscard]] bool schedule_after(
        const char* id,
        std::uint64_t duration_ms,
        std::uint64_t scenario_now_ms);
    [[nodiscard]] bool snooze(
        const char* id,
        std::uint64_t duration_ms,
        std::uint64_t scenario_now_ms);
    [[nodiscard]] bool cancel(const char* id);
    [[nodiscard]] bool acknowledge(const char* id);

    // Transitions newly-due records to firing and returns each transition once.
    // Repeated polls cannot increment fire_count or emit a duplicate delivery.
    [[nodiscard]] std::size_t poll(
        std::uint64_t scenario_now_ms,
        DueDelivery* deliveries,
        std::size_t delivery_capacity);

    [[nodiscard]] bool restore(const SchedulerJournal& journal);
    [[nodiscard]] SchedulerJournal journal() const;
    [[nodiscard]] const ScheduleRecord* find_for_app(
        const char* owner_app_id,
        const char* id) const;
    [[nodiscard]] const ScheduleRecord* find(const char* id) const;
    [[nodiscard]] std::uint64_t remaining_ms_for_app(
        const char* owner_app_id,
        const char* id,
        std::uint64_t scenario_now_ms) const;
    [[nodiscard]] std::uint64_t remaining_ms(
        const char* id,
        std::uint64_t scenario_now_ms) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint32_t generation() const;

private:
    [[nodiscard]] ScheduleRecord* find_mutable_for_app(
        const char* owner_app_id,
        const char* id);
    [[nodiscard]] static bool record_is_valid(const ScheduleRecord& record);
    void changed();

    std::array<ScheduleRecord, kCapacity> records_{};
    std::size_t size_ = 0;
    std::uint32_t generation_ = 0;
};

}  // namespace m3e::services
