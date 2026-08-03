#pragma once

#include <cstddef>
#include <cstdint>

#include "lvgl.h"
#include "m3e/generated/core_tokens.hpp"
#include "m3e/theme/style_registry.hpp"

namespace m3e {

enum class Tone : std::uint8_t {
    primary,
    secondary,
    tertiary,
    neutral,
    error,
};

enum class ButtonVariant : std::uint8_t {
    filled,
    tonal,
    outlined,
    text,
};

enum class ComponentSize : std::uint8_t {
    compact,
    normal,
    large,
};

enum class SelectionKind : std::uint8_t {
    checkbox,
    radio,
    switch_control,
};

enum class IconName : std::uint8_t {
    add,
    remove,
    check,
    close,
    microphone,
    play,
    pause,
    back,
    next,
    warning,
    information,
    settings,
};

enum class VoiceOrbState : std::uint8_t {
    idle,
    listening,
    thinking,
    speaking,
    error,
};

struct ButtonProps {
    const char* id;
    const char* label;
    Tone tone;
    ButtonVariant variant;
    ComponentSize size;
    bool enabled;
    bool selected;
};

struct CardProps {
    const char* title;
    const char* body;
    Tone tone;
    bool clickable;
};

struct ProgressProps {
    const char* label;
    std::int32_t value;
    std::int32_t maximum;
    Tone tone;
};

struct StepperProps {
    const char* label;
    std::int32_t value;
    const char* unit;
    bool at_minimum;
    bool at_maximum;
};

struct PickerProps {
    const char* const* items;
    std::uint8_t count;
    std::uint8_t selected_index;
};

struct ChangeReviewProps {
    const char* entity;
    const char* field;
    const char* old_value;
    const char* new_value;
};

struct BuildProgressProps {
    const char* stage;
    std::uint8_t stage_index;
    std::uint8_t stage_count;
    bool cancellable;
};

struct LiveCardProps {
    const char* app_name;
    const char* primary;
    const char* secondary;
    const char* freshness;
    std::int32_t progress;
    std::int32_t progress_maximum;
    Tone tone;
};

struct TransformingListItem {
    const char* id;
    const char* primary;
    const char* secondary;
    Tone tone;
};

class ComponentFactory {
 public:
    explicit ComponentFactory(StyleRegistry& styles);

    lv_obj_t* screen(lv_obj_t* root);
    lv_obj_t* text(
        lv_obj_t* parent,
        const char* value,
        generated::TypographyRole role,
        bool muted = false);
    lv_obj_t* animated_text(
        lv_obj_t* parent,
        const char* value,
        generated::TypographyRole role);
    bool set_animated_text(
        lv_obj_t* object,
        const char* value,
        bool incrementing,
        bool reduced_motion = false);
    lv_obj_t* icon(
        lv_obj_t* parent,
        IconName name,
        std::int32_t size = 24,
        bool muted = false);
    lv_obj_t* button(lv_obj_t* parent, const ButtonProps& props);
    lv_obj_t* icon_button(
        lv_obj_t* parent,
        const char* id,
        IconName icon,
        ButtonVariant variant = ButtonVariant::filled,
        ComponentSize size = ComponentSize::normal,
        bool enabled = true,
        bool selected = false);
    lv_obj_t* icon_toggle_button(
        lv_obj_t* parent,
        const char* id,
        IconName icon,
        bool selected,
        ButtonVariant variant = ButtonVariant::tonal,
        ComponentSize size = ComponentSize::normal,
        bool enabled = true);
    lv_obj_t* text_toggle_button(
        lv_obj_t* parent,
        const ButtonProps& props);
    lv_obj_t* card(lv_obj_t* parent, const CardProps& props);
    lv_obj_t* list_header(
        lv_obj_t* parent,
        const char* value,
        bool subheader = false);
    lv_obj_t* linear_progress(lv_obj_t* parent, const ProgressProps& props);
    lv_obj_t* circular_progress(lv_obj_t* parent, const ProgressProps& props);
    lv_obj_t* segmented_circular_progress(
        lv_obj_t* parent,
        const ProgressProps& props,
        std::uint8_t segment_count);
    lv_obj_t* toggle_row(
        lv_obj_t* parent,
        const char* label,
        bool checked,
        bool enabled = true);
    lv_obj_t* stepper(lv_obj_t* parent, const StepperProps& props);
    lv_obj_t* button_group(
        lv_obj_t* parent,
        const ButtonProps* buttons,
        std::uint8_t count,
        std::int8_t emphasized_index = -1,
        bool reduced_motion = false);
    lv_obj_t* selection_row(
        lv_obj_t* parent,
        const char* label,
        SelectionKind kind,
        bool checked,
        bool enabled = true);
    lv_obj_t* slider(
        lv_obj_t* parent,
        std::int32_t value,
        std::int32_t minimum,
        std::int32_t maximum,
        std::uint8_t steps = 0);
    lv_obj_t* screen_scaffold(
        lv_obj_t* root,
        const char* time_text,
        bool show_time = true);
    lv_obj_t* app_scaffold(
        lv_obj_t* root,
        const char* time_text,
        bool show_time = true);
    lv_obj_t* time_text(
        lv_obj_t* parent,
        const char* value,
        const char* leading_status = nullptr);
    lv_obj_t* picker(
        lv_obj_t* parent,
        const PickerProps& props);
    lv_obj_t* picker_group(
        lv_obj_t* parent,
        const PickerProps* columns,
        std::uint8_t column_count);
    lv_obj_t* date_picker(
        lv_obj_t* parent,
        std::int32_t year,
        std::uint8_t month,
        std::uint8_t day);
    lv_obj_t* time_picker(
        lv_obj_t* parent,
        std::uint8_t hour,
        std::uint8_t minute,
        bool use_24_hour);
    lv_obj_t* horizontal_pager(
        lv_obj_t* parent,
        std::uint8_t page_count,
        std::uint8_t selected_page);
    lv_obj_t* horizontal_pager_scaffold(
        lv_obj_t* parent,
        std::uint8_t page_count,
        std::uint8_t selected_page,
        const char* time_value = nullptr);
    lv_obj_t* animated_page(lv_obj_t* parent);
    static lv_obj_t* animated_page_slot(
        lv_obj_t* object,
        std::uint8_t index);
    static bool show_animated_page(
        lv_obj_t* object,
        std::uint8_t index,
        bool forward,
        bool reduced_motion = false);
    lv_obj_t* fading_expanding_label(
        lv_obj_t* parent,
        const char* value,
        std::int32_t collapsed_height = 36,
        bool expanded = false);
    static bool set_fading_expanding_label_expanded(
        lv_obj_t* object,
        bool expanded,
        bool reduced_motion = false);
    lv_obj_t* swipe_to_dismiss_box(lv_obj_t* parent);
    lv_obj_t* swipe_to_reveal(
        lv_obj_t* parent,
        const char* primary_action,
        const char* secondary_action = nullptr);
    lv_obj_t* split_selection_row(
        lv_obj_t* parent,
        const char* label,
        SelectionKind kind,
        bool checked,
        bool enabled = true);
    lv_obj_t* page_indicator(
        lv_obj_t* parent,
        std::uint8_t page_count,
        std::uint8_t selected_page);
    lv_obj_t* scroll_indicator(
        lv_obj_t* parent,
        std::int32_t position,
        std::int32_t maximum);
    lv_obj_t* level_indicator(
        lv_obj_t* parent,
        std::int32_t value,
        std::int32_t maximum,
        Tone tone = Tone::primary);
    lv_obj_t* loading_placeholder(
        lv_obj_t* parent,
        std::int32_t width,
        std::int32_t height,
        bool reduced_motion = false);
    static bool morph_shape_state(
        lv_obj_t* object,
        bool selected,
        bool reduced_motion = false);
    lv_obj_t* transforming_list(
        lv_obj_t* parent,
        const TransformingListItem* items,
        std::uint16_t count,
        bool reduced_motion = false);
    static std::size_t transforming_list_mounted_count(
        lv_obj_t* object);
    static bool update_transforming_list(
        lv_obj_t* object,
        const TransformingListItem* items,
        std::uint16_t count);
    lv_obj_t* dialog(
        lv_obj_t* root,
        const char* title,
        const char* body,
        const char* confirm_label,
        const char* dismiss_label = nullptr);
    lv_obj_t* confirmation_dialog(
        lv_obj_t* root,
        const char* title,
        const char* body,
        bool success);
    lv_obj_t* voice_orb(
        lv_obj_t* parent,
        const char* state_label,
        Tone tone = Tone::primary,
        VoiceOrbState state = VoiceOrbState::idle);
    lv_obj_t* transcript(
        lv_obj_t* parent,
        const char* final_text,
        const char* partial_text = nullptr);
    lv_obj_t* change_review(
        lv_obj_t* parent,
        const ChangeReviewProps& props);
    lv_obj_t* build_progress(
        lv_obj_t* parent,
        const BuildProgressProps& props);
    lv_obj_t* permission_review(
        lv_obj_t* parent,
        const char* capability,
        const char* explanation);
    lv_obj_t* clarification_choice_group(
        lv_obj_t* parent,
        const char* const* choices,
        std::uint8_t choice_count,
        const char* cancel_label = "Cancel");
    lv_obj_t* live_card(
        lv_obj_t* parent,
        const LiveCardProps& props);
    lv_obj_t* status_chip(
        lv_obj_t* parent,
        const char* label,
        IconName status_icon,
        Tone tone = Tone::neutral);
    lv_obj_t* voice_overlay(
        lv_obj_t* root,
        const char* status,
        const char* transcript_text,
        Tone tone = Tone::primary,
        VoiceOrbState state = VoiceOrbState::idle);

    static void reset(lv_obj_t* object);

 private:
    StyleRegistry& styles_;
};

}  // namespace m3e
