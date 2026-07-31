#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/navigation/route_stack.hpp"

namespace m3e::os {

enum class Surface : std::uint8_t {
    watch_face,
    live_cards,
    launcher,
    control_center,
    app,
    app_manager,
    app_detail,
    install_progress,
    crash_recovery,
};

enum class Overlay : std::uint8_t {
    none,
    voice,
    notification,
    permission_review,
    action_review,
    error,
};

enum class VoicePhase : std::uint8_t {
    idle,
    listening,
    transcribing,
    clarifying,
    reviewing,
    building,
    completed,
    error,
};

enum class Input : std::uint8_t {
    swipe_up,
    swipe_down,
    swipe_right,
    center_tap,
    long_press,
    button_a,
    button_b,
    button_b_hold,
    button_c,
    power_button,
    reset_button,
};

enum class Intent : std::uint8_t {
    none,
    back,
    home_or_launcher,
    open_live_cards,
    open_control_center,
    open_voice,
    open_app_manager,
    toggle_sleep,
};

enum class PackageState : std::uint8_t {
    bundled,
    installed,
    update_available,
    installing,
    failed,
    rolled_back,
    quarantined,
};

struct PackageRecord {
    std::array<char, 97> id{};
    std::array<char, 49> name{};
    std::array<char, 25> version{};
    std::uint32_t generation = 0;
    std::uint16_t permission_count = 0;
    std::uint16_t crash_count = 0;
    PackageState state = PackageState::installed;
};

class PackageRegistry {
public:
    static constexpr std::size_t kCapacity = 24;

    [[nodiscard]] bool add(
        const char* id,
        const char* name,
        const char* version,
        PackageState state);
    [[nodiscard]] bool transition(const char* id, PackageState next);
    [[nodiscard]] bool record_crash(const char* id);
    [[nodiscard]] bool set_permission_count(
        const char* id,
        std::uint16_t count);
    [[nodiscard]] const PackageRecord* find(const char* id) const;
    [[nodiscard]] std::size_t size() const;

private:
    [[nodiscard]] static bool can_transition(
        PackageState from,
        PackageState to);
    [[nodiscard]] PackageRecord* find_mutable(const char* id);

    std::array<PackageRecord, kCapacity> records_{};
    std::size_t size_ = 0;
};

struct ShellSnapshot {
    Surface surface;
    Overlay overlay;
    VoicePhase voice_phase;
    bool display_awake;
    std::uint8_t selected_live_card;
    std::uint8_t live_card_count;
    std::uint8_t notification_count;
    std::uint8_t ongoing_count;
    std::uint32_t generation;
};

[[nodiscard]] Intent map_input(Input input);

class ShellState {
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool dispatch(Intent intent);
    [[nodiscard]] bool open_app(
        const char* app_id,
        std::uint32_t generation);
    [[nodiscard]] bool open_app_manager();
    [[nodiscard]] bool open_app_detail();
    [[nodiscard]] bool open_install_progress();
    [[nodiscard]] bool open_crash_recovery();
    [[nodiscard]] bool show_overlay(Overlay overlay);
    [[nodiscard]] bool dismiss_overlay();
    [[nodiscard]] bool set_voice_phase(VoicePhase phase);
    void publish_surface_counts(
        std::uint8_t live_cards,
        std::uint8_t notifications,
        std::uint8_t ongoing);
    [[nodiscard]] bool select_live_card(std::uint8_t index);

    [[nodiscard]] const ShellSnapshot& snapshot() const;
    [[nodiscard]] const navigation::RouteStack& routes() const;

private:
    [[nodiscard]] bool go_home();
    [[nodiscard]] bool push_system_route(
        const char* route,
        Surface surface);
    [[nodiscard]] bool sync_surface_from_route();
    [[nodiscard]] static bool can_transition_voice(
        VoicePhase from,
        VoicePhase to);
    [[nodiscard]] static const char* overlay_route(Overlay overlay);

    navigation::RouteStack routes_{};
    ShellSnapshot snapshot_{
        Surface::watch_face,
        Overlay::none,
        VoicePhase::idle,
        true,
        0,
        0,
        0,
        0,
        0,
    };
};

}  // namespace m3e::os
