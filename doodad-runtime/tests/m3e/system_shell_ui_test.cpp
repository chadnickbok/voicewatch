#include <cassert>
#include <cstdint>
#include <cstring>

#include "lvgl.h"
#include "m3e/os/system_shell.h"

namespace {

bool contains_label(lv_obj_t* object, const char* expected) {
    if (object == nullptr || expected == nullptr) return false;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const auto* text = lv_label_get_text(object);
        if (text != nullptr && std::strcmp(text, expected) == 0) return true;
    }
    const auto count = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0; index < count; ++index) {
        if (contains_label(lv_obj_get_child(object, index), expected)) {
            return true;
        }
    }
    return false;
}

void assert_action(lv_obj_t* object, const char* expected) {
    assert(object != nullptr);
    assert(lv_obj_check_type(object, &lv_button_class));
    const auto* action =
        static_cast<const char*>(lv_obj_get_user_data(object));
    assert(action != nullptr);
    assert(std::strcmp(action, expected) == 0);
}

}  // namespace

int main() {
    lv_init();
    auto* display = lv_display_create(240, 240);
    assert(display != nullptr);
    auto* screen = lv_screen_active();
    assert(screen != nullptr);

    m3e_system_shell_home_model_t home{};
    m3e_system_shell_default_home_model(&home);
    m3e_system_shell_home_view_t home_view{};
    m3e_system_shell_show_home(screen, &home, &home_view);
    assert_action(home_view.apps_action, "system.apps");
    assert_action(home_view.voice_action, "system.voice");
    assert(contains_label(screen, "10:09"));
    assert(contains_label(screen, "JUL 30"));
    assert(contains_label(screen, "APPS"));
    assert(contains_label(screen, "VOICE"));

    constexpr m3e_system_shell_launcher_item_t items[] = {
        {"dev.doodad.timer", "Timer", "Version 0.1.0  •  ready",
         M3E_SYSTEM_SHELL_TONE_PRIMARY},
        {"dev.doodad.weather", "Weather", "72°  •  Partly cloudy",
         M3E_SYSTEM_SHELL_TONE_SECONDARY},
    };
    m3e_system_shell_launcher_view_t launcher_view{};
    m3e_system_shell_show_launcher(screen, items, 2, &launcher_view);
    assert(launcher_view.action_count == 2);
    assert_action(launcher_view.actions[0], "dev.doodad.timer");
    assert_action(launcher_view.actions[1], "dev.doodad.weather");
    assert(contains_label(screen, "Timer"));
    assert(contains_label(screen, "Weather"));

    auto* controller = m3e_system_shell_controller_create();
    assert(controller != nullptr);
    assert(m3e_system_shell_controller_initialize(controller));
    assert(m3e_system_shell_controller_surface(controller) ==
           M3E_SYSTEM_SHELL_SURFACE_WATCH_FACE);
    assert(m3e_system_shell_controller_dispatch(
        controller, M3E_SYSTEM_SHELL_INTENT_HOME_OR_LAUNCHER));
    assert(m3e_system_shell_controller_surface(controller) ==
           M3E_SYSTEM_SHELL_SURFACE_LAUNCHER);
    assert(m3e_system_shell_controller_open_app(
        controller, "dev.doodad.timer", 1));
    assert(m3e_system_shell_controller_surface(controller) ==
           M3E_SYSTEM_SHELL_SURFACE_APP);
    assert(m3e_system_shell_controller_dispatch(
        controller, M3E_SYSTEM_SHELL_INTENT_BACK));
    assert(m3e_system_shell_controller_surface(controller) ==
           M3E_SYSTEM_SHELL_SURFACE_LAUNCHER);
    assert(m3e_system_shell_controller_dispatch(
        controller, M3E_SYSTEM_SHELL_INTENT_HOME_OR_LAUNCHER));
    assert(m3e_system_shell_controller_surface(controller) ==
           M3E_SYSTEM_SHELL_SURFACE_WATCH_FACE);
    assert(m3e_system_shell_controller_dispatch(
        controller, M3E_SYSTEM_SHELL_INTENT_OPEN_VOICE));
    assert(m3e_system_shell_controller_overlay(controller) ==
           M3E_SYSTEM_SHELL_OVERLAY_VOICE);
    assert(m3e_system_shell_controller_dispatch(
        controller, M3E_SYSTEM_SHELL_INTENT_BACK));
    assert(m3e_system_shell_controller_overlay(controller) ==
           M3E_SYSTEM_SHELL_OVERLAY_NONE);
    m3e_system_shell_controller_destroy(controller);

    lv_display_delete(display);
    lv_deinit();
    return 0;
}
