#include "m3e/os/system_shell.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

#include "m3e/foundation/display_profile.hpp"
#include "m3e/generated/core_tokens.hpp"
#include "m3e/os/shell_state.hpp"

LV_FONT_DECLARE(m3e_home_time_font_114);

namespace {

using m3e::generated::ColorRole;

constexpr char kAppsAction[] = "system.apps";
constexpr char kVoiceAction[] = "system.voice";

lv_color_t color(ColorRole role) {
    const auto value =
        m3e::generated::kBaselineDarkColorScheme.get(role);
    return lv_color_make(value.red, value.green, value.blue);
}

lv_color_t rgb(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return lv_color_make(red, green, blue);
}

std::int32_t dp(std::int32_t value) {
    return m3e::dp_edge_to_px(
        value, m3e::watch_square_192.density_q8_8);
}

void reset(lv_obj_t* object) {
    lv_obj_remove_style_all(object);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* label(
    lv_obj_t* parent,
    const char* value,
    const lv_font_t* font,
    lv_color_t text_color) {
    auto* object = lv_label_create(parent);
    reset(object);
    lv_label_set_text(object, value == nullptr ? "" : value);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, text_color, 0);
    return object;
}

lv_obj_t* surface(
    lv_obj_t* parent,
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height,
    std::int32_t radius,
    lv_color_t fill,
    bool button = false) {
    auto* object = button ? lv_button_create(parent) : lv_obj_create(parent);
    reset(object);
    lv_obj_set_pos(object, dp(x), dp(y));
    lv_obj_set_size(object, dp(width), dp(height));
    lv_obj_set_style_radius(object, dp(radius), 0);
    lv_obj_set_style_bg_color(object, fill, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

lv_obj_t* transparent_container(
    lv_obj_t* parent,
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height) {
    auto* object = lv_obj_create(parent);
    reset(object);
    lv_obj_set_pos(object, dp(x), dp(y));
    lv_obj_set_size(object, dp(width), dp(height));
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    return object;
}

void nine_dot_icon(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    constexpr std::int32_t kDot = 3;
    constexpr std::int32_t kStep = 6;
    for (std::int32_t row = 0; row < 3; ++row) {
        for (std::int32_t column = 0; column < 3; ++column) {
            surface(
                parent,
                x + column * kStep,
                y + row * kStep,
                kDot,
                kDot,
                kDot,
                rgb(255, 255, 255));
        }
    }
}

void voice_bar_icon(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    constexpr std::int32_t kHeights[] = {6, 12, 18, 12, 6};
    for (std::size_t index = 0; index < 5; ++index) {
        surface(
            parent,
            x + static_cast<std::int32_t>(index) * 4,
            y + (18 - kHeights[index]) / 2,
            2,
            kHeights[index],
            2,
            rgb(255, 255, 255));
    }
}

void sun_icon(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    const auto purple = rgb(114, 65, 255);
    surface(parent, x + 4, y + 4, 8, 8, 4, purple);
    surface(parent, x + 7, y, 2, 3, 1, purple);
    surface(parent, x + 7, y + 13, 2, 3, 1, purple);
    surface(parent, x, y + 7, 3, 2, 1, purple);
    surface(parent, x + 13, y + 7, 3, 2, 1, purple);
}

void battery_icon(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    const auto lime = rgb(185, 255, 36);
    auto* body = lv_obj_create(parent);
    reset(body);
    lv_obj_set_pos(body, dp(x), dp(y));
    lv_obj_set_size(body, dp(14), dp(8));
    lv_obj_set_style_radius(body, dp(2), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, dp(2), 0);
    lv_obj_set_style_border_color(body, lime, 0);
    surface(parent, x + 3, y + 3, 7, 2, 1, lime);
    surface(parent, x + 14, y + 2, 2, 4, 1, lime);
}

ColorRole launcher_fill(std::uint8_t tone) {
    switch (tone) {
        case M3E_SYSTEM_SHELL_TONE_SECONDARY:
            return ColorRole::secondary_container;
        case M3E_SYSTEM_SHELL_TONE_TERTIARY:
            return ColorRole::tertiary_container;
        case M3E_SYSTEM_SHELL_TONE_PRIMARY:
        default:
            return ColorRole::primary_container;
    }
}

ColorRole launcher_content(std::uint8_t tone) {
    switch (tone) {
        case M3E_SYSTEM_SHELL_TONE_SECONDARY:
            return ColorRole::on_secondary_container;
        case M3E_SYSTEM_SHELL_TONE_TERTIARY:
            return ColorRole::on_tertiary_container;
        case M3E_SYSTEM_SHELL_TONE_PRIMARY:
        default:
            return ColorRole::on_primary_container;
    }
}

m3e::os::Intent intent_from_c(int intent) {
    switch (intent) {
        case M3E_SYSTEM_SHELL_INTENT_BACK:
            return m3e::os::Intent::back;
        case M3E_SYSTEM_SHELL_INTENT_HOME_OR_LAUNCHER:
            return m3e::os::Intent::home_or_launcher;
        case M3E_SYSTEM_SHELL_INTENT_OPEN_VOICE:
            return m3e::os::Intent::open_voice;
        default:
            return m3e::os::Intent::none;
    }
}

}  // namespace

struct m3e_system_shell_controller {
    m3e::os::ShellState state;
};

extern "C" void m3e_system_shell_default_home_model(
    m3e_system_shell_home_model_t* model) {
    if (model == nullptr) return;
    *model = {"10:09", "THU", "JUL 30", "72°  SF", "82%"};
}

extern "C" void m3e_system_shell_show_home(
    lv_obj_t* screen,
    const m3e_system_shell_home_model_t* supplied_model,
    m3e_system_shell_home_view_t* view) {
    if (screen == nullptr) return;
    if (view != nullptr) *view = {};
    m3e_system_shell_home_model_t fallback{};
    m3e_system_shell_default_home_model(&fallback);
    const auto& model = supplied_model == nullptr ? fallback : *supplied_model;

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* time = label(
        screen, model.time, &m3e_home_time_font_114, rgb(250, 249, 255));
    lv_obj_set_size(time, 240, 88);
    lv_obj_set_pos(time, 0, 16);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(time, -3, 0);

    auto* date_column = transparent_container(screen, 8, 97, 56, 42);
    auto* weekday = label(
        date_column, model.weekday, &lv_font_montserrat_14,
        rgb(115, 65, 255));
    lv_obj_align(weekday, LV_ALIGN_TOP_MID, 0, dp(3));
    auto* calendar_date = label(
        date_column, model.calendar_date, &lv_font_montserrat_14,
        rgb(250, 249, 255));
    lv_obj_align(calendar_date, LV_ALIGN_TOP_MID, 0, dp(21));

    auto* weather_column = transparent_container(screen, 68, 97, 56, 42);
    sun_icon(weather_column, 20, 1);
    auto* weather = label(
        weather_column, model.weather, &lv_font_montserrat_14,
        rgb(115, 65, 255));
    lv_obj_align(weather, LV_ALIGN_BOTTOM_MID, 0, -dp(3));

    auto* battery_column = transparent_container(screen, 128, 97, 56, 42);
    battery_icon(battery_column, 20, 4);
    auto* battery = label(
        battery_column, model.battery, &lv_font_montserrat_14,
        rgb(185, 255, 36));
    lv_obj_align(battery, LV_ALIGN_BOTTOM_MID, 0, -dp(3));

    constexpr std::int32_t kDividerX[] = {66, 126};
    for (const auto x : kDividerX) {
        surface(screen, x, 101, 1, 34, 1, rgb(54, 54, 62));
    }

    auto* apps = surface(
        screen, 10, 150, 83, 36, 12, rgb(83, 53, 218), true);
    lv_obj_set_style_bg_grad_color(apps, rgb(104, 66, 255), 0);
    lv_obj_set_style_bg_grad_dir(apps, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_user_data(apps, const_cast<char*>(kAppsAction));
    nine_dot_icon(apps, 10, 10);
    auto* apps_title = label(
        apps, "APPS", &lv_font_montserrat_16, rgb(255, 255, 255));
    lv_obj_align(apps_title, LV_ALIGN_RIGHT_MID, -dp(8), 0);

    auto* voice = surface(
        screen, 99, 150, 83, 36, 12, rgb(255, 65, 80), true);
    lv_obj_set_style_bg_grad_color(voice, rgb(255, 92, 71), 0);
    lv_obj_set_style_bg_grad_dir(voice, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_user_data(voice, const_cast<char*>(kVoiceAction));
    voice_bar_icon(voice, 9, 9);
    auto* voice_title = label(
        voice, "VOICE", &lv_font_montserrat_16, rgb(255, 255, 255));
    lv_obj_align(voice_title, LV_ALIGN_RIGHT_MID, -dp(6), 0);

    if (view != nullptr) {
        view->apps_action = apps;
        view->voice_action = voice;
    }
}

extern "C" void m3e_system_shell_show_launcher(
    lv_obj_t* screen,
    const m3e_system_shell_launcher_item_t* items,
    size_t item_count,
    m3e_system_shell_launcher_view_t* view) {
    if (screen == nullptr) return;
    if (view != nullptr) *view = {};

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_style_bg_color(screen, color(ColorRole::background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* title = label(
        screen, "APPS", &lv_font_montserrat_18,
        color(ColorRole::on_background));
    lv_obj_set_pos(title, dp(12), dp(9));
    auto* home = label(
        screen, "B  •  HOME", &lv_font_montserrat_10,
        color(ColorRole::outline));
    lv_obj_align(home, LV_ALIGN_TOP_RIGHT, -dp(12), dp(11));

    auto* list = lv_obj_create(screen);
    reset(list);
    lv_obj_set_pos(list, dp(12), dp(34));
    lv_obj_set_size(list, dp(168), dp(139));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(list, dp(4), 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    const auto bounded_count = std::min(
        item_count,
        static_cast<size_t>(M3E_SYSTEM_SHELL_MAX_LAUNCHER_ACTIONS));
    if (items == nullptr || bounded_count == 0) {
        auto* empty = label(
            list,
            "No apps yet\n\nHold B and ask Doodad\nto build your first one.",
            &lv_font_montserrat_14,
            color(ColorRole::outline));
        lv_obj_set_width(empty, dp(160));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_margin_top(empty, dp(28), 0);
    } else {
        for (std::size_t index = 0; index < bounded_count; ++index) {
            const auto fill = color(launcher_fill(items[index].tone));
            const auto content = color(launcher_content(items[index].tone));
            auto* row = surface(
                list, 0, 0, 168, 43, 21, fill, true);
            lv_obj_set_flex_grow(row, 0);
            lv_obj_set_user_data(
                row, const_cast<char*>(items[index].app_id));
            auto* avatar = surface(
                row, 6, 6, 31, 31, 16,
                color(ColorRole::surface_container_high));
            char monogram_text[2]{
                items[index].name == nullptr || items[index].name[0] == '\0'
                    ? '?'
                    : items[index].name[0],
                '\0',
            };
            auto* monogram = label(
                avatar, monogram_text, &lv_font_montserrat_18, content);
            lv_obj_center(monogram);
            auto* name = label(
                row, items[index].name, &lv_font_montserrat_18, content);
            lv_obj_set_pos(name, dp(45), dp(6));
            auto* detail = label(
                row, items[index].detail, &lv_font_montserrat_10, content);
            lv_obj_set_pos(detail, dp(45), dp(24));
            auto* arrow = label(
                row, ">", &lv_font_montserrat_18, content);
            lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -dp(12), 0);
            if (view != nullptr) view->actions[index] = row;
        }
    }

    auto* hint = label(
        screen,
        "A  •  BACK     HOLD B  •  VOICE",
        &lv_font_montserrat_10,
        color(ColorRole::outline));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -dp(6));
    if (view != nullptr) view->action_count = bounded_count;
}

extern "C" m3e_system_shell_controller_t*
m3e_system_shell_controller_create(void) {
    return new (std::nothrow) m3e_system_shell_controller{};
}

extern "C" void m3e_system_shell_controller_destroy(
    m3e_system_shell_controller_t* controller) {
    delete controller;
}

extern "C" int m3e_system_shell_controller_initialize(
    m3e_system_shell_controller_t* controller) {
    return controller != nullptr && controller->state.initialize();
}

extern "C" int m3e_system_shell_controller_dispatch(
    m3e_system_shell_controller_t* controller,
    int intent) {
    return controller != nullptr &&
        controller->state.dispatch(intent_from_c(intent));
}

extern "C" int m3e_system_shell_controller_open_app(
    m3e_system_shell_controller_t* controller,
    const char* app_id,
    uint32_t generation) {
    return controller != nullptr &&
        controller->state.open_app(app_id, generation);
}

extern "C" int m3e_system_shell_controller_surface(
    const m3e_system_shell_controller_t* controller) {
    return controller == nullptr
        ? -1
        : static_cast<int>(controller->state.snapshot().surface);
}

extern "C" int m3e_system_shell_controller_overlay(
    const m3e_system_shell_controller_t* controller) {
    return controller == nullptr
        ? -1
        : static_cast<int>(controller->state.snapshot().overlay);
}

extern "C" int m3e_system_shell_controller_voice_phase(
    const m3e_system_shell_controller_t* controller) {
    return controller == nullptr
        ? -1
        : static_cast<int>(controller->state.snapshot().voice_phase);
}
