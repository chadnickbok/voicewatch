#include "m3e/components/components.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>

#include "m3e/foundation/geometry.hpp"
#include "m3e/components/transforming_list.hpp"

namespace m3e {
namespace {

const lv_font_t* font_for(generated::TypographyRole role) {
    using generated::TypographyRole;
    switch (role) {
        case TypographyRole::display_large:
        case TypographyRole::numeral_extra_large:
        case TypographyRole::numeral_large:
            return &lv_font_montserrat_18;
        case TypographyRole::display_medium:
        case TypographyRole::numeral_medium:
            return &lv_font_montserrat_18;
        case TypographyRole::display_small:
        case TypographyRole::numeral_small:
        case TypographyRole::numeral_extra_small:
            return &lv_font_montserrat_18;
        case TypographyRole::title_large:
        case TypographyRole::label_large:
            return &lv_font_montserrat_18;
        case TypographyRole::title_medium:
        case TypographyRole::body_large:
            return &lv_font_montserrat_16;
        case TypographyRole::title_small:
        case TypographyRole::label_medium:
        case TypographyRole::body_medium:
        case TypographyRole::arc_large:
        case TypographyRole::arc_medium:
        case TypographyRole::arc_small:
            return &lv_font_montserrat_14;
        case TypographyRole::label_small:
        case TypographyRole::body_small:
            return &lv_font_montserrat_14;
        case TypographyRole::body_extra_small:
            return &lv_font_montserrat_10;
        case TypographyRole::count:
            return &lv_font_montserrat_14;
    }
    return &lv_font_montserrat_14;
}

StyleRole fill_style(Tone tone) {
    switch (tone) {
        case Tone::primary: return StyleRole::primary;
        case Tone::secondary: return StyleRole::secondary;
        case Tone::tertiary: return StyleRole::tertiary;
        case Tone::error: return StyleRole::error;
        case Tone::neutral: return StyleRole::surface_high;
    }
    return StyleRole::primary;
}

generated::ColorRole fill_color_role(Tone tone) {
    using generated::ColorRole;
    switch (tone) {
        case Tone::primary: return ColorRole::primary;
        case Tone::secondary: return ColorRole::secondary;
        case Tone::tertiary: return ColorRole::tertiary;
        case Tone::error: return ColorRole::error;
        case Tone::neutral: return ColorRole::surface_container_high;
    }
    return ColorRole::primary;
}

lv_color_t theme_color(
    const ResolvedTheme& theme,
    generated::ColorRole role) {
    const auto value = theme.color.get(role).rgb888;
    return lv_color_make(value.red, value.green, value.blue);
}

StyleRole content_style(Tone tone) {
    switch (tone) {
        case Tone::primary: return StyleRole::text_on_primary;
        case Tone::secondary: return StyleRole::text_on_secondary;
        case Tone::tertiary: return StyleRole::text_on_tertiary;
        case Tone::neutral:
        case Tone::error: return StyleRole::text_on_surface;
    }
    return StyleRole::text_on_surface;
}

const char* icon_symbol(IconName name) {
    switch (name) {
        case IconName::add: return LV_SYMBOL_PLUS;
        case IconName::remove: return LV_SYMBOL_MINUS;
        case IconName::check: return LV_SYMBOL_OK;
        case IconName::close: return LV_SYMBOL_CLOSE;
        case IconName::microphone: return LV_SYMBOL_AUDIO;
        case IconName::play: return LV_SYMBOL_PLAY;
        case IconName::pause: return LV_SYMBOL_PAUSE;
        case IconName::back: return LV_SYMBOL_LEFT;
        case IconName::next: return LV_SYMBOL_RIGHT;
        case IconName::warning: return LV_SYMBOL_WARNING;
        case IconName::information: return LV_SYMBOL_LIST;
        case IconName::settings: return LV_SYMBOL_SETTINGS;
    }
    return LV_SYMBOL_WARNING;
}

std::int32_t button_height(ComponentSize size) {
    switch (size) {
        case ComponentSize::compact: return 40;
        case ComponentSize::normal: return 52;
        case ComponentSize::large: return 64;
    }
    return 52;
}

void animate_y(void* object, std::int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t*>(object), value);
}

void animate_opacity(void* object, std::int32_t value) {
    lv_obj_set_style_opa(
        static_cast<lv_obj_t*>(object),
        static_cast<lv_opa_t>(value),
        0);
}

void animate_x(void* object, std::int32_t value) {
    lv_obj_set_x(static_cast<lv_obj_t*>(object), value);
}

void animate_height(void* object, std::int32_t value) {
    lv_obj_set_height(static_cast<lv_obj_t*>(object), value);
}

void animate_radius(void* object, std::int32_t value) {
    lv_obj_set_style_radius(
        static_cast<lv_obj_t*>(object), value, 0);
}

void finish_animated_text(lv_anim_t* animation) {
    auto* incoming = static_cast<lv_obj_t*>(animation->var);
    if (incoming == nullptr) return;
    auto* container = lv_obj_get_parent(incoming);
    auto* current = lv_obj_get_child(container, 0);
    if (current == nullptr) return;
    lv_label_set_text(current, lv_label_get_text(incoming));
    lv_obj_set_y(current, 0);
    lv_obj_set_style_opa(current, LV_OPA_COVER, 0);
    lv_obj_add_flag(incoming, LV_OBJ_FLAG_HIDDEN);
}

constexpr std::uint32_t kTransformingListMagic = 0x4d334c53U;
constexpr std::uint16_t kMaximumTransformingItems = 128;
constexpr std::uint8_t kMaximumMountedTransformingItems = 8;
constexpr std::int32_t kTransformingItemPitch = 60;

struct TransformingListState {
    std::uint32_t magic;
    StyleRegistry* styles;
    const TransformingListItem* items;
    lv_obj_t* viewport;
    lv_obj_t* content;
    std::uint16_t count;
    std::uint16_t first_mounted;
    std::uint8_t mounted_count;
    bool reduced_motion;
};

void refresh_transforming_list(TransformingListState& state) {
    if (state.viewport == nullptr || state.content == nullptr ||
        state.count == 0) {
        return;
    }
    const auto scroll_y = std::max<std::int32_t>(
        0, lv_obj_get_scroll_y(state.viewport));
    const auto viewport_height =
        std::max<std::int32_t>(1, lv_obj_get_height(state.viewport));
    const auto first_visible =
        std::max<std::int32_t>(
            0, scroll_y / kTransformingItemPitch - 1);
    const auto first =
        static_cast<std::uint16_t>(
            std::min<std::int32_t>(
                first_visible,
                std::max<std::int32_t>(
                    0,
                    state.count -
                        kMaximumMountedTransformingItems)));
    const auto remaining =
        static_cast<std::uint16_t>(state.count - first);
    const auto mounted_count =
        static_cast<std::uint8_t>(
            std::min<std::uint16_t>(
                remaining, kMaximumMountedTransformingItems));
    if (first != state.first_mounted ||
        mounted_count != state.mounted_count) {
        lv_obj_clean(state.content);
        ComponentFactory factory(*state.styles);
        for (std::uint8_t slot = 0; slot < mounted_count; ++slot) {
            const auto item_index =
                static_cast<std::uint16_t>(first + slot);
            const auto& item = state.items[item_index];
            auto* object = factory.card(
                state.content,
                {
                    item.primary,
                    item.secondary,
                    item.tone,
                    true,
                });
            lv_obj_set_user_data(
                object, const_cast<char*>(item.id));
            lv_obj_set_width(object, LV_PCT(100));
            lv_obj_set_height(object, 56);
            lv_obj_set_pos(
                object,
                0,
                static_cast<std::int32_t>(item_index) *
                    kTransformingItemPitch);
        }
        state.first_mounted = first;
        state.mounted_count = mounted_count;
    }
    for (std::uint8_t slot = 0;
         slot < state.mounted_count;
         ++slot) {
        auto* object = lv_obj_get_child(state.content, slot);
        if (object == nullptr) continue;
        const auto item_index =
            static_cast<std::int32_t>(state.first_mounted + slot);
        const auto item_center =
            item_index * kTransformingItemPitch +
            kTransformingItemPitch / 2 -
            scroll_y;
        const auto geometry = transforming_item_geometry(
            static_cast<std::int16_t>(item_center),
            static_cast<std::int16_t>(viewport_height),
            56,
            state.reduced_motion);
        lv_obj_set_style_transform_scale(
            object, geometry.scale_q8_8, 0);
        lv_obj_set_style_opa(object, geometry.opacity, 0);
        lv_obj_set_height(object, geometry.transformed_height_px);
        lv_obj_set_y(
            object,
            item_index * kTransformingItemPitch +
                geometry.y_offset_px);
    }
}

void transforming_list_event(lv_event_t* event) {
    auto* state = static_cast<TransformingListState*>(
        lv_event_get_user_data(event));
    if (state == nullptr ||
        state->magic != kTransformingListMagic) {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
        state->magic = 0;
        lv_free(state);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_SCROLL ||
        lv_event_get_code(event) == LV_EVENT_SIZE_CHANGED) {
        refresh_transforming_list(*state);
    }
}

constexpr std::uint32_t kButtonGroupMagic = 0x4d334247U;

struct ButtonGroupState;
struct ButtonGroupItemContext {
    ButtonGroupState* state;
    std::uint8_t index;
};

struct ButtonGroupState {
    std::uint32_t magic;
    lv_obj_t* group;
    std::array<ButtonGroupItemContext, 3> contexts;
    std::uint8_t count;
    bool reduced_motion;
};

void animate_width(void* object, std::int32_t value) {
    lv_obj_set_width(static_cast<lv_obj_t*>(object), value);
}

void animate_button_group(
    ButtonGroupState& state,
    std::int8_t emphasized_index) {
    lv_obj_update_layout(state.group);
    const auto group_width_px =
        std::max<std::int32_t>(48, lv_obj_get_content_width(state.group));
    const auto total_width_dp =
        static_cast<std::int16_t>(
            std::max<std::int32_t>(
                state.count,
                group_width_px * 4 / 5));
    const auto layout = button_group_layout(
        total_width_dp,
        state.count,
        emphasized_index,
        state.reduced_motion ? 8 : 24);
    for (std::uint8_t index = 0; index < state.count; ++index) {
        auto* item = lv_obj_get_child(state.group, index);
        if (item == nullptr) continue;
        const auto target_width =
            static_cast<std::int32_t>(
                layout.visual_widths_dp[index]) *
            5 / 4;
        lv_anim_delete(item, animate_width);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, item);
        lv_anim_set_exec_cb(&animation, animate_width);
        lv_anim_set_values(
            &animation,
            lv_obj_get_width(item),
            target_width);
        lv_anim_set_duration(
            &animation,
            state.reduced_motion
                ? 100
                : emphasized_index >= 0 ? 120 : 280);
        lv_anim_set_path_cb(
            &animation,
            state.reduced_motion
                ? lv_anim_path_ease_out
                : emphasized_index >= 0
                    ? lv_anim_path_ease_out
                    : lv_anim_path_overshoot);
        lv_anim_start(&animation);
    }
}

void button_group_item_event(lv_event_t* event) {
    auto* context = static_cast<ButtonGroupItemContext*>(
        lv_event_get_user_data(event));
    if (context == nullptr || context->state == nullptr ||
        context->state->magic != kButtonGroupMagic) {
        return;
    }
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        animate_button_group(
            *context->state,
            static_cast<std::int8_t>(context->index));
    } else if (
        code == LV_EVENT_RELEASED ||
        code == LV_EVENT_PRESS_LOST) {
        animate_button_group(*context->state, -1);
    }
}

void button_group_delete_event(lv_event_t* event) {
    auto* state = static_cast<ButtonGroupState*>(
        lv_event_get_user_data(event));
    if (state == nullptr || state->magic != kButtonGroupMagic) return;
    state->magic = 0;
    lv_free(state);
}

void swipe_dismiss_event(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) return;
    auto* object = static_cast<lv_obj_t*>(
        lv_event_get_target(event));
    const auto width =
        std::max<std::int32_t>(1, lv_obj_get_width(object));
    if (lv_obj_get_scroll_x(object) < width / 2) {
        lv_obj_send_event(object, LV_EVENT_CANCEL, nullptr);
        lv_obj_scroll_to_x(object, width, LV_ANIM_ON);
    }
}

void finish_animated_page(lv_anim_t* animation) {
    auto* incoming = static_cast<lv_obj_t*>(animation->var);
    if (incoming == nullptr) return;
    auto* host = lv_obj_get_parent(incoming);
    if (host == nullptr || lv_obj_get_child_count(host) != 2) return;
    const auto active = static_cast<std::uint8_t>(
        reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(host)));
    for (std::uint8_t index = 0; index < 2; ++index) {
        auto* page = lv_obj_get_child(host, index);
        if (page == nullptr) continue;
        lv_obj_set_x(page, 0);
        lv_obj_set_style_opa(page, LV_OPA_COVER, 0);
        if (index == active) {
            lv_obj_remove_flag(page, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void swipe_reveal_event(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_SCROLL_END) return;
    auto* object = static_cast<lv_obj_t*>(
        lv_event_get_target(event));
    constexpr std::int32_t kRevealWidth = 88;
    if (lv_obj_get_scroll_x(object) < kRevealWidth / 2) {
        lv_obj_send_event(object, LV_EVENT_READY, nullptr);
        lv_obj_scroll_to_x(object, 0, LV_ANIM_ON);
    } else {
        lv_obj_scroll_to_x(object, kRevealWidth, LV_ANIM_ON);
    }
}

void fading_label_event(lv_event_t* event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    auto* object = static_cast<lv_obj_t*>(
        lv_event_get_target(event));
    ComponentFactory::set_fading_expanding_label_expanded(
        object,
        !lv_obj_has_state(object, LV_STATE_CHECKED));
}

}  // namespace

ComponentFactory::ComponentFactory(StyleRegistry& styles) : styles_(styles) {}

void ComponentFactory::reset(lv_obj_t* object) {
    lv_obj_remove_style_all(object);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* ComponentFactory::screen(lv_obj_t* root) {
    lv_obj_clean(root);
    reset(root);
    lv_obj_set_size(root, 240, 240);
    lv_obj_add_style(root, styles_.get(StyleRole::background), 0);
    return root;
}

lv_obj_t* ComponentFactory::text(
    lv_obj_t* parent,
    const char* value,
    generated::TypographyRole role,
    bool muted) {
    auto* object = lv_label_create(parent);
    reset(object);
    lv_label_set_text(object, value == nullptr ? "" : value);
    lv_obj_set_style_text_font(object, font_for(role), 0);
    lv_obj_add_style(
        object,
        styles_.get(
            muted ? StyleRole::text_muted : StyleRole::text_on_surface),
        0);
    return object;
}

lv_obj_t* ComponentFactory::icon(
    lv_obj_t* parent,
    IconName name,
    std::int32_t size,
    bool muted) {
    auto* object = lv_label_create(parent);
    reset(object);
    lv_label_set_text(object, icon_symbol(name));
    lv_obj_set_style_text_font(
        object,
        size <= 14 ? &lv_font_montserrat_14 :
        size <= 16 ? &lv_font_montserrat_16 :
                     &lv_font_montserrat_18,
        0);
    lv_obj_add_style(
        object,
        styles_.get(
            muted ? StyleRole::text_muted : StyleRole::text_on_surface),
        0);
    return object;
}

lv_obj_t* ComponentFactory::animated_text(
    lv_obj_t* parent,
    const char* value,
    generated::TypographyRole role) {
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    auto* current = text(container, value, role);
    auto* incoming = text(container, value, role);
    lv_obj_set_pos(current, 0, 0);
    lv_obj_set_pos(incoming, 0, 0);
    lv_obj_add_flag(incoming, LV_OBJ_FLAG_HIDDEN);
    return container;
}

bool ComponentFactory::set_animated_text(
    lv_obj_t* object,
    const char* value,
    bool incrementing,
    bool reduced_motion) {
    if (object == nullptr || value == nullptr ||
        lv_obj_get_child_count(object) != 2) {
        return false;
    }
    auto* current = lv_obj_get_child(object, 0);
    auto* incoming = lv_obj_get_child(object, 1);
    if (current == nullptr || incoming == nullptr ||
        std::strcmp(lv_label_get_text(current), value) == 0) {
        return current != nullptr && incoming != nullptr;
    }
    lv_anim_delete(current, nullptr);
    lv_anim_delete(incoming, nullptr);
    lv_label_set_text(incoming, value);
    lv_obj_remove_flag(incoming, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(object);
    lv_obj_set_width(
        object,
        std::max(
            lv_obj_get_width(current),
            lv_obj_get_width(incoming)));
    lv_obj_set_height(
        object,
        std::max(
            lv_obj_get_height(current),
            lv_obj_get_height(incoming)));
    if (reduced_motion) {
        lv_label_set_text(current, value);
        lv_obj_add_flag(incoming, LV_OBJ_FLAG_HIDDEN);
        return true;
    }
    const auto offset = incrementing ? 12 : -12;
    lv_obj_set_y(incoming, offset);
    lv_obj_set_style_opa(incoming, LV_OPA_TRANSP, 0);
    lv_anim_t movement;
    lv_anim_init(&movement);
    lv_anim_set_duration(&movement, 180);
    lv_anim_set_path_cb(&movement, lv_anim_path_ease_out);
    lv_anim_set_var(&movement, incoming);
    lv_anim_set_exec_cb(&movement, animate_y);
    lv_anim_set_values(&movement, offset, 0);
    lv_anim_start(&movement);
    lv_anim_set_var(&movement, current);
    lv_anim_set_values(&movement, 0, -offset);
    lv_anim_start(&movement);
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_duration(&fade, 140);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_set_var(&fade, current);
    lv_anim_set_exec_cb(&fade, animate_opacity);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_start(&fade);
    lv_anim_set_var(&fade, incoming);
    lv_anim_set_values(&fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_completed_cb(&fade, finish_animated_text);
    lv_anim_start(&fade);
    return true;
}

lv_obj_t* ComponentFactory::button(
    lv_obj_t* parent,
    const ButtonProps& props) {
    auto* object = lv_button_create(parent);
    reset(object);
    lv_obj_set_height(object, button_height(props.size));
    lv_obj_set_style_pad_hor(object, 18, 0);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    if (props.variant == ButtonVariant::outlined) {
        lv_obj_add_style(object, styles_.get(StyleRole::outline), 0);
    } else if (props.variant == ButtonVariant::text) {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_add_style(object, styles_.get(fill_style(props.tone)), 0);
    }
    lv_obj_add_style(
        object,
        styles_.get(StyleRole::pressed),
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
            static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
    if (!props.enabled) {
        lv_obj_add_state(object, LV_STATE_DISABLED);
        lv_obj_add_style(
            object,
            styles_.get(StyleRole::disabled),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_DISABLED));
    }
    if (props.selected) {
        lv_obj_add_state(object, LV_STATE_CHECKED);
    }

    auto* label = lv_label_create(object);
    reset(label);
    lv_label_set_text(label, props.label == nullptr ? "" : props.label);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    const auto text_role =
        props.variant == ButtonVariant::outlined ||
                props.variant == ButtonVariant::text
            ? StyleRole::text_on_surface
            : content_style(props.tone);
    lv_obj_add_style(label, styles_.get(text_role), 0);
    lv_obj_center(label);
    return object;
}

lv_obj_t* ComponentFactory::icon_button(
    lv_obj_t* parent,
    const char* id,
    IconName icon_name,
    ButtonVariant variant,
    ComponentSize size,
    bool enabled,
    bool selected) {
    auto* object = lv_button_create(parent);
    reset(object);
    const auto visual_size = button_height(size);
    lv_obj_set_size(object, visual_size, visual_size);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    if (variant == ButtonVariant::outlined) {
        lv_obj_add_style(object, styles_.get(StyleRole::outline), 0);
    } else if (variant == ButtonVariant::text) {
        lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    } else {
        lv_obj_add_style(
            object,
            styles_.get(
                variant == ButtonVariant::tonal
                    ? StyleRole::secondary
                    : StyleRole::primary),
            0);
    }
    lv_obj_add_style(
        object,
        styles_.get(StyleRole::pressed),
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
            static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
    if (!enabled) {
        lv_obj_add_state(object, LV_STATE_DISABLED);
        lv_obj_add_style(
            object,
            styles_.get(StyleRole::disabled),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_DISABLED));
    }
    if (selected) lv_obj_add_state(object, LV_STATE_CHECKED);
    auto* glyph = lv_label_create(object);
    reset(glyph);
    lv_label_set_text(glyph, icon_symbol(icon_name));
    lv_obj_set_style_text_font(glyph, &lv_font_montserrat_18, 0);
    lv_obj_add_style(
        glyph,
        styles_.get(
            variant == ButtonVariant::filled ||
                    variant == ButtonVariant::tonal
                ? variant == ButtonVariant::tonal
                    ? StyleRole::text_on_secondary
                    : StyleRole::text_on_primary
                : StyleRole::text_on_surface),
        0);
    lv_obj_center(glyph);
    lv_obj_set_user_data(object, const_cast<char*>(id));
    return object;
}

lv_obj_t* ComponentFactory::text_toggle_button(
    lv_obj_t* parent,
    const ButtonProps& props) {
    auto* object = button(parent, props);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CHECKABLE);
    if (props.selected) lv_obj_add_state(object, LV_STATE_CHECKED);
    return object;
}

lv_obj_t* ComponentFactory::icon_toggle_button(
    lv_obj_t* parent,
    const char* id,
    IconName icon_name,
    bool selected,
    ButtonVariant variant,
    ComponentSize size,
    bool enabled) {
    auto* object = icon_button(
        parent,
        id,
        icon_name,
        variant,
        size,
        enabled,
        selected);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CHECKABLE);
    return object;
}

lv_obj_t* ComponentFactory::card(lv_obj_t* parent, const CardProps& props) {
    auto* object = lv_obj_create(parent);
    reset(object);
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_height(object, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(object, 64, 0);
    lv_obj_set_style_pad_all(object, 12, 0);
    lv_obj_set_style_pad_row(object, 4, 0);
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(
        object,
        styles_.get(
            props.tone == Tone::neutral
                ? StyleRole::surface
                : fill_style(props.tone)),
        0);
    if (props.clickable) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_style(
            object,
            styles_.get(StyleRole::pressed),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
    }
    auto* title = text(
        object, props.title, generated::TypographyRole::title_small);
    auto* body = text(
        object, props.body, generated::TypographyRole::body_small, true);
    if (props.tone != Tone::neutral) {
        lv_obj_add_style(
            title, styles_.get(content_style(props.tone)), 0);
        lv_obj_add_style(
            body, styles_.get(content_style(props.tone)), 0);
        lv_obj_set_style_opa(body, LV_OPA_70, 0);
    }
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_width(body, LV_PCT(100));
    return object;
}

lv_obj_t* ComponentFactory::list_header(
    lv_obj_t* parent,
    const char* value,
    bool subheader) {
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, LV_PCT(100), subheader ? 32 : 42);
    lv_obj_set_style_pad_hor(container, 8, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    auto* title = text(
        container,
        value,
        subheader
            ? generated::TypographyRole::label_medium
            : generated::TypographyRole::title_medium,
        subheader);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(title, 1);
    return container;
}

lv_obj_t* ComponentFactory::linear_progress(
    lv_obj_t* parent,
    const ProgressProps& props) {
    auto* object = lv_bar_create(parent);
    reset(object);
    lv_obj_set_size(object, LV_PCT(100), 8);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        object,
        theme_color(
            styles_.theme(),
            generated::ColorRole::surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        object,
        theme_color(styles_.theme(), fill_color_role(props.tone)),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_bar_set_range(
        object, 0, std::max(std::int32_t{1}, props.maximum));
    lv_bar_set_value(
        object,
        std::clamp(props.value, std::int32_t{0}, props.maximum),
        LV_ANIM_OFF);
    return object;
}

lv_obj_t* ComponentFactory::circular_progress(
    lv_obj_t* parent,
    const ProgressProps& props) {
    auto* object = lv_arc_create(parent);
    reset(object);
    lv_obj_set_size(object, 56, 56);
    lv_arc_set_range(
        object, 0, std::max(std::int32_t{1}, props.maximum));
    lv_arc_set_value(
        object,
        std::clamp(props.value, std::int32_t{0}, props.maximum));
    lv_obj_remove_style(object, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(object, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(object, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(
        object,
        theme_color(
            styles_.theme(),
            generated::ColorRole::surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_arc_color(
        object,
        theme_color(styles_.theme(), fill_color_role(props.tone)),
        LV_PART_INDICATOR);
    return object;
}

lv_obj_t* ComponentFactory::segmented_circular_progress(
    lv_obj_t* parent,
    const ProgressProps& props,
    std::uint8_t segment_count) {
    segment_count = std::clamp<std::uint8_t>(segment_count, 2, 12);
    const auto maximum = std::max<std::int32_t>(1, props.maximum);
    const auto value = std::clamp(props.value, std::int32_t{0}, maximum);
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, 64, 64);
    constexpr std::int32_t total_degrees = 280;
    constexpr std::int32_t gap_degrees = 4;
    const auto segment_degrees =
        (total_degrees -
         gap_degrees * static_cast<std::int32_t>(segment_count - 1)) /
        segment_count;
    const auto filled =
        static_cast<std::int32_t>(segment_count) * value / maximum;
    for (std::uint8_t index = 0; index < segment_count; ++index) {
        auto* arc = lv_arc_create(container);
        reset(arc);
        lv_obj_set_size(arc, 64, 64);
        lv_obj_center(arc);
        const auto start =
            130 + static_cast<std::int32_t>(index) *
                (segment_degrees + gap_degrees);
        lv_arc_set_bg_angles(arc, start, start + segment_degrees);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, index < filled ? 100 : 0);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(
            arc,
            theme_color(
                styles_.theme(),
                generated::ColorRole::surface_container_high),
            LV_PART_MAIN);
        lv_obj_set_style_arc_color(
            arc,
            theme_color(styles_.theme(), fill_color_role(props.tone)),
            LV_PART_INDICATOR);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    }
    return container;
}

lv_obj_t* ComponentFactory::toggle_row(
    lv_obj_t* parent,
    const char* label_value,
    bool checked,
    bool enabled) {
    auto* row = lv_button_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(row, styles_.get(StyleRole::surface), 0);
    auto* label = text(
        row, label_value, generated::TypographyRole::body_medium);
    auto* indicator = lv_obj_create(row);
    reset(indicator);
    lv_obj_set_size(indicator, 34, 22);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(
        indicator,
        styles_.get(checked ? StyleRole::primary : StyleRole::surface_high),
        0);
    auto* knob = lv_obj_create(indicator);
    reset(knob);
    lv_obj_set_size(knob, 16, 16);
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_align(
        knob, checked ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID,
        checked ? -3 : 3, 0);
    (void)label;
    if (!enabled) {
        lv_obj_add_state(row, LV_STATE_DISABLED);
        lv_obj_add_style(
            row,
            styles_.get(StyleRole::disabled),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_DISABLED));
    }
    return row;
}

lv_obj_t* ComponentFactory::stepper(
    lv_obj_t* parent,
    const StepperProps& props) {
    auto* row = lv_obj_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_PCT(100), 64);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(row, styles_.get(StyleRole::surface), 0);
    button(row, {"decrement", "-", Tone::neutral, ButtonVariant::tonal,
                 ComponentSize::compact, !props.at_minimum, false});
    char value[40];
    std::snprintf(
        value, sizeof(value), "%ld %s",
        static_cast<long>(props.value), props.unit == nullptr ? "" : props.unit);
    auto* value_label = text(
        row, value, generated::TypographyRole::title_medium);
    lv_obj_set_flex_grow(value_label, 1);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_CENTER, 0);
    button(row, {"increment", "+", Tone::primary, ButtonVariant::filled,
                 ComponentSize::compact, !props.at_maximum, false});
    return row;
}

lv_obj_t* ComponentFactory::button_group(
    lv_obj_t* parent,
    const ButtonProps* buttons,
    std::uint8_t count,
    std::int8_t emphasized_index,
    bool reduced_motion) {
    if (buttons == nullptr) return nullptr;
    count = std::clamp<std::uint8_t>(count, 1, 3);
    auto* group = lv_obj_create(parent);
    reset(group);
    lv_obj_set_size(group, LV_PCT(100), 52);
    lv_obj_set_style_pad_column(group, 5, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    const auto layout = button_group_layout(168, count, emphasized_index);
    auto* state = static_cast<ButtonGroupState*>(
        lv_malloc(sizeof(ButtonGroupState)));
    if (state == nullptr) {
        lv_obj_delete(group);
        return nullptr;
    }
    *state = {
        kButtonGroupMagic,
        group,
        {},
        count,
        reduced_motion,
    };
    lv_obj_set_user_data(group, state);
    lv_obj_add_event_cb(
        group,
        button_group_delete_event,
        LV_EVENT_DELETE,
        state);
    for (std::uint8_t index = 0; index < count; ++index) {
        auto* item = button(group, buttons[index]);
        lv_obj_set_width(
            item,
            static_cast<std::int32_t>(
                layout.visual_widths_dp[index] * 5) /
                4);
        state->contexts[index] = {state, index};
        lv_obj_add_event_cb(
            item,
            button_group_item_event,
            LV_EVENT_PRESSED,
            &state->contexts[index]);
        lv_obj_add_event_cb(
            item,
            button_group_item_event,
            LV_EVENT_RELEASED,
            &state->contexts[index]);
        lv_obj_add_event_cb(
            item,
            button_group_item_event,
            LV_EVENT_PRESS_LOST,
            &state->contexts[index]);
    }
    return group;
}

lv_obj_t* ComponentFactory::selection_row(
    lv_obj_t* parent,
    const char* label_value,
    SelectionKind kind,
    bool checked,
    bool enabled) {
    if (kind == SelectionKind::switch_control) {
        return toggle_row(parent, label_value, checked, enabled);
    }
    auto* row = lv_button_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_PCT(100), 52);
    lv_obj_set_style_pad_hor(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(row, styles_.get(StyleRole::surface), 0);
    text(row, label_value, generated::TypographyRole::body_medium);
    auto* mark = lv_obj_create(row);
    reset(mark);
    lv_obj_set_size(mark, 22, 22);
    lv_obj_set_style_radius(
        mark,
        kind == SelectionKind::radio ? LV_RADIUS_CIRCLE : 6,
        0);
    if (checked) {
        lv_obj_add_style(mark, styles_.get(StyleRole::primary), 0);
        auto* glyph = lv_label_create(mark);
        reset(glyph);
        lv_label_set_text(
            glyph,
            kind == SelectionKind::radio ? LV_SYMBOL_OK : LV_SYMBOL_OK);
        lv_obj_add_style(
            glyph, styles_.get(StyleRole::text_on_primary), 0);
        lv_obj_center(glyph);
    } else {
        lv_obj_add_style(mark, styles_.get(StyleRole::outline), 0);
    }
    if (!enabled) {
        lv_obj_add_state(row, LV_STATE_DISABLED);
        lv_obj_add_style(
            row,
            styles_.get(StyleRole::disabled),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) |
                static_cast<lv_style_selector_t>(LV_STATE_DISABLED));
    }
    return row;
}

lv_obj_t* ComponentFactory::slider(
    lv_obj_t* parent,
    std::int32_t value,
    std::int32_t minimum,
    std::int32_t maximum,
    std::uint8_t steps) {
    if (maximum <= minimum) {
        maximum = minimum + 1;
    }
    auto* object = lv_slider_create(parent);
    reset(object);
    lv_obj_set_size(object, LV_PCT(100), 8);
    lv_slider_set_range(object, minimum, maximum);
    if (steps > 1) {
        const auto interval =
            std::max<std::int32_t>(1, (maximum - minimum) / (steps - 1));
        value = minimum +
            ((value - minimum + interval / 2) / interval) * interval;
    }
    lv_slider_set_value(
        object,
        std::clamp(value, minimum, maximum),
        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        object,
        theme_color(
            styles_.theme(),
            generated::ColorRole::surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        object,
        theme_color(styles_.theme(), generated::ColorRole::primary),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        object,
        theme_color(styles_.theme(), generated::ColorRole::primary),
        LV_PART_KNOB);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_width(object, 24, LV_PART_KNOB);
    lv_obj_set_style_height(object, 24, LV_PART_KNOB);
    return object;
}

lv_obj_t* ComponentFactory::screen_scaffold(
    lv_obj_t* root,
    const char* time_value,
    bool show_time) {
    screen(root);
    if (show_time) {
        auto* clock = text(
            root,
            time_value == nullptr ? "--:--" : time_value,
            generated::TypographyRole::label_small,
            true);
        lv_obj_align(clock, LV_ALIGN_TOP_MID, 0, 5);
    }
    auto* content = lv_obj_create(root);
    reset(content);
    lv_obj_set_pos(content, 12, show_time ? 24 : 10);
    lv_obj_set_size(content, 216, show_time ? 206 : 220);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    return content;
}

lv_obj_t* ComponentFactory::app_scaffold(
    lv_obj_t* root,
    const char* time_value,
    bool show_time) {
    return screen_scaffold(root, time_value, show_time);
}

lv_obj_t* ComponentFactory::time_text(
    lv_obj_t* parent,
    const char* value,
    const char* leading_status) {
    auto* row = lv_obj_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    if (leading_status != nullptr && leading_status[0] != '\0') {
        text(
            row,
            leading_status,
            generated::TypographyRole::label_small,
            true);
    }
    text(
        row,
        value == nullptr ? "--:--" : value,
        generated::TypographyRole::label_small,
        true);
    return row;
}

lv_obj_t* ComponentFactory::picker(
    lv_obj_t* parent,
    const PickerProps& props) {
    const auto count = std::clamp<std::uint8_t>(props.count, 1, 60);
    const auto selected =
        std::min<std::uint8_t>(props.selected_index, count - 1);
    auto* viewport = lv_obj_create(parent);
    reset(viewport);
    lv_obj_set_size(viewport, LV_PCT(100), 120);
    lv_obj_set_style_pad_ver(viewport, 40, 0);
    lv_obj_set_style_pad_row(viewport, 4, 0);
    lv_obj_set_flex_flow(viewport, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(viewport, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
    for (std::uint8_t index = 0; index < count; ++index) {
        auto* item = lv_obj_create(viewport);
        reset(item);
        lv_obj_set_size(item, LV_PCT(100), 36);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(
            item,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
        if (index == selected) {
            lv_obj_add_style(item, styles_.get(StyleRole::surface_high), 0);
        }
        auto* item_label = text(
            item,
            props.items[index],
            index == selected
                ? generated::TypographyRole::title_medium
                : generated::TypographyRole::body_small,
            index != selected);
        lv_obj_add_flag(item, LV_OBJ_FLAG_SNAPPABLE);
        (void)item_label;
    }
    lv_obj_update_layout(viewport);
    auto* selected_item = lv_obj_get_child(viewport, selected);
    if (selected_item != nullptr) {
        lv_obj_scroll_to_view(selected_item, LV_ANIM_OFF);
    }
    return viewport;
}

lv_obj_t* ComponentFactory::picker_group(
    lv_obj_t* parent,
    const PickerProps* columns,
    std::uint8_t column_count) {
    if (columns == nullptr) return nullptr;
    column_count = std::clamp<std::uint8_t>(column_count, 1, 3);
    auto* group = lv_obj_create(parent);
    reset(group);
    lv_obj_set_size(group, LV_PCT(100), 120);
    lv_obj_set_style_pad_column(group, 4, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
    for (std::uint8_t index = 0; index < column_count; ++index) {
        auto* column = picker(group, columns[index]);
        lv_obj_set_flex_grow(column, 1);
    }
    return group;
}

lv_obj_t* ComponentFactory::date_picker(
    lv_obj_t* parent,
    std::int32_t year,
    std::uint8_t month,
    std::uint8_t day) {
    static constexpr const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    month = std::clamp<std::uint8_t>(month, 1, 12);
    day = std::clamp<std::uint8_t>(day, 1, 31);
    std::array<std::array<char, 3>, 31> day_storage{};
    std::array<const char*, 31> days{};
    for (std::size_t index = 0; index < days.size(); ++index) {
        std::snprintf(
            day_storage[index].data(),
            day_storage[index].size(),
            "%u",
            static_cast<unsigned>(index + 1));
        days[index] = day_storage[index].data();
    }
    std::array<std::array<char, 6>, 5> year_storage{};
    std::array<const char*, 5> years{};
    for (std::size_t index = 0; index < years.size(); ++index) {
        std::snprintf(
            year_storage[index].data(),
            year_storage[index].size(),
            "%ld",
            static_cast<long>(year - 2 +
                static_cast<std::int32_t>(index)));
        years[index] = year_storage[index].data();
    }
    const PickerProps columns[] = {
        {months, 12, static_cast<std::uint8_t>(month - 1)},
        {days.data(), 31, static_cast<std::uint8_t>(day - 1)},
        {years.data(), 5, 2},
    };
    return picker_group(parent, columns, 3);
}

lv_obj_t* ComponentFactory::time_picker(
    lv_obj_t* parent,
    std::uint8_t hour,
    std::uint8_t minute,
    bool use_24_hour) {
    hour = std::min<std::uint8_t>(hour, 23);
    minute = std::min<std::uint8_t>(minute, 59);
    std::array<std::array<char, 3>, 24> hour_storage{};
    std::array<const char*, 24> hours{};
    const auto hour_count =
        static_cast<std::uint8_t>(use_24_hour ? 24 : 12);
    for (std::uint8_t index = 0; index < hour_count; ++index) {
        std::snprintf(
            hour_storage[index].data(),
            hour_storage[index].size(),
            "%02u",
            static_cast<unsigned>(
                use_24_hour ? index : index + 1));
        hours[index] = hour_storage[index].data();
    }
    std::array<std::array<char, 3>, 60> minute_storage{};
    std::array<const char*, 60> minutes{};
    for (std::size_t index = 0; index < minutes.size(); ++index) {
        std::snprintf(
            minute_storage[index].data(),
            minute_storage[index].size(),
            "%02u",
            static_cast<unsigned>(index));
        minutes[index] = minute_storage[index].data();
    }
    static constexpr const char* periods[] = {"AM", "PM"};
    if (use_24_hour) {
        const PickerProps columns[] = {
            {hours.data(), hour_count, hour},
            {minutes.data(), 60, minute},
        };
        return picker_group(parent, columns, 2);
    }
    const auto display_hour =
        static_cast<std::uint8_t>(hour % 12);
    const PickerProps columns[] = {
        {
            hours.data(),
            hour_count,
            static_cast<std::uint8_t>(
                display_hour == 0 ? 11 : display_hour - 1),
        },
        {minutes.data(), 60, minute},
        {periods, 2, static_cast<std::uint8_t>(hour >= 12 ? 1 : 0)},
    };
    return picker_group(parent, columns, 3);
}

lv_obj_t* ComponentFactory::horizontal_pager(
    lv_obj_t* parent,
    std::uint8_t page_count,
    std::uint8_t selected_page) {
    page_count = std::clamp<std::uint8_t>(page_count, 1, 7);
    selected_page = std::min<std::uint8_t>(selected_page, page_count - 1);
    auto* pager = lv_tileview_create(parent);
    reset(pager);
    lv_obj_set_size(pager, LV_PCT(100), LV_PCT(100));
    for (std::uint8_t index = 0; index < page_count; ++index) {
        auto* tile = lv_tileview_add_tile(
            pager,
            index,
            0,
            static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_RIGHT));
        reset(tile);
        lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            tile,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER,
            LV_FLEX_ALIGN_CENTER);
    }
    lv_tileview_set_tile_by_index(
        pager, selected_page, 0, LV_ANIM_OFF);
    return pager;
}

lv_obj_t* ComponentFactory::horizontal_pager_scaffold(
    lv_obj_t* parent,
    std::uint8_t page_count,
    std::uint8_t selected_page,
    const char* time_value) {
    auto* scaffold = lv_obj_create(parent);
    reset(scaffold);
    lv_obj_set_size(scaffold, LV_PCT(100), LV_PCT(100));
    if (time_value != nullptr && time_value[0] != '\0') {
        auto* time = time_text(scaffold, time_value);
        lv_obj_align(time, LV_ALIGN_TOP_MID, 0, 4);
    }
    auto* pager = horizontal_pager(
        scaffold, page_count, selected_page);
    lv_obj_set_size(pager, LV_PCT(100), LV_PCT(100));
    auto* indicator = page_indicator(
        scaffold, page_count, selected_page);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_move_foreground(indicator);
    return scaffold;
}

lv_obj_t* ComponentFactory::animated_page(lv_obj_t* parent) {
    auto* host = lv_obj_create(parent);
    reset(host);
    lv_obj_set_size(host, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_clip_corner(host, true, 0);
    for (std::uint8_t index = 0; index < 2; ++index) {
        auto* page = lv_obj_create(host);
        reset(page);
        lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(page, 0, 0);
        if (index != 0) {
            lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_set_user_data(
        host,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(0)));
    return host;
}

lv_obj_t* ComponentFactory::animated_page_slot(
    lv_obj_t* object,
    std::uint8_t index) {
    if (object == nullptr || index > 1 ||
        lv_obj_get_child_count(object) != 2) {
        return nullptr;
    }
    return lv_obj_get_child(object, index);
}

bool ComponentFactory::show_animated_page(
    lv_obj_t* object,
    std::uint8_t index,
    bool forward,
    bool reduced_motion) {
    if (object == nullptr || index > 1 ||
        lv_obj_get_child_count(object) != 2) {
        return false;
    }
    const auto current_index = static_cast<std::uint8_t>(
        reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(object)));
    if (current_index == index) return true;
    auto* outgoing = lv_obj_get_child(object, current_index);
    auto* incoming = lv_obj_get_child(object, index);
    if (outgoing == nullptr || incoming == nullptr) return false;
    lv_anim_delete(outgoing, nullptr);
    lv_anim_delete(incoming, nullptr);
    lv_obj_set_user_data(
        object,
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(index)));
    lv_obj_remove_flag(incoming, LV_OBJ_FLAG_HIDDEN);
    if (reduced_motion) {
        lv_obj_add_flag(outgoing, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(incoming, 0);
        lv_obj_set_style_opa(incoming, LV_OPA_COVER, 0);
        return true;
    }
    lv_obj_update_layout(object);
    const auto width =
        std::max<std::int32_t>(1, lv_obj_get_width(object));
    const auto direction = forward ? 1 : -1;
    lv_obj_set_x(outgoing, 0);
    lv_obj_set_style_opa(outgoing, LV_OPA_COVER, 0);
    lv_obj_set_x(incoming, direction * width);
    lv_obj_set_style_opa(incoming, LV_OPA_40, 0);

    lv_anim_t movement;
    lv_anim_init(&movement);
    lv_anim_set_duration(&movement, 280);
    lv_anim_set_path_cb(&movement, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&movement, animate_x);
    lv_anim_set_var(&movement, outgoing);
    lv_anim_set_values(&movement, 0, -direction * width / 4);
    lv_anim_start(&movement);
    lv_anim_set_var(&movement, incoming);
    lv_anim_set_values(&movement, direction * width, 0);
    lv_anim_start(&movement);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_duration(&fade, 220);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade, animate_opacity);
    lv_anim_set_var(&fade, outgoing);
    lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_40);
    lv_anim_start(&fade);
    lv_anim_set_var(&fade, incoming);
    lv_anim_set_values(&fade, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_completed_cb(&fade, finish_animated_page);
    lv_anim_start(&fade);
    return true;
}

lv_obj_t* ComponentFactory::fading_expanding_label(
    lv_obj_t* parent,
    const char* value,
    std::int32_t collapsed_height,
    bool expanded) {
    collapsed_height =
        std::clamp<std::int32_t>(collapsed_height, 20, 96);
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_style_pad_all(container, 6, 0);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(container, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_user_data(
        container,
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(collapsed_height)));
    auto* label = text(
        container,
        value,
        generated::TypographyRole::body_medium);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_add_event_cb(
        container,
        fading_label_event,
        LV_EVENT_CLICKED,
        nullptr);
    lv_obj_update_layout(container);
    if (expanded) {
        lv_obj_add_state(container, LV_STATE_CHECKED);
        lv_obj_set_height(
            container,
            std::max<std::int32_t>(
                collapsed_height,
                lv_obj_get_height(label) + 12));
    } else {
        lv_obj_set_height(container, collapsed_height);
    }
    return container;
}

bool ComponentFactory::set_fading_expanding_label_expanded(
    lv_obj_t* object,
    bool expanded,
    bool reduced_motion) {
    if (object == nullptr || lv_obj_get_child_count(object) != 1) {
        return false;
    }
    auto* label = lv_obj_get_child(object, 0);
    const auto collapsed_height = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(
            lv_obj_get_user_data(object)));
    if (collapsed_height < 20 || collapsed_height > 96) return false;
    lv_obj_update_layout(object);
    const auto target_height = expanded
        ? std::max<std::int32_t>(
              collapsed_height, lv_obj_get_height(label) + 12)
        : collapsed_height;
    if (expanded) {
        lv_obj_add_state(object, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    }
    lv_anim_delete(object, animate_height);
    if (reduced_motion) {
        lv_obj_set_height(object, target_height);
        lv_obj_set_style_opa(object, LV_OPA_COVER, 0);
        return true;
    }
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_exec_cb(&animation, animate_height);
    lv_anim_set_values(
        &animation, lv_obj_get_height(object), target_height);
    lv_anim_set_duration(&animation, 220);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
    return true;
}

lv_obj_t* ComponentFactory::swipe_to_dismiss_box(
    lv_obj_t* parent) {
    auto* viewport = lv_obj_create(parent);
    reset(viewport);
    lv_obj_set_size(viewport, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(viewport, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(viewport, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
    auto* background = lv_obj_create(viewport);
    reset(background);
    lv_obj_set_pos(background, 0, 0);
    lv_obj_set_size(background, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(
        background, styles_.get(StyleRole::surface_low), 0);
    auto* back_icon = icon(
        background, IconName::back, 24, true);
    lv_obj_align(back_icon, LV_ALIGN_LEFT_MID, 18, 0);
    auto* foreground = lv_obj_create(viewport);
    reset(foreground);
    lv_obj_set_pos(foreground, LV_PCT(100), 0);
    lv_obj_set_size(foreground, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(
        foreground, styles_.get(StyleRole::background), 0);
    lv_obj_add_flag(background, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_add_flag(foreground, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_add_event_cb(
        viewport,
        swipe_dismiss_event,
        LV_EVENT_SCROLL_END,
        nullptr);
    lv_obj_update_layout(viewport);
    lv_obj_scroll_to_x(
        viewport, lv_obj_get_width(viewport), LV_ANIM_OFF);
    return viewport;
}

lv_obj_t* ComponentFactory::swipe_to_reveal(
    lv_obj_t* parent,
    const char* primary_action,
    const char* secondary_action) {
    constexpr std::int32_t kRevealWidth = 88;
    auto* viewport = lv_obj_create(parent);
    reset(viewport);
    lv_obj_set_size(viewport, LV_PCT(100), 60);
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(viewport, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(viewport, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
    auto* actions = lv_obj_create(viewport);
    reset(actions);
    lv_obj_set_size(actions, kRevealWidth, LV_PCT(100));
    lv_obj_set_pos(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_add_style(actions, styles_.get(StyleRole::surface_high), 0);
    const auto action_count =
        secondary_action == nullptr ? 1 : 2;
    const char* labels[] = {
        primary_action == nullptr ? "Action" : primary_action,
        secondary_action,
    };
    for (std::uint8_t index = 0; index < action_count; ++index) {
        auto* action = button(
            actions,
            {
                index == 0 ? "reveal.primary" : "reveal.secondary",
                labels[index],
                index == 0 ? Tone::primary : Tone::secondary,
                ButtonVariant::filled,
                ComponentSize::compact,
                true,
                false,
            });
        lv_obj_set_width(action, kRevealWidth / action_count);
        lv_obj_set_height(action, LV_PCT(100));
    }
    auto* foreground = lv_obj_create(viewport);
    reset(foreground);
    lv_obj_set_size(foreground, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(foreground, kRevealWidth, 0);
    lv_obj_add_style(
        foreground, styles_.get(StyleRole::surface), 0);
    lv_obj_add_flag(actions, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_add_flag(foreground, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_add_event_cb(
        viewport,
        swipe_reveal_event,
        LV_EVENT_SCROLL_END,
        nullptr);
    lv_obj_update_layout(viewport);
    lv_obj_scroll_to_x(viewport, kRevealWidth, LV_ANIM_OFF);
    return viewport;
}

lv_obj_t* ComponentFactory::split_selection_row(
    lv_obj_t* parent,
    const char* label,
    SelectionKind kind,
    bool checked,
    bool enabled) {
    auto* row = lv_obj_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_PCT(100), 60);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    auto* primary = button(
        row,
        {
            "split.primary",
            label,
            Tone::neutral,
            ButtonVariant::text,
            ComponentSize::normal,
            enabled,
            false,
        });
    lv_obj_set_flex_grow(primary, 1);
    auto* control = lv_button_create(row);
    reset(control);
    lv_obj_set_size(control, 52, 52);
    lv_obj_set_style_radius(
        control,
        kind == SelectionKind::switch_control
            ? LV_RADIUS_CIRCLE
            : 12,
        0);
    lv_obj_add_style(
        control,
        styles_.get(
            checked ? StyleRole::primary : StyleRole::surface_high),
        0);
    lv_obj_add_flag(control, LV_OBJ_FLAG_CHECKABLE);
    if (checked) lv_obj_add_state(control, LV_STATE_CHECKED);
    if (!enabled) lv_obj_add_state(control, LV_STATE_DISABLED);
    auto* glyph = text(
        control,
        checked
            ? LV_SYMBOL_OK
            : kind == SelectionKind::radio ? "o" : "-",
        generated::TypographyRole::label_large,
        !checked);
    lv_obj_center(glyph);
    return row;
}

lv_obj_t* ComponentFactory::page_indicator(
    lv_obj_t* parent,
    std::uint8_t page_count,
    std::uint8_t selected_page) {
    page_count = std::clamp<std::uint8_t>(page_count, 1, 7);
    selected_page = std::min<std::uint8_t>(selected_page, page_count - 1);
    auto* row = lv_obj_create(parent);
    reset(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 12);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    for (std::uint8_t index = 0; index < page_count; ++index) {
        auto* dot = lv_obj_create(row);
        reset(dot);
        lv_obj_set_size(dot, index == selected_page ? 14 : 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_add_style(
            dot,
            styles_.get(
                index == selected_page
                    ? StyleRole::primary
                    : StyleRole::surface_high),
            0);
    }
    return row;
}

lv_obj_t* ComponentFactory::scroll_indicator(
    lv_obj_t* parent,
    std::int32_t position,
    std::int32_t maximum) {
    maximum = std::max<std::int32_t>(1, maximum);
    auto* indicator = lv_bar_create(parent);
    reset(indicator);
    lv_obj_set_size(indicator, 4, 72);
    lv_bar_set_range(indicator, 0, maximum);
    lv_bar_set_value(
        indicator,
        std::clamp(position, std::int32_t{0}, maximum),
        LV_ANIM_OFF);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(
        indicator, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(
        indicator,
        theme_color(
            styles_.theme(),
            generated::ColorRole::surface_container_high),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        indicator,
        theme_color(styles_.theme(), generated::ColorRole::primary),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, LV_PART_INDICATOR);
    return indicator;
}

lv_obj_t* ComponentFactory::level_indicator(
    lv_obj_t* parent,
    std::int32_t value,
    std::int32_t maximum,
    Tone level_tone) {
    auto* indicator = linear_progress(
        parent, {"Level", value, maximum, level_tone});
    lv_obj_set_size(indicator, 8, 72);
    return indicator;
}

lv_obj_t* ComponentFactory::loading_placeholder(
    lv_obj_t* parent,
    std::int32_t width,
    std::int32_t height,
    bool reduced_motion) {
    auto* placeholder = lv_obj_create(parent);
    reset(placeholder);
    lv_obj_set_size(
        placeholder,
        std::max<std::int32_t>(1, width),
        std::max<std::int32_t>(1, height));
    lv_obj_add_style(
        placeholder,
        styles_.get(
            reduced_motion ? StyleRole::surface : StyleRole::surface_high),
        0);
    return placeholder;
}

bool ComponentFactory::morph_shape_state(
    lv_obj_t* object,
    bool selected,
    bool reduced_motion) {
    if (object == nullptr) return false;
    const auto target_radius = selected ? LV_RADIUS_CIRCLE : 8;
    lv_anim_delete(object, animate_radius);
    if (reduced_motion) {
        lv_obj_set_style_radius(object, target_radius, 0);
    } else {
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, object);
        lv_anim_set_exec_cb(&animation, animate_radius);
        lv_anim_set_values(
            &animation,
            lv_obj_get_style_radius(object, LV_PART_MAIN),
            target_radius);
        lv_anim_set_duration(&animation, 240);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
        lv_anim_start(&animation);
    }
    if (selected) {
        lv_obj_add_state(object, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(object, LV_STATE_CHECKED);
    }
    return true;
}

lv_obj_t* ComponentFactory::transforming_list(
    lv_obj_t* parent,
    const TransformingListItem* items,
    std::uint16_t count,
    bool reduced_motion) {
    if (parent == nullptr || items == nullptr || count == 0 ||
        count > kMaximumTransformingItems) {
        return nullptr;
    }
    auto* viewport = lv_obj_create(parent);
    reset(viewport);
    lv_obj_set_size(viewport, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(viewport, LV_SCROLLBAR_MODE_OFF);
    auto* content = lv_obj_create(viewport);
    reset(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(
        content,
        static_cast<std::int32_t>(count) *
            kTransformingItemPitch);
    auto* state = static_cast<TransformingListState*>(
        lv_malloc(sizeof(TransformingListState)));
    if (state == nullptr) {
        lv_obj_delete(viewport);
        return nullptr;
    }
    *state = {
        kTransformingListMagic,
        &styles_,
        items,
        viewport,
        content,
        count,
        std::numeric_limits<std::uint16_t>::max(),
        0,
        reduced_motion,
    };
    lv_obj_set_user_data(viewport, state);
    lv_obj_add_event_cb(
        viewport,
        transforming_list_event,
        LV_EVENT_SCROLL,
        state);
    lv_obj_add_event_cb(
        viewport,
        transforming_list_event,
        LV_EVENT_SIZE_CHANGED,
        state);
    lv_obj_add_event_cb(
        viewport,
        transforming_list_event,
        LV_EVENT_DELETE,
        state);
    lv_obj_update_layout(viewport);
    refresh_transforming_list(*state);
    return viewport;
}

std::size_t ComponentFactory::transforming_list_mounted_count(
    lv_obj_t* object) {
    if (object == nullptr) return 0;
    auto* state = static_cast<TransformingListState*>(
        lv_obj_get_user_data(object));
    return state != nullptr &&
            state->magic == kTransformingListMagic
        ? state->mounted_count
        : 0;
}

bool ComponentFactory::update_transforming_list(
    lv_obj_t* object,
    const TransformingListItem* items,
    std::uint16_t count) {
    if (object == nullptr || items == nullptr || count == 0 ||
        count > kMaximumTransformingItems) {
        return false;
    }
    auto* state = static_cast<TransformingListState*>(
        lv_obj_get_user_data(object));
    if (state == nullptr ||
        state->magic != kTransformingListMagic) {
        return false;
    }
    const auto old_scroll_y =
        std::max<std::int32_t>(0, lv_obj_get_scroll_y(object));
    const auto focal_y =
        old_scroll_y +
        std::max<std::int32_t>(1, lv_obj_get_height(object)) / 2;
    const auto old_anchor_index =
        static_cast<std::uint16_t>(
            std::min<std::int32_t>(
                state->count - 1,
                focal_y / kTransformingItemPitch));
    const auto* anchor_id =
        state->items[old_anchor_index].id;
    std::uint16_t new_anchor_index = old_anchor_index;
    if (anchor_id != nullptr) {
        for (std::uint16_t index = 0; index < count; ++index) {
            if (items[index].id != nullptr &&
                std::strcmp(items[index].id, anchor_id) == 0) {
                new_anchor_index = index;
                break;
            }
        }
    }
    state->items = items;
    state->count = count;
    state->first_mounted =
        std::numeric_limits<std::uint16_t>::max();
    state->mounted_count = 0;
    lv_obj_set_height(
        state->content,
        static_cast<std::int32_t>(count) *
            kTransformingItemPitch);
    const auto preserved_scroll = preserve_anchor_scroll_offset(
        old_scroll_y,
        static_cast<std::int32_t>(old_anchor_index) *
            kTransformingItemPitch,
        static_cast<std::int32_t>(new_anchor_index) *
            kTransformingItemPitch);
    lv_obj_scroll_to_y(
        object,
        std::max<std::int32_t>(0, preserved_scroll),
        LV_ANIM_OFF);
    refresh_transforming_list(*state);
    return true;
}

lv_obj_t* ComponentFactory::dialog(
    lv_obj_t* root,
    const char* title_value,
    const char* body_value,
    const char* confirm_label,
    const char* dismiss_label) {
    auto* scrim = lv_obj_create(root);
    reset(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    auto* panel = lv_obj_create(scrim);
    reset(panel);
    lv_obj_set_size(panel, 204, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(panel, 132, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(panel, styles_.get(StyleRole::surface_high), 0);
    lv_obj_center(panel);
    text(panel, title_value, generated::TypographyRole::title_medium);
    auto* body = text(
        panel, body_value, generated::TypographyRole::body_small, true);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    auto* actions = lv_obj_create(panel);
    reset(actions);
    lv_obj_set_size(actions, LV_PCT(100), 40);
    lv_obj_set_style_pad_column(actions, 6, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_REVERSE);
    auto* confirm = button(
        actions,
        {"confirm", confirm_label, Tone::primary, ButtonVariant::filled,
         ComponentSize::compact, true, false});
    lv_obj_set_flex_grow(confirm, 1);
    if (dismiss_label != nullptr) {
        auto* dismiss = button(
            actions,
            {"dismiss", dismiss_label, Tone::neutral, ButtonVariant::text,
             ComponentSize::compact, true, false});
        lv_obj_set_flex_grow(dismiss, 1);
    }
    return scrim;
}

lv_obj_t* ComponentFactory::confirmation_dialog(
    lv_obj_t* root,
    const char* title_value,
    const char* body_value,
    bool success) {
    auto* scrim = dialog(
        root,
        title_value,
        body_value,
        "Done",
        nullptr);
    auto* panel = lv_obj_get_child(scrim, 0);
    if (panel != nullptr) {
        auto* status = icon(
            panel,
            success ? IconName::check : IconName::warning,
            24,
            false);
        lv_obj_move_to_index(status, 0);
    }
    return scrim;
}

lv_obj_t* ComponentFactory::voice_orb(
    lv_obj_t* parent,
    const char* state_label,
    Tone tone) {
    auto* orb = lv_button_create(parent);
    reset(orb);
    lv_obj_set_size(orb, 88, 88);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(orb, styles_.get(fill_style(tone)), 0);
    lv_obj_add_style(
        orb,
        styles_.get(StyleRole::pressed),
        static_cast<lv_style_selector_t>(LV_PART_MAIN) |
            static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
    auto* symbol = lv_label_create(orb);
    reset(symbol);
    lv_label_set_text(symbol, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(symbol, &lv_font_montserrat_18, 0);
    lv_obj_add_style(symbol, styles_.get(content_style(tone)), 0);
    lv_obj_align(symbol, LV_ALIGN_CENTER, 0, -8);
    auto* state = lv_label_create(orb);
    reset(state);
    lv_label_set_text(state, state_label == nullptr ? "" : state_label);
    lv_obj_set_style_text_font(state, &lv_font_montserrat_10, 0);
    lv_obj_add_style(state, styles_.get(content_style(tone)), 0);
    lv_obj_align(state, LV_ALIGN_CENTER, 0, 16);
    return orb;
}

lv_obj_t* ComponentFactory::transcript(
    lv_obj_t* parent,
    const char* final_text,
    const char* partial_text) {
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(container, 88, 0);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_pad_row(container, 4, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(container, styles_.get(StyleRole::surface), 0);
    auto* final_label = text(
        container,
        final_text == nullptr ? "" : final_text,
        generated::TypographyRole::body_medium);
    lv_label_set_long_mode(final_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(final_label, LV_PCT(100));
    if (partial_text != nullptr && partial_text[0] != '\0') {
        auto* partial_label = text(
            container,
            partial_text,
            generated::TypographyRole::body_small,
            true);
        lv_label_set_long_mode(partial_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(partial_label, LV_PCT(100));
    }
    return container;
}

lv_obj_t* ComponentFactory::change_review(
    lv_obj_t* parent,
    const ChangeReviewProps& props) {
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_pad_row(container, 4, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(container, styles_.get(StyleRole::surface), 0);
    text(
        container,
        props.entity,
        generated::TypographyRole::label_medium,
        true);
    text(
        container,
        props.field,
        generated::TypographyRole::title_small);
    char change[96]{};
    std::snprintf(
        change,
        sizeof(change),
        "%s  to  %s",
        props.old_value == nullptr ? "" : props.old_value,
        props.new_value == nullptr ? "" : props.new_value);
    auto* value = text(
        container,
        change,
        generated::TypographyRole::body_medium);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_width(value, LV_PCT(100));
    return container;
}

lv_obj_t* ComponentFactory::build_progress(
    lv_obj_t* parent,
    const BuildProgressProps& props) {
    auto* container = lv_obj_create(parent);
    reset(container);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 10, 0);
    lv_obj_set_style_pad_row(container, 6, 0);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(container, styles_.get(StyleRole::surface), 0);
    text(
        container,
        props.stage,
        generated::TypographyRole::title_small);
    linear_progress(
        container,
        {
            props.stage,
            props.stage_index,
            std::max<std::uint8_t>(1, props.stage_count),
            Tone::primary,
        });
    if (props.cancellable) {
        auto* cancel = button(
            container,
            {
                "cancel",
                "Cancel",
                Tone::neutral,
                ButtonVariant::text,
                ComponentSize::compact,
                true,
                false,
            });
        lv_obj_set_width(cancel, LV_PCT(100));
    }
    return container;
}

lv_obj_t* ComponentFactory::permission_review(
    lv_obj_t* parent,
    const char* capability,
    const char* explanation) {
    auto* container = card(
        parent,
        {
            capability,
            explanation,
            Tone::neutral,
            false,
        });
    auto* leading = icon(
        container, IconName::information, 18, true);
    lv_obj_move_to_index(leading, 0);
    return container;
}

lv_obj_t* ComponentFactory::clarification_choice_group(
    lv_obj_t* parent,
    const char* const* choices,
    std::uint8_t choice_count,
    const char* cancel_label) {
    if (choices == nullptr || choice_count == 0) return nullptr;
    choice_count = std::min<std::uint8_t>(choice_count, 3);
    auto* group = lv_obj_create(parent);
    reset(group);
    lv_obj_set_size(group, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(group, 6, 0);
    lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
    for (std::uint8_t index = 0; index < choice_count; ++index) {
        auto* choice = button(
            group,
            {
                "clarification.choice",
                choices[index],
                index == 0 ? Tone::primary : Tone::neutral,
                index == 0
                    ? ButtonVariant::filled
                    : ButtonVariant::tonal,
                ComponentSize::compact,
                true,
                false,
            });
        lv_obj_set_width(choice, LV_PCT(100));
    }
    if (cancel_label != nullptr && cancel_label[0] != '\0') {
        auto* cancel = button(
            group,
            {
                "clarification.cancel",
                cancel_label,
                Tone::neutral,
                ButtonVariant::text,
                ComponentSize::compact,
                true,
                false,
            });
        lv_obj_set_width(cancel, LV_PCT(100));
    }
    return group;
}

lv_obj_t* ComponentFactory::live_card(
    lv_obj_t* parent,
    const LiveCardProps& props) {
    auto* container = card(
        parent,
        {
            props.primary,
            props.secondary,
            props.tone,
            true,
        });
    lv_obj_set_user_data(
        container,
        const_cast<char*>(
            props.app_name == nullptr ? "" : props.app_name));
    auto* app = text(
        container,
        props.app_name,
        generated::TypographyRole::label_small,
        true);
    if (props.tone != Tone::neutral) {
        lv_obj_add_style(
            app, styles_.get(content_style(props.tone)), 0);
        lv_obj_set_style_opa(app, LV_OPA_70, 0);
    }
    lv_obj_move_to_index(app, 0);
    if (props.progress_maximum > 0) {
        linear_progress(
            container,
            {
                props.primary,
                props.progress,
                props.progress_maximum,
                props.tone,
            });
    }
    if (props.freshness != nullptr &&
        props.freshness[0] != '\0') {
        auto* freshness = text(
            container,
            props.freshness,
            generated::TypographyRole::body_extra_small,
            true);
        if (props.tone != Tone::neutral) {
            lv_obj_add_style(
                freshness,
                styles_.get(content_style(props.tone)),
                0);
            lv_obj_set_style_opa(freshness, LV_OPA_70, 0);
        }
    }
    return container;
}

lv_obj_t* ComponentFactory::status_chip(
    lv_obj_t* parent,
    const char* label,
    IconName status_icon,
    Tone tone) {
    auto* chip = lv_obj_create(parent);
    reset(chip);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_min_width(chip, 48, 0);
    lv_obj_set_style_pad_hor(chip, 10, 0);
    lv_obj_set_style_pad_column(chip, 6, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        chip,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(chip, styles_.get(fill_style(tone)), 0);
    auto* status = icon(chip, status_icon, 14, false);
    lv_obj_add_style(status, styles_.get(content_style(tone)), 0);
    auto* value = text(
        chip,
        label,
        generated::TypographyRole::label_small);
    lv_obj_add_style(value, styles_.get(content_style(tone)), 0);
    return chip;
}

lv_obj_t* ComponentFactory::voice_overlay(
    lv_obj_t* root,
    const char* status,
    const char* transcript_text,
    Tone orb_tone) {
    auto* scrim = lv_obj_create(root);
    reset(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_80, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    auto* panel = lv_obj_create(scrim);
    reset(panel);
    lv_obj_set_size(panel, 216, 216);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        panel,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_add_style(panel, styles_.get(StyleRole::surface_low), 0);
    lv_obj_center(panel);
    voice_orb(panel, status, orb_tone);
    transcript(panel, transcript_text, nullptr);
    auto* cancel = icon_button(
        panel,
        "voice.cancel",
        IconName::close,
        ButtonVariant::text,
        ComponentSize::compact);
    lv_obj_set_user_data(cancel, const_cast<char*>("voice.cancel"));
    return scrim;
}

}  // namespace m3e
