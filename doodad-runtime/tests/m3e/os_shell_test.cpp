#include <cassert>
#include <cstring>

#include "m3e/os/shell_state.hpp"

int main() {
    using namespace m3e::os;

    ShellState shell;
    assert(shell.initialize());
    assert(shell.snapshot().surface == Surface::watch_face);
    assert(shell.routes().depth() == 1);

    assert(map_input(Input::swipe_up) == Intent::open_live_cards);
    assert(map_input(Input::button_a) == Intent::back);
    assert(map_input(Input::button_b) == Intent::home_or_launcher);
    assert(map_input(Input::button_b_hold) == Intent::open_voice);
    assert(map_input(Input::button_c) == Intent::open_live_cards);
    assert(map_input(Input::reset_button) == Intent::none);

    assert(shell.dispatch(map_input(Input::button_b)));
    assert(shell.snapshot().surface == Surface::launcher);
    assert(shell.open_app("dev.doodad.timer", 4));
    assert(shell.snapshot().surface == Surface::app);
    assert(shell.dispatch(map_input(Input::button_a)));
    assert(shell.snapshot().surface == Surface::launcher);
    assert(shell.dispatch(map_input(Input::button_b)));
    assert(shell.snapshot().surface == Surface::watch_face);

    shell.publish_surface_counts(3, 2, 1);
    assert(shell.dispatch(map_input(Input::button_c)));
    assert(shell.snapshot().surface == Surface::live_cards);
    assert(shell.select_live_card(2));
    assert(!shell.select_live_card(3));
    assert(shell.snapshot().selected_live_card == 2);

    assert(shell.dispatch(map_input(Input::button_b_hold)));
    assert(shell.snapshot().overlay == Overlay::voice);
    assert(shell.snapshot().voice_phase == VoicePhase::listening);
    assert(shell.set_voice_phase(VoicePhase::transcribing));
    assert(shell.set_voice_phase(VoicePhase::clarifying));
    assert(shell.set_voice_phase(VoicePhase::reviewing));
    assert(shell.set_voice_phase(VoicePhase::building));
    assert(shell.set_voice_phase(VoicePhase::completed));
    assert(!shell.set_voice_phase(VoicePhase::building));
    assert(shell.dispatch(Intent::back));
    assert(shell.snapshot().overlay == Overlay::none);
    assert(shell.snapshot().surface == Surface::live_cards);

    assert(shell.dispatch(map_input(Input::power_button)));
    assert(!shell.snapshot().display_awake);
    assert(shell.dispatch(map_input(Input::power_button)));
    assert(shell.snapshot().display_awake);

    assert(shell.open_app_manager());
    assert(shell.snapshot().surface == Surface::app_manager);
    assert(shell.open_app_detail());
    assert(shell.snapshot().surface == Surface::app_detail);
    assert(shell.open_install_progress());
    assert(shell.snapshot().surface == Surface::install_progress);
    assert(shell.open_crash_recovery());
    assert(shell.snapshot().surface == Surface::crash_recovery);
    assert(shell.dispatch(Intent::back));
    assert(shell.snapshot().surface == Surface::install_progress);
    assert(shell.show_overlay(Overlay::notification));
    assert(shell.snapshot().overlay == Overlay::notification);
    assert(shell.dismiss_overlay());
    assert(shell.show_overlay(Overlay::permission_review));
    assert(shell.dismiss_overlay());
    assert(shell.show_overlay(Overlay::action_review));
    assert(shell.dismiss_overlay());
    assert(shell.show_overlay(Overlay::error));
    assert(shell.dismiss_overlay());

    PackageRegistry packages;
    assert(packages.add(
        "dev.doodad.timer", "Timer", "0.1.0", PackageState::bundled));
    assert(!packages.add(
        "dev.doodad.timer", "Duplicate", "0.1.0", PackageState::installed));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::installing));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::installed));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::update_available));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::installing));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::failed));
    assert(packages.transition(
        "dev.doodad.timer", PackageState::rolled_back));
    assert(packages.record_crash("dev.doodad.timer"));
    assert(packages.set_permission_count("dev.doodad.timer", 3));
    const auto* timer = packages.find("dev.doodad.timer");
    assert(timer != nullptr);
    assert(std::strcmp(timer->name.data(), "Timer") == 0);
    assert(timer->state == PackageState::rolled_back);
    assert(timer->crash_count == 1);
    assert(timer->permission_count == 3);
    assert(timer->generation == 7);
    assert(packages.size() == 1);

    return 0;
}
