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
    assert(contains_label(screen, "READY"));
    assert(contains_label(screen, "Tap to talk"));

    m3e_catalog_show_voice_runtime(
        screen, 1, "Hello Doodad", nullptr, &view);
    assert_action(view.primary_action, "voice.primary");
    assert(contains_label(screen, "LISTENING"));
    assert(contains_label(screen, "Hello Doodad"));
    assert(!contains_label(screen, "Set a timer for five minutes"));

    m3e_catalog_show_voice_runtime(
        screen, 2, "What's the weather?", nullptr, &view);
    assert(view.primary_action == nullptr);
    assert_action(view.cancel_action, "voice.cancel");
    assert(contains_label(screen, "THINKING"));
    assert(contains_label(screen, "What's the weather?"));

    m3e_catalog_show_voice_runtime(
        screen, 3, nullptr, "It will be sunny today.", &view);
    assert_action(view.primary_action, "voice.primary");
    assert(contains_label(screen, "SPEAKING"));
    assert(contains_label(screen, "It will be sunny today."));

    m3e_catalog_show_voice_runtime(
        screen, 5, nullptr, "Voice connection lost", &view);
    assert(view.primary_action == nullptr);
    assert_action(view.cancel_action, "voice.cancel");
    assert(contains_label(screen, "UNAVAILABLE"));
    assert(contains_label(screen, "Voice connection lost"));

    lv_display_delete(display);
    lv_deinit();
    return 0;
}
