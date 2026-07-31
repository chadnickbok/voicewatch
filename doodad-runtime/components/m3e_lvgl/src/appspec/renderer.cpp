#include "m3e/appspec/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "m3e/components/components.hpp"
#include "m3e/foundation/display_profile.hpp"
#include "m3e/foundation/semantic_tokens.hpp"

LV_FONT_DECLARE(m3e_calculator_font_20);
LV_FONT_DECLARE(m3e_calculator_result_font_40);

namespace m3e::appspec {
namespace {

struct CalculatorKeyColors {
    lv_color_t container;
    lv_color_t content;
};

std::int32_t px(std::int32_t dp) {
    return dp_edge_to_px(dp, watch_square_192.density_q8_8);
}

std::int32_t gap_px(std::uint8_t gap) {
    const auto role =
        gap == 0 ? SpacingRole::none :
        gap == 1 ? SpacingRole::xs :
        gap == 2 ? SpacingRole::sm :
        gap == 3 ? SpacingRole::md : SpacingRole::lg;
    return px(spacing_dp(role));
}

lv_flex_align_t align(std::uint8_t value) {
    switch (value) {
        case 0: return LV_FLEX_ALIGN_START;
        case 2: return LV_FLEX_ALIGN_END;
        case 3: return LV_FLEX_ALIGN_SPACE_EVENLY;
        default: return LV_FLEX_ALIGN_CENTER;
    }
}

generated::TypographyRole typography(std::uint8_t value) {
    using generated::TypographyRole;
    switch (value) {
        case 0: return TypographyRole::display_medium;
        case 1: return TypographyRole::title_medium;
        case 2: return TypographyRole::label_medium;
        case 4: return TypographyRole::numeral_large;
        case 5: return TypographyRole::body_extra_small;
        default: return TypographyRole::body_medium;
    }
}

bool is_keypad_document(const WireDocument& document) {
    return std::any_of(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [](const WireNode& node) {
            return node.kind == ComponentKind::keypad;
        });
}

const char* calculator_glyph(const char* key) {
    if (std::strcmp(key, "+/-") == 0) return "±";
    if (std::strcmp(key, "/") == 0) return "÷";
    if (std::strcmp(key, "*") == 0) return "×";
    if (std::strcmp(key, "<-") == 0) return "⌫";
    return key;
}

CalculatorKeyColors calculator_key_colors(
    bool is_operator,
    bool is_utility) {
    // Keep the production renderer on the exact violet-dark roles used by
    // ReferenceTheme. LVGL quantizes these colors to RGB565 on hardware.
    if (is_operator) {
        return {
            lv_color_make(0xD8, 0xB9, 0xFF),
            lv_color_make(0x35, 0x11, 0x51),
        };
    }
    if (is_utility) {
        return {
            lv_color_make(0xCF, 0xC0, 0xDA),
            lv_color_make(0x34, 0x2B, 0x3D),
        };
    }
    return {
        lv_color_make(0x49, 0x44, 0x53),
        lv_color_make(0xF6, 0xED, 0xFF),
    };
}

Tone tone(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(Tone::error)
        ? static_cast<Tone>(value)
        : Tone::primary;
}

ComponentSize component_size(std::uint8_t value) {
    switch (value) {
        case 0: return ComponentSize::compact;
        case 2: return ComponentSize::large;
        default: return ComponentSize::normal;
    }
}

lv_obj_t* layout(
    ComponentFactory& factory,
    lv_obj_t* parent,
    const WireNode& node,
    bool scroll) {
    auto* object = lv_obj_create(parent);
    factory.reset(object);
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_height(object, scroll ? LV_PCT(100) : LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(object, gap_px(node.gap), 0);
    lv_obj_set_flex_flow(
        object,
        node.kind == ComponentKind::row
            ? LV_FLEX_FLOW_ROW
            : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        object,
        align(node.alignment),
        align(node.alignment),
        align(node.alignment));
    if (scroll) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(object, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_AUTO);
    }
    return object;
}

lv_event_code_t lvgl_event_code(EventKind kind) {
    switch (kind) {
        case EventKind::tap: return LV_EVENT_CLICKED;
        case EventKind::long_press: return LV_EVENT_LONG_PRESSED;
        case EventKind::repeat: return LV_EVENT_LONG_PRESSED_REPEAT;
        case EventKind::value_changing:
        case EventKind::checked_changed:
        case EventKind::page_changed:
            return LV_EVENT_VALUE_CHANGED;
        case EventKind::value_committed:
        case EventKind::submit:
            return LV_EVENT_RELEASED;
        case EventKind::dismissed:
        case EventKind::cancel:
            return LV_EVENT_CANCEL;
        case EventKind::retry:
            return LV_EVENT_CLICKED;
    }
    return LV_EVENT_CLICKED;
}

void dispatch_event(lv_event_t* lv_event) {
    auto* binding = static_cast<MountedEventBinding*>(
        lv_event_get_user_data(lv_event));
    auto* event = binding == nullptr ? nullptr : binding->event;
    if (event == nullptr || event->document == nullptr ||
        event->sink == nullptr ||
        event->node_index >= event->document->node_count) {
        return;
    }
    const auto& document = *event->document;
    const auto& node = document.nodes[event->node_index];
    EventValue value{};
    switch (binding->value_kind) {
        case MountedEventValue::none:
            break;
        case MountedEventValue::integer:
            value = EventValue::integer(binding->integer_value);
            break;
        case MountedEventValue::stepper_decrement:
            value = EventValue::integer(std::max(
                node.minimum, node.value - node.step));
            break;
        case MountedEventValue::stepper_increment:
            value = EventValue::integer(std::min(
                node.maximum, node.value + node.step));
            break;
        case MountedEventValue::checked_state:
            value = EventValue::boolean(
                node.mounted_object != nullptr &&
                lv_obj_has_state(
                    static_cast<lv_obj_t*>(node.mounted_object),
                    LV_STATE_CHECKED));
            break;
        case MountedEventValue::keypad_key:
            if (binding->key_index >= document.key_count) return;
            value = EventValue::text(document.string_at(
                document.key_offsets[binding->key_index]));
            break;
    }
    const UiEvent envelope{
        1,
        document.string_at(document.app_id_offset),
        document.string_at(document.nodes[0].id_offset),
        document.string_at(node.id_offset),
        document.string_at(event->action_id_offset),
        event->kind,
        lv_tick_get(),
        value,
    };
    event->sink(envelope, event->sink_context);
}

bool bind_event(
    WireDocument& document,
    WireEvent& event,
    lv_obj_t* object,
    MountedEventValue value_kind = MountedEventValue::none,
    std::int32_t integer_value = 0,
    std::uint16_t key_index = 0) {
    if (object == nullptr ||
        document.mounted_event_binding_count >=
            document.mounted_event_bindings.size()) {
        return false;
    }
    auto& binding =
        document.mounted_event_bindings[
            document.mounted_event_binding_count++];
    binding = {&event, value_kind, integer_value, key_index};
    lv_obj_add_event_cb(
        object,
        dispatch_event,
        lvgl_event_code(event.kind),
        &binding);
    return true;
}

}  // namespace

Renderer::Renderer(StyleRegistry& styles) : styles_(styles) {}

bool Renderer::mount(
    lv_obj_t* root,
    WireDocument& document,
    WireEventSink event_sink,
    void* event_context) {
    if (root == nullptr || document.node_count == 0 ||
        !styles_.initialized()) {
        return false;
    }
    std::size_t required_bindings = 0;
    if (event_sink != nullptr) {
        for (std::size_t index = 1;
             index < document.node_count;
             ++index) {
            const auto& node = document.nodes[index];
            const auto multiplier =
                node.kind == ComponentKind::keypad
                    ? static_cast<std::size_t>(node.key_count)
                    : node.kind == ComponentKind::stepper ? 2U : 1U;
            required_bindings +=
                static_cast<std::size_t>(node.event_count) *
                multiplier;
            if (required_bindings >
                document.mounted_event_bindings.size()) {
                return false;
            }
        }
    }
    document.mounted_event_binding_count = 0;
    ComponentFactory factory(styles_);
    std::array<lv_obj_t*, Reconciler::kCapacity> objects{};
    const auto keypad_document = is_keypad_document(document);
    objects[0] = factory.screen(root);
    document.nodes[0].mounted_object = root;
    lv_obj_set_style_pad_all(root, keypad_document ? 5 : px(12), 0);
    lv_obj_set_style_pad_gap(
        root, keypad_document ? 4 : gap_px(document.nodes[0].gap), 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        root,
        keypad_document
            ? LV_FLEX_ALIGN_START
            : align(document.nodes[0].alignment),
        keypad_document
            ? LV_FLEX_ALIGN_CENTER
            : align(document.nodes[0].alignment),
        keypad_document
            ? LV_FLEX_ALIGN_CENTER
            : align(document.nodes[0].alignment));

    for (std::size_t index = 1; index < document.node_count; ++index) {
        auto& node = document.nodes[index];
        if (node.parent_index >= index ||
            objects[node.parent_index] == nullptr) {
            return false;
        }
        auto* parent = objects[node.parent_index];
        const auto* primary =
            document.string_at(node.primary_text_offset);
        const auto* secondary =
            document.string_at(node.secondary_text_offset);
        lv_obj_t* object = nullptr;
        switch (node.kind) {
            case ComponentKind::column:
            case ComponentKind::row:
                object = layout(factory, parent, node, false);
                break;
            case ComponentKind::scroll:
                object = layout(factory, parent, node, true);
                break;
            case ComponentKind::text:
                object = factory.text(
                    parent, primary, typography(node.variant));
                lv_obj_set_width(object, LV_PCT(100));
                lv_label_set_long_mode(
                    object,
                    keypad_document && node.variant == 4
                        ? LV_LABEL_LONG_DOT
                        : LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(
                    object,
                    node.alignment == 0
                        ? LV_TEXT_ALIGN_LEFT
                        : node.alignment == 2
                            ? LV_TEXT_ALIGN_RIGHT
                            : LV_TEXT_ALIGN_CENTER,
                    0);
                if (keypad_document && node.variant == 4) {
                    lv_obj_set_height(object, 50);
                    lv_obj_set_style_text_font(
                        object, &m3e_calculator_result_font_40, 0);
                    lv_obj_set_style_pad_top(object, 11, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_RIGHT, 0);
                }
                break;
            case ComponentKind::button:
                object = factory.button(
                    parent,
                    {
                        document.string_at(node.id_offset),
                        primary,
                        tone(node.tone),
                        node.variant <=
                                static_cast<std::uint8_t>(
                                    ButtonVariant::text)
                            ? static_cast<ButtonVariant>(node.variant)
                            : ButtonVariant::filled,
                        component_size(node.size),
                        node.enabled,
                        false,
                    });
                lv_obj_set_width(object, LV_PCT(100));
                break;
            case ComponentKind::card:
                object = factory.card(
                    parent,
                    {primary, secondary, tone(node.tone), false});
                break;
            case ComponentKind::live_card:
                object = factory.live_card(
                    parent,
                    {
                        document.string_at(node.semantic_label_offset),
                        primary,
                        secondary,
                        nullptr,
                        node.value,
                        node.maximum,
                        tone(node.tone),
                    });
                break;
            case ComponentKind::progress:
                object = node.variant == 1
                    ? factory.circular_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)})
                    : factory.linear_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)});
                break;
            case ComponentKind::stepper:
                object = factory.stepper(
                    parent,
                    {primary, node.value, secondary, false, false});
                break;
            case ComponentKind::toggle:
                object = factory.toggle_row(
                    parent, primary, node.value != 0, node.enabled);
                lv_obj_add_flag(object, LV_OBJ_FLAG_CHECKABLE);
                if (node.value != 0) {
                    lv_obj_add_state(object, LV_STATE_CHECKED);
                }
                break;
            case ComponentKind::voice_orb:
                object = factory.voice_orb(
                    parent, primary, tone(node.tone));
                break;
            case ComponentKind::keypad: {
                object = layout(factory, parent, node, false);
                lv_obj_set_height(object, 176);
                lv_obj_set_style_pad_row(object, 4, 0);
                for (std::uint8_t key_index = 0;
                     key_index < node.key_count;) {
                    WireNode row_node{};
                    row_node.kind = ComponentKind::row;
                    row_node.gap = 1;
                    row_node.alignment = 0;
                    auto* row =
                        layout(factory, object, row_node, false);
                    lv_obj_set_height(row, 32);
                    lv_obj_set_style_pad_column(row, 4, 0);
                    lv_obj_set_flex_align(
                        row,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    for (std::uint8_t column = 0;
                         column < node.key_columns &&
                         key_index < node.key_count;
                         ++column, ++key_index) {
                        const auto offset =
                            document.key_offsets[
                                node.key_start + key_index];
                        const auto* key_text =
                            document.string_at(offset);
                        const auto is_operator =
                            column == node.key_columns - 1;
                        const auto is_utility =
                            key_index < node.key_columns ||
                            std::strcmp(key_text, "<-") == 0;
                        const auto colors = calculator_key_colors(
                            is_operator, is_utility);
                        auto* key = factory.button(
                            row,
                            {
                                "key",
                                calculator_glyph(key_text),
                                is_operator
                                    ? Tone::primary
                                    : is_utility
                                        ? Tone::secondary
                                        : Tone::neutral,
                                ButtonVariant::tonal,
                                ComponentSize::compact,
                                node.enabled,
                                false,
                            });
                        lv_obj_set_height(key, 32);
                        lv_obj_set_style_pad_all(key, 0, 0);
                        lv_obj_set_style_radius(key, 15, 0);
                        lv_obj_set_style_bg_color(
                            key, colors.container, 0);
                        lv_obj_set_style_transform_scale(
                            key,
                            241,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_PRESSED));
                        lv_obj_set_style_opa(
                            key,
                            LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_PRESSED));
                        lv_obj_set_flex_grow(key, 1);
                        if (lv_obj_get_child_count(key) == 1) {
                            auto* label = lv_obj_get_child(key, 0);
                            lv_obj_set_style_text_font(
                                label,
                                &m3e_calculator_font_20,
                                0);
                            lv_obj_set_style_text_color(
                                label, colors.content, 0);
                        }
                        if (event_sink != nullptr) {
                            for (std::size_t event_index = 0;
                                 event_index < document.event_count;
                                 ++event_index) {
                                auto& event =
                                    document.events[event_index];
                                if (event.node_index != index) {
                                    continue;
                                }
                                if (!bind_event(
                                        document,
                                        event,
                                        key,
                                        MountedEventValue::keypad_key,
                                        0,
                                        static_cast<std::uint16_t>(
                                            node.key_start +
                                            key_index))) {
                                    return false;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case ComponentKind::screen:
                return false;
        }
        if (object == nullptr) return false;
        lv_obj_set_user_data(
            object,
            const_cast<char*>(
                document.string_at(node.id_offset)));
        if (!node.visible) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        if (!node.enabled) lv_obj_add_state(object, LV_STATE_DISABLED);
        if (event_sink != nullptr) {
            for (std::size_t event_index = 0;
                 event_index < document.event_count;
                 ++event_index) {
                auto& event = document.events[event_index];
                if (event.node_index != index) continue;
                event.document = &document;
                event.sink = event_sink;
                event.sink_context = event_context;
                if (node.kind == ComponentKind::keypad) {
                    continue;
                }
                if (node.kind == ComponentKind::stepper) {
                    if (lv_obj_get_child_count(object) < 3 ||
                        !bind_event(
                            document,
                            event,
                            lv_obj_get_child(object, 0),
                            MountedEventValue::stepper_decrement) ||
                        !bind_event(
                            document,
                            event,
                            lv_obj_get_child(object, 2),
                            MountedEventValue::stepper_increment)) {
                        return false;
                    }
                    continue;
                }
                if (!bind_event(
                        document,
                        event,
                        object,
                        node.kind == ComponentKind::toggle
                            ? MountedEventValue::checked_state
                            : MountedEventValue::none)) {
                    return false;
                }
            }
        }
        objects[index] = object;
        node.mounted_object = object;
    }
    return true;
}

}  // namespace m3e::appspec
