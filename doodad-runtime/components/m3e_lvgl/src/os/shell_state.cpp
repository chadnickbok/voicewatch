#include "m3e/os/shell_state.hpp"

#include <cstring>

namespace m3e::os {
namespace {

template <std::size_t Size>
bool copy_text(std::array<char, Size>& destination, const char* source) {
    if (source == nullptr) return false;
    const auto length = std::strlen(source);
    if (length == 0 || length >= Size) return false;
    std::memcpy(destination.data(), source, length + 1);
    return true;
}

bool same_text(const char* left, const char* right) {
    return left != nullptr && right != nullptr &&
           std::strcmp(left, right) == 0;
}

}  // namespace

bool PackageRegistry::add(
    const char* id,
    const char* name,
    const char* version,
    PackageState state) {
    if (size_ >= kCapacity || find(id) != nullptr) return false;
    PackageRecord candidate{};
    if (!copy_text(candidate.id, id) ||
        !copy_text(candidate.name, name) ||
        !copy_text(candidate.version, version)) {
        return false;
    }
    candidate.state = state;
    candidate.generation = 1;
    records_[size_++] = candidate;
    return true;
}

bool PackageRegistry::can_transition(
    PackageState from,
    PackageState to) {
    if (from == to) return true;
    switch (from) {
        case PackageState::bundled:
            return to == PackageState::installing ||
                   to == PackageState::quarantined;
        case PackageState::installed:
            return to == PackageState::update_available ||
                   to == PackageState::installing ||
                   to == PackageState::rolled_back ||
                   to == PackageState::quarantined;
        case PackageState::update_available:
            return to == PackageState::installing ||
                   to == PackageState::installed ||
                   to == PackageState::quarantined;
        case PackageState::installing:
            return to == PackageState::installed ||
                   to == PackageState::failed;
        case PackageState::failed:
            return to == PackageState::installing ||
                   to == PackageState::rolled_back ||
                   to == PackageState::quarantined;
        case PackageState::rolled_back:
            return to == PackageState::installing ||
                   to == PackageState::update_available ||
                   to == PackageState::quarantined;
        case PackageState::quarantined:
            return to == PackageState::rolled_back ||
                   to == PackageState::installed;
    }
    return false;
}

PackageRecord* PackageRegistry::find_mutable(const char* id) {
    if (id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_text(records_[index].id.data(), id)) {
            return &records_[index];
        }
    }
    return nullptr;
}

const PackageRecord* PackageRegistry::find(const char* id) const {
    if (id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (same_text(records_[index].id.data(), id)) {
            return &records_[index];
        }
    }
    return nullptr;
}

bool PackageRegistry::transition(const char* id, PackageState next) {
    auto* record = find_mutable(id);
    if (record == nullptr || !can_transition(record->state, next)) {
        return false;
    }
    if (record->state != next) {
        record->state = next;
        ++record->generation;
    }
    return true;
}

bool PackageRegistry::record_crash(const char* id) {
    auto* record = find_mutable(id);
    if (record == nullptr ||
        record->crash_count == static_cast<std::uint16_t>(-1)) {
        return false;
    }
    ++record->crash_count;
    return true;
}

bool PackageRegistry::set_permission_count(
    const char* id,
    std::uint16_t count) {
    auto* record = find_mutable(id);
    if (record == nullptr) return false;
    record->permission_count = count;
    return true;
}

std::size_t PackageRegistry::size() const {
    return size_;
}

Intent map_input(Input input) {
    switch (input) {
        case Input::swipe_up:
        case Input::button_c:
            return Intent::open_live_cards;
        case Input::swipe_down:
            return Intent::open_control_center;
        case Input::swipe_right:
        case Input::button_a:
            return Intent::back;
        case Input::center_tap:
        case Input::button_b:
            return Intent::home_or_launcher;
        case Input::long_press:
        case Input::button_b_hold:
            return Intent::open_voice;
        case Input::power_button:
            return Intent::toggle_sleep;
        case Input::reset_button:
            return Intent::none;
    }
    return Intent::none;
}

bool ShellState::initialize() {
    if (!routes_.reset("system.watchface")) return false;
    snapshot_ = {
        Surface::watch_face,
        Overlay::none,
        VoicePhase::idle,
        true,
        0,
        0,
        0,
        0,
        {},
        snapshot_.generation + 1,
    };
    return true;
}

bool ShellState::go_home() {
    if (!routes_.reset("system.watchface")) return false;
    snapshot_.surface = Surface::watch_face;
    snapshot_.overlay = Overlay::none;
    snapshot_.voice_phase = VoicePhase::idle;
    ++snapshot_.generation;
    return true;
}

bool ShellState::push_system_route(
    const char* route,
    Surface surface) {
    const auto* active = routes_.active();
    if (active != nullptr && same_text(active->id.data(), route)) {
        return true;
    }
    if (!routes_.push(route, navigation::LayerOwner::system, 0)) {
        return false;
    }
    snapshot_.surface = surface;
    ++snapshot_.generation;
    return true;
}

bool ShellState::dispatch(Intent intent) {
    switch (intent) {
        case Intent::none:
            return false;
        case Intent::back:
            if (snapshot_.overlay != Overlay::none) {
                return dismiss_overlay();
            }
            if (!routes_.pop(navigation::LayerOwner::system)) {
                return false;
            }
            if (!sync_surface_from_route()) return false;
            ++snapshot_.generation;
            return true;
        case Intent::home_or_launcher:
            if (snapshot_.surface == Surface::watch_face &&
                snapshot_.overlay == Overlay::none) {
                return push_system_route("system.launcher", Surface::launcher);
            }
            return go_home();
        case Intent::open_live_cards:
            return push_system_route(
                "system.live-cards", Surface::live_cards);
        case Intent::open_control_center:
            return push_system_route(
                "system.control-center", Surface::control_center);
        case Intent::open_voice:
            if (!show_overlay(Overlay::voice)) return false;
            return set_voice_phase(VoicePhase::listening);
        case Intent::open_app_manager:
            return open_app_manager();
        case Intent::toggle_sleep:
            snapshot_.display_awake = !snapshot_.display_awake;
            ++snapshot_.generation;
            return true;
    }
    return false;
}

bool ShellState::open_app(
    const char* app_id,
    std::uint32_t generation) {
    if (app_id == nullptr || generation == 0 ||
        !routes_.push(
            app_id, navigation::LayerOwner::application, generation)) {
        return false;
    }
    snapshot_.surface = Surface::app;
    ++snapshot_.generation;
    return true;
}

bool ShellState::replace_app(
    const char* app_id,
    std::uint32_t generation) {
    navigation::RouteStack replacement;
    if (app_id == nullptr || generation == 0 ||
        !replacement.reset("system.watchface") ||
        !replacement.push(
            app_id, navigation::LayerOwner::application, generation)) {
        return false;
    }
    routes_ = replacement;
    snapshot_.surface = Surface::app;
    snapshot_.overlay = Overlay::none;
    snapshot_.voice_phase = VoicePhase::idle;
    ++snapshot_.generation;
    return true;
}

bool ShellState::open_app_manager() {
    return push_system_route("system.app-manager", Surface::app_manager);
}

bool ShellState::open_app_detail() {
    return push_system_route("system.app-detail", Surface::app_detail);
}

bool ShellState::open_install_progress() {
    return push_system_route(
        "system.install-progress", Surface::install_progress);
}

bool ShellState::open_crash_recovery() {
    return push_system_route(
        "system.crash-recovery", Surface::crash_recovery);
}

const char* ShellState::overlay_route(Overlay overlay) {
    switch (overlay) {
        case Overlay::voice:
            return "system.voice";
        case Overlay::notification:
            return "system.notification";
        case Overlay::permission_review:
            return "system.permission-review";
        case Overlay::action_review:
            return "system.action-review";
        case Overlay::error:
            return "system.error";
        case Overlay::none:
            return nullptr;
    }
    return nullptr;
}

bool ShellState::show_overlay(Overlay overlay) {
    const auto* route = overlay_route(overlay);
    if (route == nullptr ||
        !routes_.show_overlay(route, navigation::LayerOwner::system)) {
        return false;
    }
    snapshot_.overlay = overlay;
    ++snapshot_.generation;
    return true;
}

bool ShellState::dismiss_overlay() {
    if (snapshot_.overlay == Overlay::none ||
        !routes_.dismiss_overlay(navigation::LayerOwner::system)) {
        return false;
    }
    snapshot_.overlay = Overlay::none;
    snapshot_.voice_phase = VoicePhase::idle;
    ++snapshot_.generation;
    return true;
}

bool ShellState::can_transition_voice(
    VoicePhase from,
    VoicePhase to) {
    if (from == to) return true;
    if (to == VoicePhase::idle) return true;
    switch (from) {
        case VoicePhase::idle:
            return to == VoicePhase::ready ||
                   to == VoicePhase::listening;
        case VoicePhase::ready:
            return to == VoicePhase::listening ||
                   to == VoicePhase::error;
        case VoicePhase::listening:
            return to == VoicePhase::thinking ||
                   to == VoicePhase::speaking ||
                   to == VoicePhase::ready ||
                   to == VoicePhase::error;
        case VoicePhase::thinking:
            return to == VoicePhase::clarifying ||
                   to == VoicePhase::speaking ||
                   to == VoicePhase::listening ||
                   to == VoicePhase::ready ||
                   to == VoicePhase::error;
        case VoicePhase::speaking:
            return to == VoicePhase::listening ||
                   to == VoicePhase::thinking ||
                   to == VoicePhase::ready ||
                   to == VoicePhase::error;
        case VoicePhase::clarifying:
            return to == VoicePhase::thinking ||
                   to == VoicePhase::speaking ||
                   to == VoicePhase::listening ||
                   to == VoicePhase::ready ||
                   to == VoicePhase::error;
        case VoicePhase::error:
            return to == VoicePhase::ready ||
                   to == VoicePhase::listening;
    }
    return false;
}

bool ShellState::set_voice_phase(VoicePhase phase) {
    if (phase != VoicePhase::idle &&
        snapshot_.overlay != Overlay::voice) {
        return false;
    }
    if (!can_transition_voice(snapshot_.voice_phase, phase)) {
        return false;
    }
    if (snapshot_.voice_phase != phase) {
        snapshot_.voice_phase = phase;
        ++snapshot_.generation;
    }
    return true;
}

void ShellState::publish_background_activity(
    std::uint8_t running_count,
    bool focused_question,
    bool review_ready,
    bool completion_pending,
    BackgroundInstallState install_state) {
    const BackgroundActivity next{
        running_count,
        focused_question,
        review_ready,
        completion_pending,
        install_state,
    };
    if (snapshot_.background.running_count == next.running_count &&
        snapshot_.background.focused_question == next.focused_question &&
        snapshot_.background.review_ready == next.review_ready &&
        snapshot_.background.completion_pending == next.completion_pending &&
        snapshot_.background.install_state == next.install_state) {
        return;
    }
    snapshot_.background = next;
    ++snapshot_.generation;
}

void ShellState::publish_surface_counts(
    std::uint8_t live_cards,
    std::uint8_t notifications,
    std::uint8_t ongoing) {
    snapshot_.live_card_count = live_cards;
    snapshot_.notification_count = notifications;
    snapshot_.ongoing_count = ongoing;
    if (live_cards == 0) {
        snapshot_.selected_live_card = 0;
    } else if (snapshot_.selected_live_card >= live_cards) {
        snapshot_.selected_live_card =
            static_cast<std::uint8_t>(live_cards - 1);
    }
    ++snapshot_.generation;
}

bool ShellState::select_live_card(std::uint8_t index) {
    if (index >= snapshot_.live_card_count) return false;
    snapshot_.selected_live_card = index;
    ++snapshot_.generation;
    return true;
}

bool ShellState::sync_surface_from_route() {
    const auto* active = routes_.active();
    if (active == nullptr) return false;
    const auto* route = active->id.data();
    if (same_text(route, "system.watchface")) {
        snapshot_.surface = Surface::watch_face;
    } else if (same_text(route, "system.live-cards")) {
        snapshot_.surface = Surface::live_cards;
    } else if (same_text(route, "system.launcher")) {
        snapshot_.surface = Surface::launcher;
    } else if (same_text(route, "system.control-center")) {
        snapshot_.surface = Surface::control_center;
    } else if (same_text(route, "system.app-manager")) {
        snapshot_.surface = Surface::app_manager;
    } else if (same_text(route, "system.app-detail")) {
        snapshot_.surface = Surface::app_detail;
    } else if (same_text(route, "system.install-progress")) {
        snapshot_.surface = Surface::install_progress;
    } else if (same_text(route, "system.crash-recovery")) {
        snapshot_.surface = Surface::crash_recovery;
    } else {
        snapshot_.surface = Surface::app;
    }
    return true;
}

const ShellSnapshot& ShellState::snapshot() const {
    return snapshot_;
}

const navigation::RouteStack& ShellState::routes() const {
    return routes_;
}

}  // namespace m3e::os
