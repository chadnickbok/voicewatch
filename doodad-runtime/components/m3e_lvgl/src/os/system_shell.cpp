#include "m3e/os/system_shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

#include "m3e/foundation/display_profile.hpp"
#include "m3e/os/shell_state.hpp"

LV_FONT_DECLARE(m3e_home_time_font_114);
LV_FONT_DECLARE(m3e_home_time_font_148);
LV_FONT_DECLARE(m3e_shell_stat_font_38);
LV_FONT_DECLARE(m3e_weather_font_55);
LV_FONT_DECLARE(m3e_launcher_font_26);
LV_FONT_DECLARE(m3e_agent_title_font_20);
LV_FONT_DECLARE(m3e_voice_display_font_44);
LV_FONT_DECLARE(m3e_voice_label_font_26);

namespace {

constexpr char kAppsAction[] = "system.apps";
constexpr char kVoiceAction[] = "system.voice";
constexpr char kAgentsAction[] = "system.agents";
constexpr char kAgentBackAction[] = "system.agent.back";
constexpr char kVoicePrimaryAction[] = "voice.primary";
constexpr char kVoiceCancelAction[] = "voice.cancel";

constexpr std::time_t kEarliestValidWallClock = 946684800;  // 2000-01-01

struct HomeClockState {
    lv_obj_t* time;
    lv_obj_t* weekday;
    lv_obj_t* calendar_date;
    lv_timer_t* timer;
};

bool current_clock_text(
    char* time_text,
    std::size_t time_size,
    char* weekday_text,
    std::size_t weekday_size,
    char* date_text,
    std::size_t date_size) {
    static constexpr const char* kWeekdays[]{
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    static constexpr const char* kMonths[]{
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
    };
    const auto now = std::time(nullptr);
    std::tm local{};
    if (now < kEarliestValidWallClock || localtime_r(&now, &local) == nullptr ||
        local.tm_wday < 0 || local.tm_wday > 6 ||
        local.tm_mon < 0 || local.tm_mon > 11) {
        std::snprintf(time_text, time_size, "%s", "--:--");
        std::snprintf(weekday_text, weekday_size, "%s", "---");
        std::snprintf(date_text, date_size, "%s", "--- --");
        return false;
    }
    std::snprintf(
        time_text, time_size, "%02d:%02d", local.tm_hour, local.tm_min);
    std::snprintf(
        weekday_text, weekday_size, "%s", kWeekdays[local.tm_wday]);
    std::snprintf(
        date_text, date_size, "%s %d", kMonths[local.tm_mon], local.tm_mday);
    return true;
}

void refresh_home_clock(HomeClockState& state) {
    char time_text[6]{};
    char weekday_text[4]{};
    char date_text[7]{};
    current_clock_text(
        time_text, sizeof(time_text),
        weekday_text, sizeof(weekday_text),
        date_text, sizeof(date_text));
    if (std::strcmp(lv_label_get_text(state.time), time_text) != 0) {
        lv_label_set_text(state.time, time_text);
    }
    if (std::strcmp(lv_label_get_text(state.weekday), weekday_text) != 0) {
        lv_label_set_text(state.weekday, weekday_text);
    }
    if (std::strcmp(
            lv_label_get_text(state.calendar_date), date_text) != 0) {
        lv_label_set_text(state.calendar_date, date_text);
    }
}

void home_clock_timer(lv_timer_t* timer) {
    auto* state = static_cast<HomeClockState*>(lv_timer_get_user_data(timer));
    if (state != nullptr) refresh_home_clock(*state);
}

void home_clock_deleted(lv_event_t* event) {
    auto* state = static_cast<HomeClockState*>(lv_event_get_user_data(event));
    if (state == nullptr) return;
    if (state->timer != nullptr) {
        lv_timer_delete(state->timer);
        state->timer = nullptr;
    }
    lv_free(state);
}

lv_color_t rgb(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return lv_color_make(red, green, blue);
}

lv_color_t rgb24(std::uint32_t value) {
    return rgb(
        static_cast<std::uint8_t>((value >> 16) & 0xffU),
        static_cast<std::uint8_t>((value >> 8) & 0xffU),
        static_cast<std::uint8_t>(value & 0xffU));
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

lv_obj_t* pixel_surface(
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
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, radius, 0);
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

void home_agent_icon(
    lv_obj_t* parent,
    std::int32_t x,
    std::int32_t y,
    bool changed) {
    const auto purple = rgb(114, 65, 255);
    const auto lime = rgb(185, 255, 36);
    surface(parent, x + 7, y, 7, 7, 4, purple);
    surface(parent, x + 3, y + 9, 15, 7, 4, purple);
    if (!changed) return;
    surface(parent, x + 15, y - 2, 8, 8, 4, lime);
    surface(parent, x + 18, y + 1, 2, 2, 1, rgb(0, 0, 0));
}

void battery_icon_pixels(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    const auto lime = rgb(185, 255, 36);
    auto* body = lv_obj_create(parent);
    reset(body);
    lv_obj_set_pos(body, x, y);
    lv_obj_set_size(body, 14, 8);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lime, 0);
    pixel_surface(parent, x + 3, y + 3, 7, 2, 1, lime);
    pixel_surface(parent, x + 14, y + 2, 2, 4, 1, lime);
}

void ultra_battery_icon(lv_obj_t* parent, std::int32_t x, std::int32_t y) {
    const auto lime = rgb(185, 255, 36);
    auto* body = pixel_surface(parent, x, y, 20, 10, 3, lime);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, lime, 0);
    pixel_surface(parent, x + 4, y + 3, 12, 4, 2, lime);
    pixel_surface(parent, x + 20, y + 3, 3, 4, 1, lime);
}

void ultra_header(lv_obj_t* screen, const char* state) {
    char time_text[6]{};
    char weekday_text[4]{};
    char date_text[7]{};
    current_clock_text(
        time_text, sizeof(time_text),
        weekday_text, sizeof(weekday_text),
        date_text, sizeof(date_text));
    auto* time = label(
        screen, time_text, &lv_font_montserrat_12, rgb(185, 255, 36));
    lv_obj_set_pos(time, 28, 25);

    auto* battery = label(
        screen, "82%", &lv_font_montserrat_12, rgb(185, 255, 36));
    lv_obj_set_pos(battery, 326, 25);
    ultra_battery_icon(screen, 364, 28);

    auto* back = pixel_surface(screen, 26, 52, 42, 42, 21, rgb(0, 0, 0), true);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_set_style_border_color(back, rgb(58, 56, 64), 0);
    lv_obj_set_user_data(back, const_cast<char*>(kVoiceCancelAction));
    auto* arrow = label(back, "<", &lv_font_montserrat_18, rgb(185, 255, 36));
    lv_obj_align(arrow, LV_ALIGN_CENTER, 0, -1);

    auto* title = label(
        screen, "DOODAD", &m3e_voice_label_font_26, rgb(250, 249, 255));
    lv_obj_set_size(title, 210, 34);
    lv_obj_set_pos(title, 100, 58);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(title, 1, 0);

    auto* live = label(
        screen, state, &lv_font_montserrat_10, rgb(185, 255, 36));
    lv_obj_set_size(live, 60, 16);
    lv_obj_set_pos(live, 324, 68);
    lv_obj_set_style_text_align(live, LV_TEXT_ALIGN_RIGHT, 0);
}

void calculator_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* body = pixel_surface(parent, 5, 2, 26, 32, 5, white);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_border_color(body, white, 0);
    pixel_surface(parent, 9, 7, 18, 6, 2, white);
    for (std::int32_t row = 0; row < 2; ++row) {
        for (std::int32_t column = 0; column < 3; ++column) {
            pixel_surface(
                parent, 9 + column * 7, 18 + row * 7,
                4, 4, 2, white);
        }
    }
}

void timer_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* dial = pixel_surface(parent, 5, 7, 26, 26, 13, white);
    lv_obj_set_style_bg_opa(dial, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dial, 3, 0);
    lv_obj_set_style_border_color(dial, white, 0);
    pixel_surface(parent, 14, 2, 8, 4, 2, white);
    pixel_surface(parent, 17, 12, 3, 10, 2, white);
    pixel_surface(parent, 12, 20, 8, 3, 2, white);
}

void weather_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    pixel_surface(parent, 5, 7, 14, 14, 7, white);
    pixel_surface(parent, 8, 22, 23, 11, 6, white);
    pixel_surface(parent, 13, 17, 13, 15, 7, white);
    pixel_surface(parent, 4, 13, 4, 2, 1, white);
    pixel_surface(parent, 10, 3, 2, 4, 1, white);
}

void tasks_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    for (std::int32_t row = 0; row < 3; ++row) {
        pixel_surface(parent, 3, 5 + row * 10, 7, 7, 2, white);
        pixel_surface(parent, 14, 7 + row * 10, 19, 3, 2, white);
    }
}

void calendar_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* body = pixel_surface(parent, 4, 6, 28, 27, 5, white);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 3, 0);
    lv_obj_set_style_border_color(body, white, 0);
    pixel_surface(parent, 4, 12, 28, 3, 0, white);
    pixel_surface(parent, 10, 2, 3, 8, 2, white);
    pixel_surface(parent, 23, 2, 3, 8, 2, white);
}

void generic_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    for (std::int32_t row = 0; row < 3; ++row) {
        for (std::int32_t column = 0; column < 3; ++column) {
            pixel_surface(
                parent, 5 + column * 10, 5 + row * 10,
                6, 6, 3, white);
        }
    }
}

void app_builder_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    pixel_surface(parent, 5, 5, 8, 8, 2, white);
    pixel_surface(parent, 16, 5, 8, 8, 2, white);
    pixel_surface(parent, 5, 16, 8, 8, 2, white);
    pixel_surface(parent, 19, 21, 13, 5, 3, white);
    pixel_surface(parent, 16, 24, 7, 9, 3, white);
    auto* jaw = pixel_surface(parent, 25, 15, 8, 8, 4, white);
    pixel_surface(jaw, 0, 0, 4, 4, 2, rgb(0, 0, 0));
}

void research_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* page = pixel_surface(parent, 6, 4, 23, 29, 3, white);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 2, 0);
    lv_obj_set_style_border_color(page, white, 0);
    pixel_surface(parent, 10, 10, 13, 2, 1, white);
    pixel_surface(parent, 10, 16, 10, 2, 1, white);
    auto* lens = pixel_surface(parent, 20, 20, 13, 13, 7, white);
    lv_obj_set_style_bg_opa(lens, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lens, 2, 0);
    lv_obj_set_style_border_color(lens, white, 0);
    pixel_surface(parent, 30, 30, 6, 3, 2, white);
}

void monitoring_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* outer = pixel_surface(parent, 4, 4, 32, 32, 16, white);
    lv_obj_set_style_bg_opa(outer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(outer, 2, 0);
    lv_obj_set_style_border_color(outer, white, 0);
    auto* inner = pixel_surface(parent, 11, 11, 18, 18, 9, white);
    lv_obj_set_style_bg_opa(inner, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(inner, 2, 0);
    lv_obj_set_style_border_color(inner, white, 0);
    pixel_surface(parent, 19, 8, 2, 14, 1, white);
    pixel_surface(parent, 19, 19, 12, 2, 1, white);
    pixel_surface(parent, 17, 17, 6, 6, 3, white);
}

void presentation_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* frame = pixel_surface(parent, 5, 6, 30, 23, 3, white);
    lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, white, 0);
    pixel_surface(parent, 10, 21, 5, 5, 1, white);
    pixel_surface(parent, 18, 16, 5, 10, 1, white);
    pixel_surface(parent, 26, 11, 5, 15, 1, white);
    pixel_surface(parent, 19, 29, 2, 5, 1, white);
    pixel_surface(parent, 13, 34, 14, 2, 1, white);
}

void agent_icon(lv_obj_t* parent, std::uint8_t icon) {
    auto* glyph = pixel_surface(parent, 2, 2, 36, 36, 0, rgb(0, 0, 0));
    lv_obj_set_style_bg_opa(glyph, LV_OPA_TRANSP, 0);
    switch (icon) {
        case M3E_SYSTEM_SHELL_AGENT_ICON_RESEARCH:
            research_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_AGENT_ICON_MONITORING:
            monitoring_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_AGENT_ICON_PRESENTATION:
            presentation_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_AGENT_ICON_APP_BUILDER:
        default:
            app_builder_icon(glyph);
            break;
    }
}

void water_drop_icon(lv_obj_t* parent) {
    const auto white = lv_obj_get_style_text_color(parent, LV_PART_MAIN);
    auto* drop = pixel_surface(parent, 10, 10, 16, 20, 8, white);
    lv_obj_set_style_transform_rotation(drop, 450, 0);
    pixel_surface(parent, 14, 3, 8, 16, 5, white);
}

void launcher_icon(lv_obj_t* parent, std::uint8_t icon) {
    auto* glyph = pixel_surface(parent, 2, 2, 36, 36, 0, rgb(0, 0, 0));
    lv_obj_set_style_bg_opa(glyph, LV_OPA_TRANSP, 0);
    switch (icon) {
        case M3E_SYSTEM_SHELL_APP_ICON_TIMER:
            timer_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_APP_ICON_WEATHER:
            weather_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_APP_ICON_TASKS:
            tasks_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_APP_ICON_CALCULATOR:
            calculator_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_APP_ICON_CALENDAR:
            calendar_icon(glyph);
            break;
        case M3E_SYSTEM_SHELL_APP_ICON_WATER_DROP:
            water_drop_icon(glyph);
            break;
        default:
            generic_icon(glyph);
            break;
    }
}

std::array<char, 49> launcher_name(const char* supplied) {
    std::array<char, 49> output{};
    if (supplied == nullptr) return output;
    std::size_t index = 0;
    while (supplied[index] != '\0' && index + 1 < output.size()) {
        const auto byte = static_cast<unsigned char>(supplied[index]);
        output[index] = byte < 0x80U
            ? static_cast<char>(std::toupper(byte))
            : supplied[index];
        ++index;
    }
    return output;
}

m3e::os::Intent intent_from_c(int intent) {
    switch (intent) {
        case M3E_SYSTEM_SHELL_INTENT_BACK:
            return m3e::os::Intent::back;
        case M3E_SYSTEM_SHELL_INTENT_HOME_OR_LAUNCHER:
            return m3e::os::Intent::home_or_launcher;
        case M3E_SYSTEM_SHELL_INTENT_OPEN_VOICE:
            return m3e::os::Intent::open_voice;
        case M3E_SYSTEM_SHELL_INTENT_OPEN_AGENTS:
            return m3e::os::Intent::open_agents;
        case M3E_SYSTEM_SHELL_INTENT_OPEN_AGENT_DETAIL:
            return m3e::os::Intent::open_agent_detail;
        default:
            return m3e::os::Intent::none;
    }
}

const char* voice_status(int phase) {
    switch (phase) {
        case M3E_SYSTEM_SHELL_VOICE_LISTENING:
            return "LISTENING";
        case M3E_SYSTEM_SHELL_VOICE_THINKING:
        case M3E_SYSTEM_SHELL_VOICE_CLARIFYING:
            return "THINKING";
        case M3E_SYSTEM_SHELL_VOICE_SPEAKING:
            return "SPEAKING";
        case M3E_SYSTEM_SHELL_VOICE_ERROR:
            return "UNAVAILABLE";
        case M3E_SYSTEM_SHELL_VOICE_IDLE:
        case M3E_SYSTEM_SHELL_VOICE_READY:
        default:
            return "READY";
    }
}

void set_voice_bar_height(void* object, std::int32_t height) {
    auto* bar = static_cast<lv_obj_t*>(object);
    if (bar == nullptr) return;
    lv_obj_set_height(bar, height);
    lv_obj_set_y(bar, (142 - height) / 2);
}

void start_voice_bar_animation(
    lv_obj_t* bar,
    std::int32_t peak,
    std::uint32_t duration,
    std::uint32_t delay) {
    lv_anim_t animation{};
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, bar);
    lv_anim_set_exec_cb(&animation, set_voice_bar_height);
    lv_anim_set_values(&animation, 7, peak);
    lv_anim_set_duration(&animation, duration);
    lv_anim_set_reverse_duration(&animation, duration);
    lv_anim_set_delay(&animation, delay);
    lv_anim_set_repeat_delay(&animation, 35);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

}  // namespace

struct m3e_system_shell_controller {
    m3e::os::ShellState state;
};

extern "C" void m3e_system_shell_default_home_model(
    m3e_system_shell_home_model_t* model) {
    if (model == nullptr) return;
    *model = {
        "--:--", "---", "--- --", "72°", "82%", 3, true, true,
    };
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

    char current_time[6]{};
    char current_weekday[4]{};
    char current_date[7]{};
    if (model.live_clock) {
        current_clock_text(
            current_time, sizeof(current_time),
            current_weekday, sizeof(current_weekday),
            current_date, sizeof(current_date));
    }
    const auto* time_text = model.live_clock ? current_time : model.time;
    const auto* weekday_text =
        model.live_clock ? current_weekday : model.weekday;
    const auto* date_text =
        model.live_clock ? current_date : model.calendar_date;

    const auto white = rgb(250, 249, 255);
    const auto purple = rgb(115, 65, 255);
    const auto purple_dark = rgb(78, 44, 218);
    const auto lime = rgb(185, 255, 36);
    const auto coral = rgb(255, 69, 82);

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 410, 502);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    pixel_surface(screen, 28, 29, 7, 7, 4, lime);
    auto* gps = label(screen, "GPS", &lv_font_montserrat_12, lime);
    lv_obj_set_pos(gps, 41, 25);
    auto* battery = label(
        screen, model.battery, &lv_font_montserrat_12, lime);
    lv_obj_set_pos(battery, 326, 25);
    ultra_battery_icon(screen, 364, 28);

    auto* time = label(
        screen, time_text, &m3e_home_time_font_148, white);
    lv_obj_set_size(time, 410, 158);
    lv_obj_set_pos(time, 0, 62);
    lv_obj_set_style_text_align(time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(time, -5, 0);

    auto* weekday = label(
        screen, weekday_text, &lv_font_montserrat_14, purple);
    lv_obj_set_pos(weekday, 126, 176);
    lv_obj_set_width(weekday, 58);
    lv_obj_set_style_text_align(weekday, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(weekday, 2, 0);
    auto* date_divider = pixel_surface(
        screen, 195, 183, 10, 2, 1, rgb(68, 37, 171));
    (void)date_divider;
    auto* calendar_date = label(
        screen, date_text, &lv_font_montserrat_14, purple);
    lv_obj_set_pos(calendar_date, 213, 176);
    lv_obj_set_style_text_letter_space(calendar_date, 2, 0);

    if (model.live_clock) {
        auto* clock_state = static_cast<HomeClockState*>(
            lv_malloc(sizeof(HomeClockState)));
        if (clock_state != nullptr) {
            *clock_state = {time, weekday, calendar_date, nullptr};
            clock_state->timer = lv_timer_create(
                home_clock_timer, 1000, clock_state);
            if (clock_state->timer != nullptr) {
                lv_obj_add_event_cb(
                    time,
                    home_clock_deleted,
                    LV_EVENT_DELETE,
                    clock_state);
            } else {
                lv_free(clock_state);
            }
        }
    }

    auto* weather_card = pixel_surface(
        screen, 22, 219, 178, 125, 28, purple_dark);
    lv_obj_set_style_bg_grad_color(weather_card, rgb(102, 55, 244), 0);
    lv_obj_set_style_bg_grad_dir(weather_card, LV_GRAD_DIR_HOR, 0);
    auto* weather_kicker = label(
        weather_card, "WEATHER", &lv_font_montserrat_12, white);
    lv_obj_set_pos(weather_kicker, 17, 16);
    lv_obj_set_style_text_letter_space(weather_kicker, 1, 0);
    auto* weather = label(
        weather_card, model.weather, &m3e_weather_font_55, white);
    lv_obj_set_pos(weather, 16, 32);
    auto* city = label(
        weather_card, "San Francisco", &lv_font_montserrat_12, white);
    lv_obj_set_pos(city, 17, 96);

    auto* agents_column = pixel_surface(
        screen, 210, 219, 178, 125, 28, rgb(18, 17, 22), true);
    lv_obj_set_style_border_width(agents_column, 2, 0);
    lv_obj_set_style_border_color(agents_column, rgb(55, 53, 62), 0);
    lv_obj_set_user_data(agents_column, const_cast<char*>(kAgentsAction));
    auto* agents_kicker = label(
        agents_column, "AGENTS", &lv_font_montserrat_12, white);
    lv_obj_set_pos(agents_kicker, 17, 16);
    lv_obj_set_style_text_letter_space(agents_kicker, 1, 0);
    char agent_count[4]{};
    std::snprintf(
        agent_count,
        sizeof(agent_count),
        "%u",
        static_cast<unsigned>(model.agent_count));
    char agent_live[12]{};
    std::snprintf(agent_live, sizeof(agent_live), "%s LIVE", agent_count);
    auto* agents = label(
        agents_column, agent_live, &m3e_shell_stat_font_38, lime);
    lv_obj_set_pos(agents, 17, 34);
    auto* route = label(
        agents_column, "Route ready\nBuild needs review",
        &lv_font_montserrat_12, white);
    lv_obj_set_pos(route, 17, 76);
    pixel_surface(
        agents_column, 146, 15, 17, 17, 9, rgb(38, 48, 19));
    pixel_surface(
        agents_column, 151, 20, 7, 7, 4,
        model.agent_status_changed ? lime : rgb(102, 104, 106));

    auto* apps = pixel_surface(
        screen, 22, 414, 178, 68, 22, rgb(78, 44, 218), true);
    lv_obj_set_style_bg_grad_color(apps, rgb(108, 59, 250), 0);
    lv_obj_set_style_bg_grad_dir(apps, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_user_data(apps, const_cast<char*>(kAppsAction));
    constexpr std::int32_t kDot = 5;
    for (std::int32_t row = 0; row < 3; ++row) {
        for (std::int32_t column = 0; column < 3; ++column) {
            pixel_surface(
                apps, 48 + column * 8, 22 + row * 8,
                kDot, kDot, 2, white);
        }
    }
    auto* apps_title = label(
        apps, "APPS", &lv_font_montserrat_18, white);
    lv_obj_set_pos(apps_title, 84, 24);

    auto* voice = pixel_surface(
        screen, 210, 414, 178, 68, 22, coral, true);
    lv_obj_set_style_bg_grad_color(voice, rgb(255, 113, 78), 0);
    lv_obj_set_style_bg_grad_dir(voice, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_user_data(voice, const_cast<char*>(kVoiceAction));
    constexpr std::int32_t kChatHeights[] = {12, 22, 32, 42, 32, 22, 12};
    for (std::size_t index = 0; index < 7; ++index) {
        pixel_surface(
            voice,
            44 + static_cast<std::int32_t>(index) * 6,
            13 + (42 - kChatHeights[index]) / 2,
            3,
            kChatHeights[index],
            2,
            white);
    }
    auto* voice_title = label(
        voice, "CHAT", &lv_font_montserrat_18, white);
    lv_obj_set_pos(voice_title, 86, 24);

    if (view != nullptr) {
        view->apps_action = apps;
        view->voice_action = voice;
        view->agents_action = agents_column;
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
    lv_obj_set_size(screen, 410, 502);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* title = label(
        screen, "APPS", &m3e_voice_label_font_26,
        rgb(250, 249, 255));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_letter_space(title, 3, 0);

    auto* list = lv_obj_create(screen);
    reset(list);
    lv_obj_set_pos(list, 10, 32);
    lv_obj_set_size(list, 220, 208);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(list, 2, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(
        list, rgb(250, 249, 255), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_60, LV_PART_SCROLLBAR);

    const auto bounded_count = std::min(
        item_count,
        static_cast<size_t>(M3E_SYSTEM_SHELL_MAX_LAUNCHER_ACTIONS));
    if (items == nullptr || bounded_count == 0) {
        auto* empty = label(
            list,
            "No apps yet\n\nHold B and ask Doodad\nto build your first one.",
            &lv_font_montserrat_14,
            rgb(160, 157, 168));
        lv_obj_set_width(empty, 196);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_margin_top(empty, 42, 0);
    } else {
        for (std::size_t index = 0; index < bounded_count; ++index) {
            const auto primary = rgb24(items[index].primary_color_rgb);
            auto* row = pixel_surface(
                list, 0, 0, 216, 56, 14, rgb(0, 0, 0), true);
            lv_obj_set_flex_grow(row, 0);
            lv_obj_set_style_border_width(row, 2, 0);
            lv_obj_set_style_border_color(row, primary, 0);
            lv_obj_set_user_data(
                row, const_cast<char*>(items[index].app_id));
            auto* icon_tile = pixel_surface(
                row, 6, 6, 40, 40, 10, primary);
            lv_obj_set_style_text_color(
                icon_tile, rgb24(items[index].on_primary_color_rgb), 0);
            launcher_icon(icon_tile, items[index].icon);
            const auto display_name = launcher_name(items[index].name);
            auto* name = label(
                row,
                display_name.data(),
                &m3e_launcher_font_26,
                rgb(250, 249, 255));
            lv_obj_set_pos(name, 59, 16);
            lv_obj_set_size(name, 132, 25);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_letter_space(name, 0, 0);
            pixel_surface(row, 201, 25, 6, 6, 3, primary);
            if (view != nullptr) view->actions[index] = row;
        }
    }
    if (view != nullptr) view->action_count = bounded_count;
}

extern "C" void m3e_system_shell_show_agents(
    lv_obj_t* screen,
    const m3e_system_shell_agent_item_t* items,
    size_t item_count,
    uint8_t active_count,
    m3e_system_shell_agents_view_t* view) {
    if (screen == nullptr) return;
    if (view != nullptr) *view = {};

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 410, 502);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* title = label(
        screen, "AGENTS", &m3e_voice_label_font_26,
        rgb(250, 249, 255));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_letter_space(title, 2, 0);

    const auto bounded_count = std::min(
        item_count,
        static_cast<size_t>(M3E_SYSTEM_SHELL_MAX_AGENT_ACTIONS));
    char active[16]{};
    std::snprintf(
        active,
        sizeof(active),
        "%u ACTIVE",
        static_cast<unsigned>(active_count));
    auto* active_label = label(
        screen, active, &lv_font_montserrat_10, rgb(185, 255, 36));
    lv_obj_align(active_label, LV_ALIGN_TOP_RIGHT, -7, 9);

    if (items == nullptr || bounded_count == 0) {
        auto* empty = label(
            screen, "NO ACTIVE AGENTS", &m3e_launcher_font_26,
            rgb(116, 113, 124));
        lv_obj_set_width(empty, 220);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 4);
        return;
    }

    const auto visible_count = std::min<std::size_t>(bounded_count, 3);
    constexpr std::int32_t kRowY[] = {37, 103, 169};
    for (std::size_t index = 0; index < visible_count; ++index) {
        const auto primary = rgb24(items[index].primary_color_rgb);
        auto* row = pixel_surface(
            screen, 9, kRowY[index], 222, 58, 14, rgb(0, 0, 0), true);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, primary, 0);
        lv_obj_set_user_data(row, const_cast<char*>(items[index].task_id));

        auto* icon_tile = pixel_surface(row, 6, 8, 40, 40, 9, primary);
        lv_obj_set_style_text_color(icon_tile, rgb(255, 255, 255), 0);
        agent_icon(icon_tile, items[index].icon);

        const auto display_name = launcher_name(items[index].title);
        auto* name = label(
            row, display_name.data(), &m3e_agent_title_font_20,
            rgb(250, 249, 255));
        lv_obj_set_pos(name, 59, 7);
        lv_obj_set_size(name, 148, 22);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

        const auto display_status = launcher_name(items[index].status);
        auto* status = label(
            row, display_status.data(), &lv_font_montserrat_10, primary);
        lv_obj_set_pos(status, 60, 35);
        lv_obj_set_size(status, 116, 14);
        lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_letter_space(status, 0, 0);

        auto* elapsed = label(
            row, items[index].elapsed, &lv_font_montserrat_10,
            rgb(250, 249, 255));
        lv_obj_set_pos(elapsed, 176, 35);
        lv_obj_set_size(elapsed, 31, 14);
        lv_obj_set_style_text_align(elapsed, LV_TEXT_ALIGN_RIGHT, 0);
        pixel_surface(row, 210, 26, 5, 5, 3, primary);
        if (view != nullptr) view->actions[index] = row;
    }
    if (view != nullptr) view->action_count = visible_count;
}

extern "C" void m3e_system_shell_show_agent_detail(
    lv_obj_t* screen,
    const m3e_system_shell_agent_detail_model_t* supplied_model,
    m3e_system_shell_agent_detail_view_t* view) {
    if (screen == nullptr || supplied_model == nullptr) return;
    if (view != nullptr) *view = {};
    const auto& model = *supplied_model;
    const auto primary = rgb24(model.primary_color_rgb);
    const auto lime = rgb(185, 255, 36);
    const auto white = rgb(250, 249, 255);
    const auto muted = rgb(82, 80, 88);

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 410, 502);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* back = pixel_surface(
        screen, 4, 2, 28, 28, 14, rgb(0, 0, 0), true);
    lv_obj_set_user_data(back, const_cast<char*>(kAgentBackAction));
    auto* back_label = label(back, "<", &lv_font_montserrat_16, lime);
    lv_obj_center(back_label);
    if (view != nullptr) view->back_action = back;

    auto* header = label(
        screen, "AGENT", &m3e_voice_label_font_26, white);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_text_letter_space(header, 2, 0);
    auto* live = label(
        screen, "LIVE", &lv_font_montserrat_10, lime);
    lv_obj_align(live, LV_ALIGN_TOP_RIGHT, -8, 9);

    auto* icon_tile = pixel_surface(screen, 16, 44, 40, 40, 9, primary);
    lv_obj_set_style_text_color(icon_tile, white, 0);
    agent_icon(icon_tile, model.icon);

    const auto display_title = launcher_name(model.title);
    auto* title = label(
        screen, display_title.data(), &m3e_launcher_font_26, white);
    lv_obj_set_pos(title, 69, 43);
    lv_obj_set_size(title, 162, 29);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    const auto display_status = launcher_name(model.status);
    auto* status = label(
        screen, display_status.data(), &lv_font_montserrat_10, primary);
    lv_obj_set_pos(status, 70, 72);
    lv_obj_set_size(status, 103, 14);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_letter_space(status, 1, 0);
    auto* elapsed = label(
        screen, model.elapsed, &lv_font_montserrat_10, white);
    lv_obj_set_pos(elapsed, 174, 72);
    lv_obj_set_size(elapsed, 43, 14);

    auto* progress = pixel_surface(screen, 16, 101, 208, 10, 5, muted);
    const auto progress_width = std::max<std::int32_t>(
        10,
        208 * std::min<std::uint8_t>(model.progress_percent, 100) / 100);
    pixel_surface(progress, 0, 0, progress_width, 10, 5, primary);

    const auto display_context_label = launcher_name(model.context_label);
    auto* context_label = label(
        screen, display_context_label.data(), &lv_font_montserrat_10, lime);
    lv_obj_set_pos(context_label, 16, 123);
    lv_obj_set_style_text_letter_space(context_label, 1, 0);
    const auto display_context = launcher_name(model.context);
    auto* context = label(
        screen, display_context.data(), &m3e_agent_title_font_20, white);
    lv_obj_set_pos(context, 16, 142);
    lv_obj_set_size(context, 208, 22);
    lv_label_set_long_mode(context, LV_LABEL_LONG_DOT);

    constexpr std::int32_t kStageX[] = {35, 94, 151, 206};
    constexpr std::int32_t kStageLabelX[] = {8, 70, 126, 178};
    constexpr std::int32_t kStageLabelWidth[] = {54, 48, 51, 58};
    pixel_surface(screen, 35, 194, 171, 3, 2, muted);
    for (std::size_t index = 0; index < 4; ++index) {
        const bool complete = index < model.completed_stage_count;
        const bool active = index == model.active_stage;
        const auto color = complete ? lime : active ? primary : muted;
        if (complete && index + 1 < 4) {
            const auto next_x = kStageX[index + 1];
            pixel_surface(
                screen,
                kStageX[index],
                194,
                next_x - kStageX[index],
                3,
                2,
                lime);
        }
        const auto display_stage = launcher_name(model.stages[index]);
        auto* stage = label(
            screen, display_stage.data(), &lv_font_montserrat_10, color);
        lv_obj_set_pos(stage, kStageLabelX[index], 174);
        lv_obj_set_size(stage, kStageLabelWidth[index], 14);
        lv_obj_set_style_text_align(stage, LV_TEXT_ALIGN_CENTER, 0);
        if (active) {
            auto* ring = pixel_surface(
                screen, kStageX[index] - 7, 188, 15, 15, 8, primary);
            pixel_surface(ring, 3, 3, 9, 9, 5, rgb(0, 0, 0));
            pixel_surface(ring, 5, 5, 5, 5, 3, primary);
        } else {
            pixel_surface(
                screen, kStageX[index] - 5, 190, 11, 11, 6, color);
        }
    }

    auto* footer = label(
        screen, "UPDATES AUTOMATICALLY", &lv_font_montserrat_10,
        rgb(116, 113, 124));
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_letter_space(footer, 1, 0);
}

extern "C" void m3e_system_shell_show_voice_overlay(
    lv_obj_t* screen,
    int phase,
    const char* transcript,
    const char* response,
    m3e_system_shell_voice_view_t* view) {
    if (screen == nullptr) return;
    if (view != nullptr) *view = {};
    const auto white = rgb(250, 249, 255);
    const auto lime = rgb(185, 255, 36);
    const auto coral = rgb(255, 69, 82);
    const auto purple = rgb(78, 44, 218);
    const auto* prompt = transcript == nullptr || transcript[0] == '\0'
        ? "Find me a quiet walking route home..."
        : transcript;
    const auto* answer = response == nullptr || response[0] == '\0'
        ? "I found a calmer 1.8 mile route through the park. Want to start navigation?"
        : response;
    const bool listening = phase == M3E_SYSTEM_SHELL_VOICE_LISTENING;

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 410, 502);
    lv_obj_set_style_bg_color(screen, rgb(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    ultra_header(screen, listening ? "LIVE" : voice_status(phase));

    if (listening) {
        auto* status = label(
            screen, "LISTENING", &m3e_voice_display_font_44, white);
        lv_obj_set_size(status, 410, 54);
        lv_obj_set_pos(status, 0, 112);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_letter_space(status, -1, 0);

        auto* prompt_label = label(
            screen, prompt, &lv_font_montserrat_18, white);
        lv_obj_set_size(prompt_label, 306, 60);
        lv_obj_set_pos(prompt_label, 52, 174);
        lv_label_set_long_mode(prompt_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(prompt_label, LV_TEXT_ALIGN_CENTER, 0);

        auto* level = pixel_surface(
            screen, 24, 247, 362, 142, 71, purple, true);
        lv_obj_set_style_bg_grad_color(level, rgb(111, 59, 250), 0);
        lv_obj_set_style_bg_grad_dir(level, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_user_data(level, const_cast<char*>(kVoicePrimaryAction));

        constexpr std::int32_t kBarX[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT] = {
            102, 137, 172, 207, 242,
        };
        constexpr std::int32_t kPeak[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT] = {
            38, 73, 96, 73, 38,
        };
        constexpr std::uint32_t kDuration[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT] = {
            390, 470, 520, 450, 410,
        };
        constexpr std::uint32_t kDelay[M3E_SYSTEM_SHELL_VOICE_BAR_COUNT] = {
            100, 165, 225, 135, 195,
        };
        for (std::size_t index = 0;
             index < M3E_SYSTEM_SHELL_VOICE_BAR_COUNT;
             ++index) {
            auto* bar = pixel_surface(
                level, kBarX[index], 67, 19, 8, 5, white);
            start_voice_bar_animation(
                bar, kPeak[index], kDuration[index], kDelay[index]);
            if (view != nullptr) view->level_bars[index] = bar;
        }

        auto* cancel = pixel_surface(
            screen, 112, 418, 186, 56, 20, coral, true);
        lv_obj_set_style_bg_grad_color(cancel, rgb(255, 113, 78), 0);
        lv_obj_set_style_bg_grad_dir(cancel, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_user_data(cancel, const_cast<char*>(kVoiceCancelAction));
        auto* cancel_label = label(
            cancel, "CANCEL", &m3e_voice_label_font_26, white);
        lv_obj_align(cancel_label, LV_ALIGN_CENTER, 0, 0);

        if (view != nullptr) {
            view->primary_action = level;
            view->cancel_action = cancel;
            view->level_ring = level;
        }
        return;
    }

    auto* user_bubble = pixel_surface(
        screen, 81, 174, 307, 71, 18, purple);
    lv_obj_set_style_bg_grad_color(user_bubble, rgb(111, 59, 250), 0);
    lv_obj_set_style_bg_grad_dir(user_bubble, LV_GRAD_DIR_HOR, 0);
    auto* prompt_label = label(
        user_bubble, prompt, &lv_font_montserrat_16, white);
    lv_obj_set_size(prompt_label, 274, 48);
    lv_obj_set_pos(prompt_label, 16, 14);
    lv_label_set_long_mode(prompt_label, LV_LABEL_LONG_WRAP);

    auto* response_card = pixel_surface(
        screen, 22, 257, 366, 140, 24, rgb(18, 17, 22));
    lv_obj_set_style_border_width(response_card, 1, 0);
    lv_obj_set_style_border_color(response_card, rgb(55, 53, 62), 0);
    pixel_surface(response_card, 0, 0, 4, 140, 2, lime);
    auto* kicker = label(
        response_card, "ROUTE READY     34 MIN", &lv_font_montserrat_12, lime);
    lv_obj_set_pos(kicker, 21, 17);
    lv_obj_set_style_text_letter_space(kicker, 1, 0);
    pixel_surface(response_card, 111, 24, 3, 3, 2, lime);
    auto* answer_label = label(
        response_card, answer, &lv_font_montserrat_16, white);
    lv_obj_set_size(answer_label, 322, 48);
    lv_obj_set_pos(answer_label, 21, 40);
    lv_label_set_long_mode(answer_label, LV_LABEL_LONG_WRAP);

    auto* start = pixel_surface(
        response_card, 21, 94, 64, 30, 15, rgb(47, 42, 61));
    auto* start_label = label(
        start, "START", &lv_font_montserrat_12, white);
    lv_obj_center(start_label);
    auto* show_map = pixel_surface(
        response_card, 93, 94, 96, 30, 15, rgb(47, 42, 61));
    auto* map_label = label(
        show_map, "SHOW MAP", &lv_font_montserrat_12, white);
    lv_obj_center(map_label);

    auto* composer = pixel_surface(
        screen, 22, 413, 366, 68, 24, coral, true);
    lv_obj_set_style_bg_grad_color(composer, rgb(255, 113, 78), 0);
    lv_obj_set_style_bg_grad_dir(composer, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_user_data(composer, const_cast<char*>(kVoicePrimaryAction));
    constexpr std::int32_t kComposerHeights[] = {12, 20, 30, 42, 30, 20, 12};
    for (std::size_t index = 0; index < 7; ++index) {
        pixel_surface(
            composer,
            98 + static_cast<std::int32_t>(index) * 6,
            13 + (42 - kComposerHeights[index]) / 2,
            3,
            kComposerHeights[index],
            2,
            white);
    }
    auto* composer_label = label(
        composer, "HOLD TO TALK", &lv_font_montserrat_18, white);
    lv_obj_set_pos(composer_label, 147, 24);

    const bool primary_enabled =
        phase != M3E_SYSTEM_SHELL_VOICE_THINKING &&
        phase != M3E_SYSTEM_SHELL_VOICE_CLARIFYING &&
        phase != M3E_SYSTEM_SHELL_VOICE_ERROR;
    if (!primary_enabled) lv_obj_add_state(composer, LV_STATE_DISABLED);
    if (view != nullptr) {
        view->primary_action = primary_enabled ? composer : nullptr;
        view->cancel_action = nullptr;
        view->level_ring = nullptr;
    }
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
