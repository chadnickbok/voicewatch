#include "m3e/catalog/catalog.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "m3e/components/components.hpp"
#include "m3e/foundation/display_profile.hpp"
#include "m3e/generated/core_tokens.hpp"
#include "m3e/os/system_shell.h"
#include "m3e/theme/resolved_theme.hpp"

LV_FONT_DECLARE(m3e_timer_font_55);

namespace {

using m3e::generated::ColorRole;

lv_color_t color(ColorRole role) {
    const auto value =
        m3e::generated::kBaselineDarkColorScheme.get(role);
    return lv_color_make(value.red, value.green, value.blue);
}

constexpr std::uint8_t expand5(std::uint8_t value) {
    return static_cast<std::uint8_t>((value << 3) | (value >> 2));
}

constexpr std::uint8_t expand6(std::uint8_t value) {
    return static_cast<std::uint8_t>((value << 2) | (value >> 4));
}

lv_color_t exact_rgb565(
    std::uint8_t red5, std::uint8_t green6, std::uint8_t blue5) {
    return lv_color_make(expand5(red5), expand6(green6), expand5(blue5));
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
    ColorRole role) {
    auto* object = lv_label_create(parent);
    reset(object);
    lv_label_set_text(object, value);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, color(role), 0);
    return object;
}

lv_obj_t* surface(
    lv_obj_t* parent,
    std::int32_t x_dp,
    std::int32_t y_dp,
    std::int32_t width_dp,
    std::int32_t height_dp,
    std::int32_t radius_dp,
    ColorRole role) {
    auto* object = lv_obj_create(parent);
    reset(object);
    lv_obj_set_pos(object, dp(x_dp), dp(y_dp));
    lv_obj_set_size(object, dp(width_dp), dp(height_dp));
    lv_obj_set_style_radius(object, dp(radius_dp), 0);
    lv_obj_set_style_bg_color(object, color(role), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}

void foundation_story(lv_obj_t* screen) {
    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_style_bg_color(screen, color(ColorRole::background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* eyebrow = label(
        screen,
        "M3E / FOUNDATION",
        &lv_font_montserrat_10,
        ColorRole::primary);
    lv_obj_set_pos(eyebrow, dp(12), dp(10));

    auto* title = label(
        screen,
        "Material tokens",
        &lv_font_montserrat_18,
        ColorRole::on_background);
    lv_obj_set_pos(title, dp(12), dp(24));

    auto* provenance = label(
        screen,
        "Wear 1.6.2  |  RGB565",
        &lv_font_montserrat_10,
        ColorRole::on_surface_variant);
    lv_obj_set_pos(provenance, dp(12), dp(43));

    auto* low = surface(
        screen, 12, 62, 168, 22, 11, ColorRole::surface_container_low);
    auto* middle = surface(
        screen, 20, 69, 152, 22, 11, ColorRole::surface_container);
    auto* high = surface(
        screen, 28, 76, 136, 22, 11, ColorRole::surface_container_high);
    (void)low;
    (void)middle;
    auto* layer_label = label(
        high,
        "LOW  /  SURFACE  /  HIGH",
        &lv_font_montserrat_10,
        ColorRole::on_surface);
    lv_obj_center(layer_label);

    struct Swatch {
        const char* text;
        ColorRole fill;
        ColorRole content;
    };
    constexpr Swatch swatches[] = {
        {"PRIMARY", ColorRole::primary, ColorRole::on_primary},
        {"SECONDARY", ColorRole::secondary, ColorRole::on_secondary},
        {"TERTIARY", ColorRole::tertiary, ColorRole::on_tertiary},
    };
    for (std::size_t index = 0; index < 3; ++index) {
        const auto x = 12 + static_cast<std::int32_t>(index) * 57;
        auto* swatch = surface(
            screen, x, 111, 52, 38, 12, swatches[index].fill);
        auto* swatch_label = label(
            swatch,
            swatches[index].text,
            &lv_font_montserrat_10,
            swatches[index].content);
        lv_obj_center(swatch_label);
    }

    auto* shape_title = label(
        screen,
        "SHAPE  4 / 8 / 18 / FULL",
        &lv_font_montserrat_10,
        ColorRole::on_surface_variant);
    lv_obj_set_pos(shape_title, dp(12), dp(157));

    constexpr std::int32_t radii[] = {4, 8, 18, 100};
    for (std::size_t index = 0; index < 4; ++index) {
        const auto x = 12 + static_cast<std::int32_t>(index) * 42;
        surface(
            screen,
            x,
            168,
            34,
            18,
            radii[index],
            index % 2 == 0
                ? ColorRole::primary_container
                : ColorRole::secondary_container);
    }

    auto* footer = label(
        screen,
        "192dp = 240px",
        &lv_font_montserrat_10,
        ColorRole::outline);
    lv_obj_align(footer, LV_ALIGN_TOP_RIGHT, -dp(12), dp(43));
}

void stress_animation(void* object_pointer, std::int32_t value) {
    auto* object = static_cast<lv_obj_t*>(object_pointer);
    lv_obj_set_x(object, dp(12) + value);
    lv_obj_set_style_radius(object, dp(8) + value / 5, 0);
    lv_obj_invalidate(lv_screen_active());
}

void display_stress_story(lv_obj_t* screen) {
    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_style_bg_color(
        screen, color(ColorRole::surface_container_low), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* eyebrow = label(
        screen,
        "M3E / HARDWARE",
        &lv_font_montserrat_10,
        ColorRole::primary);
    lv_obj_set_pos(eyebrow, dp(12), dp(10));

    auto* title = label(
        screen,
        "Display stress",
        &lv_font_montserrat_18,
        ColorRole::on_surface);
    lv_obj_set_pos(title, dp(12), dp(24));

    auto* description = label(
        screen,
        "RGB565  |  DMA  |  40 ROWS",
        &lv_font_montserrat_10,
        ColorRole::on_surface_variant);
    lv_obj_set_pos(description, dp(12), dp(43));

    auto* stage = surface(
        screen, 12, 62, 168, 96, 18, ColorRole::surface_container);
    auto* rail_a = surface(
        stage, 10, 14, 148, 16, 8, ColorRole::primary_container);
    auto* rail_b = surface(
        stage, 10, 40, 148, 16, 8, ColorRole::secondary_container);
    auto* rail_c = surface(
        stage, 10, 66, 148, 16, 8, ColorRole::tertiary_container);
    (void)rail_a;
    (void)rail_b;
    (void)rail_c;

    auto* mover = surface(
        screen, 12, 168, 48, 16, 8, ColorRole::primary);
    auto* mover_label = label(
        mover,
        "30 FPS",
        &lv_font_montserrat_10,
        ColorRole::on_primary);
    lv_obj_center(mover_label);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, mover);
    lv_anim_set_values(&animation, 0, dp(120));
    lv_anim_set_duration(&animation, 900);
    lv_anim_set_playback_duration(&animation, 900);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&animation, stress_animation);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
    lv_anim_start(&animation);
}

m3e::ComponentFactory& component_factory() {
    static m3e::StyleRegistry registry;
    if (!registry.initialized()) {
        registry.initialize(m3e::baseline_dark_theme());
    }
    static m3e::ComponentFactory factory(registry);
    return factory;
}

lv_obj_t* story_column(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);
    auto* column = lv_obj_create(screen);
    m3e::ComponentFactory::reset(column);
    lv_obj_set_pos(column, 12, 10);
    lv_obj_set_size(column, 216, 220);
    lv_obj_set_style_pad_row(column, 4, 0);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    return column;
}

void component_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column, "CORE COMPONENTS",
        m3e::generated::TypographyRole::label_small, true);
    auto* title = factory.text(
        column, "Expressive controls",
        m3e::generated::TypographyRole::title_large);
    lv_obj_set_style_margin_bottom(title, 2, 0);
    constexpr m3e::ButtonProps group_buttons[] = {
        {"save", "Save", m3e::Tone::primary,
         m3e::ButtonVariant::filled, m3e::ComponentSize::compact, true, false},
        {"edit", "Edit", m3e::Tone::secondary,
         m3e::ButtonVariant::filled, m3e::ComponentSize::compact, true, false},
    };
    auto* group = factory.button_group(column, group_buttons, 2);
    lv_obj_set_height(group, 40);
    auto* component_card = factory.card(
        column,
        {"Today", "Bounded content.",
         m3e::Tone::neutral, true});
    lv_obj_set_height(component_card, 56);
    factory.linear_progress(
        column, {"Build", 68, 100, m3e::Tone::tertiary});
    factory.toggle_row(column, "Voice wake", true);
}

void calories_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column, "TODAY / CALORIES",
        m3e::generated::TypographyRole::label_small, true);
    auto* numeral = factory.text(
        column, "1,420",
        m3e::generated::TypographyRole::display_large);
    lv_obj_set_width(numeral, LV_PCT(100));
    lv_obj_set_style_text_align(numeral, LV_TEXT_ALIGN_CENTER, 0);
    auto* subtitle = factory.text(
        column, "of 2,000 kcal",
        m3e::generated::TypographyRole::body_small, true);
    lv_obj_set_width(subtitle, LV_PCT(100));
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    factory.linear_progress(
        column, {"Daily calories", 1420, 2000, m3e::Tone::primary});
    factory.card(
        column, {"Lunch", "Chicken bowl  |  560 kcal",
                 m3e::Tone::neutral, true});
    auto* action = factory.button(
        column, {"quick_add", "+  Quick add", m3e::Tone::primary,
                 m3e::ButtonVariant::filled, m3e::ComponentSize::compact,
                 true, false});
    lv_obj_set_width(action, LV_PCT(100));
}

void calculator_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column, "CALCULATOR",
        m3e::generated::TypographyRole::label_small, true);
    auto* result = factory.text(
        column, "42",
        m3e::generated::TypographyRole::display_large);
    lv_obj_set_width(result, LV_PCT(100));
    lv_obj_set_style_text_align(result, LV_TEXT_ALIGN_RIGHT, 0);
    constexpr const char* keys[] = {
        "7", "8", "9", "/", "4", "5", "6", "*",
        "1", "2", "3", "-", "0", ".", "=", "+",
    };
    auto* grid = lv_obj_create(column);
    m3e::ComponentFactory::reset(grid);
    lv_obj_set_size(grid, LV_PCT(100), 152);
    static std::int32_t columns[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    static std::int32_t rows[] = {
        LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
        LV_GRID_TEMPLATE_LAST,
    };
    lv_obj_set_grid_dsc_array(grid, columns, rows);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, 4, 0);
    for (std::size_t index = 0; index < 16; ++index) {
        const auto operation = index % 4 == 3 || index == 14;
        auto* key = factory.button(
            grid, {"key", keys[index],
                   operation ? m3e::Tone::secondary : m3e::Tone::neutral,
                   m3e::ButtonVariant::filled, m3e::ComponentSize::compact,
                   true, false});
        lv_obj_set_grid_cell(
            key, LV_GRID_ALIGN_STRETCH, static_cast<std::int32_t>(index % 4), 1,
            LV_GRID_ALIGN_STRETCH, static_cast<std::int32_t>(index / 4), 1);
    }
}

void workout_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column, "WORKOUT / SQUAT",
        m3e::generated::TypographyRole::label_small, true);
    auto* title = factory.text(
        column, "Set 3 of 5",
        m3e::generated::TypographyRole::title_large);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    factory.stepper(
        column, {"Weight", 135, "lb", false, false});
    factory.card(
        column, {"8 reps", "Rest timer  0:38",
                 m3e::Tone::neutral, false});
    auto* done = factory.button(
        column, {"complete_set", "Complete set", m3e::Tone::primary,
                 m3e::ButtonVariant::filled, m3e::ComponentSize::compact,
                 true, false});
    lv_obj_set_width(done, LV_PCT(100));
}

void inputs_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column, "INPUT CONTROLS",
        m3e::generated::TypographyRole::label_small, true);
    factory.slider(column, 65, 0, 100, 5);
    factory.selection_row(
        column, "Remember choice", m3e::SelectionKind::checkbox, true);
    factory.selection_row(
        column, "Primary option", m3e::SelectionKind::radio, true);
    factory.selection_row(
        column, "Voice wake", m3e::SelectionKind::switch_control, true);
}

void voice_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* content = factory.screen_scaffold(screen, "10:09", false);
    lv_obj_set_flex_align(
        content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    factory.text(
        content, "What can I do?",
        m3e::generated::TypographyRole::title_large);
    factory.voice_orb(content, "LISTENING");
    auto* transcript = factory.text(
        content, "Listening...",
        m3e::generated::TypographyRole::body_medium, true);
    lv_obj_set_width(transcript, LV_PCT(100));
    lv_obj_set_style_text_align(transcript, LV_TEXT_ALIGN_CENTER, 0);
    factory.page_indicator(content, 3, 1);
}

void navigation_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.list_header(column, "Schedule", true);
    auto* summary = lv_obj_create(column);
    m3e::ComponentFactory::reset(summary);
    lv_obj_set_size(summary, LV_PCT(100), 68);
    lv_obj_set_style_pad_column(summary, 10, 0);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        summary,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    factory.segmented_circular_progress(
        summary, {"Progress", 4, 7, m3e::Tone::primary}, 7);
    auto* labels = lv_obj_create(summary);
    m3e::ComponentFactory::reset(labels);
    lv_obj_set_flex_grow(labels, 1);
    lv_obj_set_height(labels, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(labels, LV_FLEX_FLOW_COLUMN);
    factory.text(
        labels,
        "4 of 7",
        m3e::generated::TypographyRole::title_medium);
    factory.text(
        labels,
        "steps complete",
        m3e::generated::TypographyRole::body_extra_small,
        true);
    constexpr const char* hours[] = {"08", "09", "10"};
    constexpr const char* minutes[] = {"00", "15", "30"};
    const m3e::PickerProps pickers[] = {
        {hours, 3, 1},
        {minutes, 3, 2},
    };
    auto* picker_group = factory.picker_group(column, pickers, 2);
    lv_obj_set_height(picker_group, 108);
    for (std::uint32_t index = 0;
         index < lv_obj_get_child_count(picker_group);
         ++index) {
        lv_obj_set_height(lv_obj_get_child(picker_group, index), 108);
    }
}

void system_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "SYSTEM / GENERATED APP",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* review = factory.change_review(
        column,
        {"Lunch", "Calories", "520 kcal", "560 kcal"});
    lv_obj_set_height(review, 68);
    auto* progress = factory.build_progress(
        column,
        {"Compiling app", 4, 9, false});
    lv_obj_set_height(progress, 48);
    auto* permission = factory.permission_review(
        column,
        "Add nutrition entries",
        "Only when you confirm.");
    lv_obj_set_height(permission, 66);
}

void os_home_story(lv_obj_t* screen) {
    m3e_system_shell_home_model_t model{};
    m3e_system_shell_default_home_model(&model);
    m3e_system_shell_show_home(screen, &model, nullptr);
}

void os_live_cards_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);
    auto* title = factory.text(
        screen,
        "LIVE CARDS",
        m3e::generated::TypographyRole::label_small,
        true);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 9);

    auto* card = factory.live_card(
        screen,
        {
            "Timer",
            "0:05",
            "Tea  •  running",
            "NOW",
            55,
            60,
            m3e::Tone::tertiary,
        });
    lv_obj_set_pos(card, 12, 34);
    lv_obj_set_size(card, 216, 146);
    auto* pause = factory.button(
        screen,
        {
            "timer.pause",
            "Pause",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_pos(pause, 55, 188);
    lv_obj_set_size(pause, 130, 38);
    auto* pager = factory.page_indicator(screen, 3, 0);
    lv_obj_align(pager, LV_ALIGN_BOTTOM_MID, 0, -3);
}

void os_launcher_story(lv_obj_t* screen) {
    constexpr m3e_system_shell_launcher_item_t items[] = {
        {"dev.doodad.timer", "Timer", "1 active",
         M3E_SYSTEM_SHELL_TONE_PRIMARY},
        {"dev.doodad.weather", "Weather", "72°  •  Partly cloudy",
         M3E_SYSTEM_SHELL_TONE_SECONDARY},
        {"dev.doodad.workout", "Workout", "Paused  •  Set 4",
         M3E_SYSTEM_SHELL_TONE_TERTIARY},
    };
    m3e_system_shell_show_launcher(screen, items, 3, nullptr);
}

void os_control_center_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "CONTROL CENTER",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* wifi = factory.status_chip(
        column,
        "Wi-Fi  connected",
        m3e::IconName::check,
        m3e::Tone::primary);
    lv_obj_set_width(wifi, LV_PCT(100));
    auto* battery = factory.status_chip(
        column,
        "Battery  82%  •  1d 4h",
        m3e::IconName::information,
        m3e::Tone::secondary);
    lv_obj_set_width(battery, LV_PCT(100));
    factory.text(
        column,
        "BRIGHTNESS",
        m3e::generated::TypographyRole::label_small,
        true);
    factory.slider(column, 64, 0, 100, 10);
    auto* voice = factory.toggle_row(column, "Voice wake", true);
    lv_obj_set_height(voice, 44);
    auto* manager = factory.button(
        column,
        {
            "system.app-manager",
            "App manager",
            m3e::Tone::neutral,
            m3e::ButtonVariant::tonal,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_width(manager, LV_PCT(100));
}

void os_app_manager_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "APP MANAGER  •  20",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* title = factory.text(
        column,
        "Packages",
        m3e::generated::TypographyRole::title_large);
    lv_obj_set_style_margin_bottom(title, 2, 0);
    auto* installed = factory.card(
        column,
        {"Timer  0.1.0", "Installed  •  84 KB",
         m3e::Tone::primary, true});
    lv_obj_set_height(installed, 54);
    auto* update = factory.card(
        column,
        {"Weather  0.2.0", "Update ready  •  mocked",
         m3e::Tone::secondary, true});
    lv_obj_set_height(update, 54);
    auto* rollback = factory.card(
        column,
        {"Tasks  0.1.0", "Rollback available",
         m3e::Tone::tertiary, true});
    lv_obj_set_height(rollback, 54);
    auto* storage = factory.linear_progress(
        column,
        {"Onboard app storage", 3, 9, m3e::Tone::primary});
    lv_obj_set_height(storage, 34);
}

void os_app_detail_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "APP MANAGER / TIMER",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* health = factory.card(
        column,
        {"Timer  0.1.0", "Healthy  •  84 KB  •  gen 7",
         m3e::Tone::primary, false});
    lv_obj_set_height(health, 50);
    auto* surfaces = factory.card(
        column,
        {"Declared surfaces", "A  G  C  N  O  V",
         m3e::Tone::secondary, false});
    lv_obj_set_height(surfaces, 50);
    auto* permissions = factory.card(
        column,
        {"Exact alarms + haptics", "Only while a timer exists",
         m3e::Tone::neutral, false});
    lv_obj_set_height(permissions, 50);
}

void os_install_progress_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "ATOMIC UPDATE",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* progress = factory.build_progress(
        column,
        {"Health check", 4, 6, true});
    lv_obj_set_height(progress, 84);
    auto* safety = factory.card(
        column,
        {"Last known good retained", "Timer 0.1.0 remains active until commit",
         m3e::Tone::primary, false});
    lv_obj_set_height(safety, 68);
    auto* cancel = factory.button(
        column,
        {
            "install.cancel",
            "Cancel update",
            m3e::Tone::neutral,
            m3e::ButtonVariant::outlined,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_width(cancel, LV_PCT(100));
}

void os_crash_recovery_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "SAFE MODE",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* crash = factory.card(
        column,
        {"Weather quarantined", "3 traps  •  guest stopped",
         m3e::Tone::error, false});
    lv_obj_set_height(crash, 64);
    auto* home = factory.card(
        column,
        {"Home is still available", "Broken cards are hidden",
         m3e::Tone::primary, false});
    lv_obj_set_height(home, 64);
    auto* rollback = factory.button(
        column,
        {
            "recovery.rollback",
            "Restore last known good",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::normal,
            true,
            false,
        });
    lv_obj_set_width(rollback, LV_PCT(100));
    auto* details = factory.button(
        column,
        {
            "recovery.details",
            "View crash telemetry",
            m3e::Tone::neutral,
            m3e::ButtonVariant::text,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_width(details, LV_PCT(100));
}

void os_notification_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    os_home_story(screen);
    auto* panel = factory.card(
        screen,
        {"Maya", "Dinner at 7?  •  Reply available",
         m3e::Tone::secondary, true});
    lv_obj_set_pos(panel, 12, 116);
    lv_obj_set_size(panel, 216, 78);
    auto* reply = factory.button(
        screen,
        {
            "notification.reply",
            "Quick reply",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_pos(reply, 55, 198);
    lv_obj_set_size(reply, 130, 34);
}

void os_permission_review_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "TRUSTED PERMISSION REVIEW",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* review = factory.permission_review(
        column,
        "Microphone while recording",
        "Only while this screen is recording.");
    lv_obj_set_height(review, 102);
    auto* allow = factory.button(
        column,
        {
            "permission.allow",
            "Allow while recording",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::normal,
            true,
            false,
        });
    lv_obj_set_width(allow, LV_PCT(100));
    auto* deny = factory.button(
        column,
        {
            "permission.deny",
            "Not now",
            m3e::Tone::neutral,
            m3e::ButtonVariant::outlined,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_width(deny, LV_PCT(100));
}

void os_action_review_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "TRUSTED ACTION REVIEW",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* review = factory.change_review(
        column,
        {"Front door", "State", "Locked", "Unlock"});
    lv_obj_set_height(review, 82);
    auto* warning = factory.card(
        column,
        {"Always confirm", "Physical access",
         m3e::Tone::error, false});
    lv_obj_set_height(warning, 60);
    auto* confirm = factory.button(
        column,
        {
            "action.confirm",
            "Confirm unlock",
            m3e::Tone::error,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::normal,
            true,
            false,
        });
    lv_obj_set_width(confirm, LV_PCT(100));
}

void os_error_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "SYSTEM RECOVERY",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* failure = factory.card(
        column,
        {"Provider unavailable", "Cache safe  •  nothing committed",
         m3e::Tone::error, false});
    lv_obj_set_height(failure, 80);
    auto* retry = factory.button(
        column,
        {
            "error.retry",
            "Retry safely",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::normal,
            true,
            false,
        });
    lv_obj_set_width(retry, LV_PCT(100));
    auto* home = factory.button(
        column,
        {
            "error.home",
            "Return Home",
            m3e::Tone::neutral,
            m3e::ButtonVariant::outlined,
            m3e::ComponentSize::compact,
            true,
            false,
        });
    lv_obj_set_width(home, LV_PCT(100));
}

void os_voice_phase_story(
    lv_obj_t* screen,
    const char* status,
    const char* transcript,
    m3e::Tone tone) {
    auto& factory = component_factory();
    os_home_story(screen);
    factory.voice_overlay(screen, status, transcript, tone);
}

void os_voice_story(lv_obj_t* screen) {
    os_voice_phase_story(
        screen,
        "LISTENING",
        "Set a timer for five minutes",
        m3e::Tone::primary);
}

void os_voice_thinking_story(lv_obj_t* screen) {
    os_voice_phase_story(
        screen,
        "THINKING",
        "Checking the exact scheduler...",
        m3e::Tone::secondary);
}

void os_voice_review_story(lv_obj_t* screen) {
    os_voice_phase_story(
        screen,
        "REVIEW",
        "Start a five minute timer?",
        m3e::Tone::tertiary);
}

void os_voice_build_story(lv_obj_t* screen) {
    os_voice_phase_story(
        screen,
        "BUILDING APP  4 / 9",
        "Compiling bounded Wasm package…",
        m3e::Tone::secondary);
}

void os_voice_result_story(lv_obj_t* screen) {
    os_voice_phase_story(
        screen,
        "DONE",
        "Timer started  •  5:00",
        m3e::Tone::primary);
}

lv_obj_t* find_action(lv_obj_t* root, const char* id) {
    if (root == nullptr || id == nullptr) return nullptr;
    const auto* object_id = static_cast<const char*>(lv_obj_get_user_data(root));
    if (object_id != nullptr && std::strcmp(object_id, id) == 0) return root;
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        if (auto* found = find_action(lv_obj_get_child(root, index), id)) {
            return found;
        }
    }
    return nullptr;
}

void transforming_list_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);
    static constexpr m3e::TransformingListItem items[] = {
        {"activity.0", "Morning walk", "28 minutes", m3e::Tone::primary},
        {"activity.1", "Breakfast", "420 kcal", m3e::Tone::neutral},
        {"activity.2", "Hydration", "3 of 8 cups", m3e::Tone::secondary},
        {"activity.3", "Lunch", "560 kcal", m3e::Tone::neutral},
        {"activity.4", "Strength", "5 sets", m3e::Tone::tertiary},
        {"activity.5", "Afternoon walk", "18 minutes", m3e::Tone::primary},
        {"activity.6", "Dinner", "610 kcal", m3e::Tone::neutral},
        {"activity.7", "Daily review", "On track", m3e::Tone::secondary},
        {"activity.8", "Wind down", "10:00 PM", m3e::Tone::neutral},
        {"activity.9", "Sleep goal", "8 hours", m3e::Tone::tertiary},
    };
    auto* list = factory.transforming_list(
        screen,
        items,
        static_cast<std::uint16_t>(
            sizeof(items) / sizeof(items[0])));
    lv_obj_set_pos(list, 12, 10);
    lv_obj_set_size(list, 216, 220);
    lv_obj_update_layout(list);
    lv_obj_send_event(list, LV_EVENT_SIZE_CHANGED, nullptr);
}

void expressive_depth_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    auto* column = story_column(screen);
    factory.text(
        column,
        "EXPRESSIVE DEPTH",
        m3e::generated::TypographyRole::label_small,
        true);
    auto* status = factory.status_chip(
        column,
        "Server connected",
        m3e::IconName::check,
        m3e::Tone::primary);
    lv_obj_set_width(status, LV_PCT(100));
    auto* glance = factory.live_card(
        column,
        {
            "Calories",
            "1,420 kcal",
            "580 remaining",
            nullptr,
            1420,
            2000,
            m3e::Tone::secondary,
        });
    lv_obj_set_height(glance, 96);
    auto* split = factory.split_selection_row(
        column,
        "Voice wake",
        m3e::SelectionKind::switch_control,
        true);
    lv_obj_set_height(split, 60);
}

void hydration_mockup_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);

    auto* eyebrow = factory.text(
        screen,
        "HYDRATION",
        m3e::generated::TypographyRole::label_small,
        true);
    lv_obj_align(eyebrow, LV_ALIGN_TOP_MID, 0, 10);

    auto* title = factory.text(
        screen,
        "5 cups",
        m3e::generated::TypographyRole::title_large);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    auto* progress = factory.segmented_circular_progress(
        screen,
        {"Daily goal", 5, 8, m3e::Tone::primary},
        8);
    lv_obj_set_size(progress, 82, 82);
    lv_obj_align(progress, LV_ALIGN_TOP_MID, 0, 56);
    for (std::uint32_t index = 0;
         index < lv_obj_get_child_count(progress);
         ++index) {
        auto* segment = lv_obj_get_child(progress, index);
        lv_obj_set_size(segment, 82, 82);
        lv_obj_center(segment);
        lv_obj_set_style_arc_width(segment, 8, LV_PART_MAIN);
        lv_obj_set_style_arc_width(segment, 8, LV_PART_INDICATOR);
    }
    auto* percentage = factory.text(
        screen,
        "63%",
        m3e::generated::TypographyRole::title_medium);
    lv_obj_align(percentage, LV_ALIGN_TOP_MID, 0, 86);

    auto* pace = factory.status_chip(
        screen,
        "On pace",
        m3e::IconName::check,
        m3e::Tone::secondary);
    lv_obj_align(pace, LV_ALIGN_TOP_MID, 0, 143);

    auto* remaining = factory.text(
        screen,
        "3 cups to your goal",
        m3e::generated::TypographyRole::body_small,
        true);
    lv_obj_align(remaining, LV_ALIGN_TOP_MID, 0, 178);

    constexpr m3e::ButtonProps actions[] = {
        {
            "hydration.log",
            "+  Log cup",
            m3e::Tone::primary,
            m3e::ButtonVariant::filled,
            m3e::ComponentSize::compact,
            true,
            false,
        },
        {
            "hydration.history",
            "History",
            m3e::Tone::neutral,
            m3e::ButtonVariant::tonal,
            m3e::ComponentSize::compact,
            true,
            false,
        },
    };
    auto* group = factory.button_group(screen, actions, 2, 0);
    lv_obj_set_pos(group, 12, 199);
    lv_obj_set_size(group, 216, 40);
}

void focus_mockup_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);

    auto* eyebrow = factory.text(
        screen,
        "DEEP WORK",
        m3e::generated::TypographyRole::label_small,
        true);
    lv_obj_align(eyebrow, LV_ALIGN_TOP_MID, 0, 10);

    auto* arc = factory.circular_progress(
        screen,
        {"Focus session", 1472, 1500, m3e::Tone::tertiary});
    lv_obj_set_size(arc, 148, 148);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 31);

    auto* timer = factory.animated_text(
        screen,
        "24:32",
        m3e::generated::TypographyRole::display_large);
    lv_obj_align(timer, LV_ALIGN_TOP_MID, 0, 84);
    auto* task = factory.text(
        screen,
        "Prototype review",
        m3e::generated::TypographyRole::body_small,
        true);
    lv_obj_align(task, LV_ALIGN_TOP_MID, 0, 111);

    auto* pause = factory.icon_button(
        screen,
        "focus.pause",
        m3e::IconName::pause,
        m3e::ButtonVariant::filled,
        m3e::ComponentSize::normal);
    lv_obj_set_pos(pause, 62, 175);
    auto* stop = factory.icon_button(
        screen,
        "focus.stop",
        m3e::IconName::close,
        m3e::ButtonVariant::tonal,
        m3e::ComponentSize::normal);
    lv_obj_set_pos(stop, 126, 175);

    auto* footer = factory.text(
        screen,
        "28 seconds remaining",
        m3e::generated::TypographyRole::body_extra_small,
        true);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -5);
}

void travel_mockup_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);

    auto* eyebrow = factory.text(
        screen,
        "TRAVEL DAY",
        m3e::generated::TypographyRole::label_small,
        true);
    lv_obj_set_pos(eyebrow, 14, 10);
    auto* status = factory.status_chip(
        screen,
        "On time",
        m3e::IconName::check,
        m3e::Tone::primary);
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, -12, 5);

    auto* flight = factory.live_card(
        screen,
        {
            "UA 216",
            "SFO  >  JFK",
            "Boards 6:20 PM  •  Gate F12",
            "Updated now",
            0,
            0,
            m3e::Tone::secondary,
        });
    lv_obj_set_pos(flight, 12, 44);
    lv_obj_set_size(flight, 216, 100);

    auto* hotel = factory.card(
        screen,
        {
            "Arlo Midtown",
            "Check-in after 3 PM",
            m3e::Tone::neutral,
            true,
        });
    lv_obj_set_pos(hotel, 12, 151);
    lv_obj_set_size(hotel, 216, 58);

    auto* indicator = factory.page_indicator(screen, 3, 0);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void music_mockup_story(lv_obj_t* screen) {
    auto& factory = component_factory();
    factory.screen(screen);

    auto* eyebrow = factory.text(
        screen,
        "NOW PLAYING",
        m3e::generated::TypographyRole::label_small,
        true);
    lv_obj_align(eyebrow, LV_ALIGN_TOP_MID, 0, 10);

    auto* track = factory.card(
        screen,
        {
            "Midnight City",
            "M83  •  Hurry Up, We're Dreaming",
            m3e::Tone::tertiary,
            false,
        });
    lv_obj_set_pos(track, 12, 31);
    lv_obj_set_size(track, 216, 74);

    auto* slider = factory.slider(screen, 138, 0, 242, 0);
    lv_obj_set_pos(slider, 24, 127);
    lv_obj_set_width(slider, 192);
    auto* elapsed = factory.text(
        screen,
        "2:18",
        m3e::generated::TypographyRole::body_extra_small,
        true);
    lv_obj_set_pos(elapsed, 24, 143);
    auto* duration = factory.text(
        screen,
        "4:02",
        m3e::generated::TypographyRole::body_extra_small,
        true);
    lv_obj_align(duration, LV_ALIGN_TOP_RIGHT, -24, 143);

    auto* previous = factory.icon_button(
        screen,
        "music.previous",
        m3e::IconName::back,
        m3e::ButtonVariant::text,
        m3e::ComponentSize::compact);
    lv_obj_set_pos(previous, 49, 166);
    auto* play = factory.icon_button(
        screen,
        "music.pause",
        m3e::IconName::pause,
        m3e::ButtonVariant::filled,
        m3e::ComponentSize::normal);
    lv_obj_set_pos(play, 94, 160);
    auto* next = factory.icon_button(
        screen,
        "music.next",
        m3e::IconName::next,
        m3e::ButtonVariant::text,
        m3e::ComponentSize::compact);
    lv_obj_set_pos(next, 151, 166);

    auto* output = factory.status_chip(
        screen,
        "Living room",
        m3e::IconName::play,
        m3e::Tone::neutral);
    lv_obj_align(output, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void color_bars_story(lv_obj_t* screen) {
    // A label-free calibration target with a white registration frame. The
    // frame lets the camera lane find the app viewport independently of the
    // CoreS3's physical side gutters. The inner 224×220 area divides exactly
    // into 8 columns × 5 rows. Values are authored as native RGB565 channel
    // codes, then expanded exactly for LVGL.
    struct Patch {
        std::uint8_t red5;
        std::uint8_t green6;
        std::uint8_t blue5;
    };
    constexpr Patch bars[] = {
        {31, 63, 31},  // white
        {31, 63, 0},   // yellow
        {0, 63, 31},   // cyan
        {0, 63, 0},    // green
        {31, 0, 31},   // magenta
        {31, 0, 0},    // red
        {0, 0, 31},    // blue
        {0, 0, 0},     // black
    };
    constexpr std::uint8_t ramp5[] = {0, 4, 9, 13, 18, 22, 27, 31};
    constexpr std::uint8_t ramp6[] = {0, 9, 18, 27, 36, 45, 54, 63};

    lv_obj_clean(screen);
    reset(screen);
    lv_obj_set_size(screen, 240, 240);
    lv_obj_set_style_bg_color(screen, exact_rgb565(31, 63, 31), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    for (std::size_t row = 0; row < 5; ++row) {
        for (std::size_t column = 0; column < 8; ++column) {
            Patch patch{};
            switch (row) {
                case 0:
                    patch = bars[column];
                    break;
                case 1:
                    patch = {
                        ramp5[column],
                        ramp6[column],
                        ramp5[column],
                    };
                    break;
                case 2:
                    patch = {ramp5[column], 0, 0};
                    break;
                case 3:
                    patch = {0, ramp6[column], 0};
                    break;
                case 4:
                    patch = {0, 0, ramp5[column]};
                    break;
            }
            auto* swatch = lv_obj_create(screen);
            reset(swatch);
            lv_obj_set_pos(
                swatch,
                8 + static_cast<std::int32_t>(column) * 28,
                10 + static_cast<std::int32_t>(row) * 44);
            lv_obj_set_size(swatch, 28, 44);
            lv_obj_set_style_radius(swatch, 0, 0);
            lv_obj_set_style_bg_color(
                swatch,
                exact_rgb565(patch.red5, patch.green6, patch.blue5),
                0);
            lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        }
    }
}

}  // namespace

extern "C" void m3e_catalog_show(lv_obj_t* screen, int story) {
    switch (story) {
        case M3E_CATALOG_STORY_COMPONENTS:
            component_story(screen);
            break;
        case M3E_CATALOG_STORY_CALORIES:
            calories_story(screen);
            break;
        case M3E_CATALOG_STORY_CALCULATOR:
            calculator_story(screen);
            break;
        case M3E_CATALOG_STORY_WORKOUT:
            workout_story(screen);
            break;
        case M3E_CATALOG_STORY_INPUTS:
            inputs_story(screen);
            break;
        case M3E_CATALOG_STORY_VOICE:
            voice_story(screen);
            break;
        case M3E_CATALOG_STORY_NAVIGATION:
            navigation_story(screen);
            break;
        case M3E_CATALOG_STORY_SYSTEM:
            system_story(screen);
            break;
        case M3E_CATALOG_STORY_TRANSFORMING_LIST:
            transforming_list_story(screen);
            break;
        case M3E_CATALOG_STORY_EXPRESSIVE_DEPTH:
            expressive_depth_story(screen);
            break;
        case M3E_CATALOG_STORY_MOCKUP_HYDRATION:
            hydration_mockup_story(screen);
            break;
        case M3E_CATALOG_STORY_MOCKUP_FOCUS:
            focus_mockup_story(screen);
            break;
        case M3E_CATALOG_STORY_MOCKUP_TRAVEL:
            travel_mockup_story(screen);
            break;
        case M3E_CATALOG_STORY_MOCKUP_MUSIC:
            music_mockup_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_HOME:
            os_home_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_LIVE_CARDS:
            os_live_cards_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_LAUNCHER:
            os_launcher_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_CONTROL_CENTER:
            os_control_center_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_APP_MANAGER:
            os_app_manager_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_VOICE:
            os_voice_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_APP_DETAIL:
            os_app_detail_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_INSTALL_PROGRESS:
            os_install_progress_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_CRASH_RECOVERY:
            os_crash_recovery_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_NOTIFICATION:
            os_notification_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_PERMISSION_REVIEW:
            os_permission_review_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_ACTION_REVIEW:
            os_action_review_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_ERROR:
            os_error_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_VOICE_THINKING:
            os_voice_thinking_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_VOICE_REVIEW:
            os_voice_review_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_VOICE_BUILD:
            os_voice_build_story(screen);
            break;
        case M3E_CATALOG_STORY_OS_VOICE_RESULT:
            os_voice_result_story(screen);
            break;
        case M3E_CATALOG_STORY_COLOR_BARS:
            color_bars_story(screen);
            break;
        case M3E_CATALOG_STORY_DISPLAY_STRESS:
            display_stress_story(screen);
            break;
        case M3E_CATALOG_STORY_FOUNDATION:
        default:
            foundation_story(screen);
            break;
    }
}

extern "C" void m3e_catalog_show_voice_runtime(
    lv_obj_t* screen,
    int phase,
    const char* transcript,
    const char* response,
    m3e_voice_runtime_view_t* view) {
    if (view != nullptr) *view = {};
    if (screen == nullptr) return;

    const char* status = "READY";
    const char* detail = "Tap to talk";
    m3e::Tone tone = m3e::Tone::neutral;
    auto orb_state = m3e::VoiceOrbState::idle;
    bool primary_enabled = true;
    switch (phase) {
        case 1:
            status = "LISTENING";
            detail = transcript != nullptr && transcript[0] != '\0'
                ? transcript : "Speak now...";
            tone = m3e::Tone::primary;
            orb_state = m3e::VoiceOrbState::listening;
            break;
        case 2:
        case 4:
            status = "THINKING";
            detail = transcript != nullptr && transcript[0] != '\0'
                ? transcript : "Working on it...";
            tone = m3e::Tone::secondary;
            orb_state = m3e::VoiceOrbState::thinking;
            primary_enabled = false;
            break;
        case 3:
            status = "SPEAKING";
            detail = response != nullptr && response[0] != '\0'
                ? response : "Replying...";
            tone = m3e::Tone::primary;
            orb_state = m3e::VoiceOrbState::speaking;
            break;
        case 5:
            status = "UNAVAILABLE";
            detail = response != nullptr && response[0] != '\0'
                ? response : "Voice connection lost";
            tone = m3e::Tone::error;
            orb_state = m3e::VoiceOrbState::error;
            primary_enabled = false;
            break;
        case 0:
        case 6:
        default:
            break;
    }

    os_home_story(screen);
    auto& factory = component_factory();
    auto* overlay = factory.voice_overlay(
        screen, status, detail, tone, orb_state);
    auto* primary = find_action(overlay, "voice.primary");
    auto* cancel = find_action(overlay, "voice.cancel");
    if (!primary_enabled && primary != nullptr) {
        lv_obj_add_state(primary, LV_STATE_DISABLED);
    }
    if (view != nullptr) {
        view->primary_action = primary_enabled ? primary : nullptr;
        view->cancel_action = cancel;
        view->level_ring = primary;
    }
}
