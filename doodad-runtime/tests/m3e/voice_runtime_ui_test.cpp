#include <cassert>
#include <cstdint>
#include <cstring>

#include "lvgl.h"
#include "m3e/catalog/catalog.h"

namespace {

bool contains_label(lv_obj_t* object, const char* expected) {
    if (object == nullptr || expected == nullptr) return false;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const auto* text = lv_label_get_text(object);
        if (text != nullptr && std::strcmp(text, expected) == 0) {
            return true;
        }
    }
    const auto children = lv_obj_get_child_count(object);
    for (std::uint32_t index = 0; index < children; ++index) {
        if (contains_label(lv_obj_get_child(object, index), expected)) {
            return true;
        }
    }
    return false;
}

void assert_action(lv_obj_t* object, const char* expected) {
    assert(object != nullptr);
    const auto* action = static_cast<const char*>(
        lv_obj_get_user_data(object));
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

    m3e_voice_runtime_view_t view{};
    m3e_catalog_show_voice_runtime(
        screen, 6, nullptr, nullptr, &view);
    assert_action(view.primary_action, "voice.primary");
    assert_action(view.cancel_action, "voice.cancel");
    assert(view.level_ring == view.primary_action);
    for (auto* bar : view.level_bars) {
        assert(bar != nullptr);
        assert(lv_obj_get_height(bar) == 7);
    }
    lv_obj_update_layout(screen);
    const auto bar_group_left = lv_obj_get_x(view.level_bars[0]);
    const auto bar_group_right =
        lv_obj_get_x(view.level_bars[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT - 1]) +
        lv_obj_get_width(
            view.level_bars[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT - 1]);
    assert(
        bar_group_left + bar_group_right ==
        lv_obj_get_width(view.level_ring));
    const auto bar_group_top = lv_obj_get_y(view.level_bars[0]);
    const auto bar_group_bottom =
        bar_group_top + lv_obj_get_height(view.level_bars[0]);
    const auto vertical_center_error =
        bar_group_top + bar_group_bottom - lv_obj_get_height(view.level_ring);
    assert(vertical_center_error >= -1 && vertical_center_error <= 1);
    assert(contains_label(screen, "READY"));

    m3e_catalog_show_voice_runtime(
        screen, 1, "Hello Doodad", nullptr, &view);
    assert_action(view.primary_action, "voice.primary");
    assert(contains_label(screen, "LISTENING"));

    m3e_catalog_show_voice_runtime(
        screen, 2, "What's the weather?", nullptr, &view);
    assert(view.primary_action == nullptr);
    assert_action(view.cancel_action, "voice.cancel");
    assert(contains_label(screen, "THINKING"));

    m3e_catalog_show_voice_runtime(
        screen, 3, nullptr, "It will be sunny today.", &view);
    assert_action(view.primary_action, "voice.primary");
    assert(contains_label(screen, "SPEAKING"));

    m3e_catalog_show_voice_runtime(
        screen, 5, nullptr, "Voice connection lost", &view);
    assert(view.primary_action == nullptr);
    assert_action(view.cancel_action, "voice.cancel");
    assert(contains_label(screen, "UNAVAILABLE"));

    lv_display_delete(display);
    lv_deinit();
    return 0;
}
