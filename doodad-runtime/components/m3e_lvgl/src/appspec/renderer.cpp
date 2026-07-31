#include "m3e/appspec/renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "m3e/components/components.hpp"
#include "m3e/assets/image_assets.hpp"
#include "m3e/foundation/display_profile.hpp"
#include "m3e/foundation/semantic_tokens.hpp"

LV_FONT_DECLARE(m3e_calculator_font_20);
LV_FONT_DECLARE(m3e_calculator_result_font_40);
LV_FONT_DECLARE(m3e_timer_font_55);
LV_FONT_DECLARE(m3e_timer_value_font_28);
LV_FONT_DECLARE(m3e_weather_font_55);
LV_FONT_DECLARE(m3e_nutrition_font_32);
LV_FONT_DECLARE(m3e_live_action_font_32);

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

bool is_countdown_document(const WireDocument& document) {
    if (document.node_count != 5 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    bool progress = false;
    bool numeral = false;
    bool stepper = false;
    bool button = false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        switch (node.kind) {
            case ComponentKind::progress:
                progress = node.variant == 1;
                break;
            case ComponentKind::text:
                numeral = node.variant == 4;
                break;
            case ComponentKind::stepper:
                stepper = true;
                break;
            case ComponentKind::button:
                button = true;
                break;
            default:
                return false;
        }
    }
    return progress && numeral && stepper && button;
}

bool is_weather_hero_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 6) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 4 && cards == 1 && buttons == 1;
}

bool is_notification_stack_document(const WireDocument& document) {
    if (document.node_count < 5 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 2; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 1) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return document.nodes[1].child_count ==
            texts + cards + buttons &&
        texts == 1 &&
        cards >= 1 && cards <= 2 &&
        (buttons == 1 || buttons == 3);
}

bool is_calendar_agenda_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0 ||
        document.nodes[1].child_count != 1 ||
        document.nodes[2].kind != ComponentKind::column ||
        document.nodes[2].parent_index != 1 ||
        document.nodes[2].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 3; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 2) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 1 &&
        ((cards == 2 && buttons == 1) ||
         (cards == 1 && buttons == 2));
}

bool is_task_list_document(const WireDocument& document) {
    if (document.node_count < 5 ||
        document.nodes[0].child_count != 1 ||
        document.nodes[1].kind != ComponentKind::scroll ||
        document.nodes[1].parent_index != 0) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t toggles = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 2; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 1) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::toggle) {
            ++toggles;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return document.nodes[1].child_count ==
            texts + toggles + buttons &&
        texts == 1 &&
        ((toggles == 2 && buttons == 1) ||
         (toggles == 3 && buttons == 0));
}

bool is_workout_set_document(const WireDocument& document) {
    if (document.node_count != 5 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t steppers = 0;
    std::size_t live_cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::stepper) {
            ++steppers;
        } else if (node.kind == ComponentKind::live_card) {
            ++live_cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 1 && steppers == 1 &&
        live_cards == 1 && buttons == 1;
}

bool is_workout_rest_document(const WireDocument& document) {
    if (document.node_count != 6 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t live_cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::live_card) {
            ++live_cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 2 && live_cards == 1 && buttons == 2;
}

bool is_workout_summary_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t root_texts = 0;
    std::size_t rows = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    std::size_t row_texts = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++root_texts;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                if (node.child_count != 2) return false;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::button) {
                ++buttons;
            } else {
                return false;
            }
        } else if (node.parent_index == 2 &&
                   node.kind == ComponentKind::text) {
            ++row_texts;
        } else {
            return false;
        }
    }
    return root_texts == 1 && rows == 1 && row_texts == 2 &&
        cards == 1 && buttons == 1;
}

bool is_nutrition_dashboard_document(
    const WireDocument& document) {
    if (document.node_count != 6 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t progress = 0;
    std::size_t cards = 0;
    std::size_t buttons = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::progress) {
            ++progress;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::button) {
            ++buttons;
        } else {
            return false;
        }
    }
    return texts == 2 && progress == 1 &&
        cards == 1 && buttons == 1;
}

bool is_nutrition_quick_add_document(
    const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t steppers = 0;
    std::size_t cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::stepper) {
                ++steppers;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 1 && steppers == 1 &&
        cards == 1 && rows == 1 && buttons == 2;
}

bool is_nutrition_review_document(
    const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::card) {
                ++cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 2 && cards == 1 &&
        rows == 1 && buttons == 2;
}

bool is_voice_ready_document(const WireDocument& document) {
    if (document.node_count != 4 ||
        document.nodes[0].child_count != 3) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t cards = 0;
    std::size_t orbs = 0;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index != 0) return false;
        if (node.kind == ComponentKind::text) {
            ++texts;
        } else if (node.kind == ComponentKind::card) {
            ++cards;
        } else if (node.kind == ComponentKind::voice_orb) {
            ++orbs;
        } else {
            return false;
        }
    }
    return texts == 1 && cards == 1 && orbs == 1;
}

bool is_live_action_detail_document(const WireDocument& document) {
    if (document.node_count != 7 ||
        document.nodes[0].child_count != 4) {
        return false;
    }
    std::size_t texts = 0;
    std::size_t live_cards = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::live_card) {
                ++live_cards;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return texts == 2 && live_cards == 1 &&
        rows == 1 && buttons == 2;
}

bool is_media_player_document(const WireDocument& document) {
    if (document.node_count != 8 ||
        document.nodes[0].child_count != 5) {
        return false;
    }
    std::size_t images = 0;
    std::size_t texts = 0;
    std::size_t progress = 0;
    std::size_t rows = 0;
    std::size_t buttons = 0;
    std::size_t row_index = Reconciler::kCapacity;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == 0) {
            if (node.kind == ComponentKind::image) {
                ++images;
            } else if (node.kind == ComponentKind::text) {
                ++texts;
            } else if (node.kind == ComponentKind::progress) {
                ++progress;
            } else if (node.kind == ComponentKind::row) {
                ++rows;
                row_index = index;
                if (node.child_count != 2) return false;
            } else {
                return false;
            }
        }
    }
    if (row_index == Reconciler::kCapacity) return false;
    for (std::size_t index = 1; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (node.parent_index == row_index &&
            node.kind == ComponentKind::button) {
            ++buttons;
        } else if (node.parent_index != 0) {
            return false;
        }
    }
    return images == 1 && texts == 2 && progress == 1 &&
        rows == 1 && buttons == 2;
}

std::size_t count_kind(
    const WireDocument& document,
    ComponentKind kind) {
    return static_cast<std::size_t>(std::count_if(
        document.nodes.begin() + 1,
        document.nodes.begin() + document.node_count,
        [kind](const WireNode& node) {
            return node.kind == kind;
        }));
}

std::size_t kind_ordinal(
    const WireDocument& document,
    std::size_t node_index,
    ComponentKind kind) {
    return static_cast<std::size_t>(std::count_if(
        document.nodes.begin() + 1,
        document.nodes.begin() + node_index,
        [kind](const WireNode& node) {
            return node.kind == kind;
        }));
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
    const auto countdown_document = is_countdown_document(document);
    const auto weather_hero_document =
        is_weather_hero_document(document);
    const auto notification_stack_document =
        is_notification_stack_document(document);
    const auto calendar_agenda_document =
        is_calendar_agenda_document(document);
    const auto task_list_document =
        is_task_list_document(document);
    const auto workout_set_document =
        is_workout_set_document(document);
    const auto workout_rest_document =
        is_workout_rest_document(document);
    const auto workout_summary_document =
        is_workout_summary_document(document);
    const auto nutrition_dashboard_document =
        is_nutrition_dashboard_document(document);
    const auto nutrition_quick_add_document =
        is_nutrition_quick_add_document(document);
    const auto nutrition_review_document =
        is_nutrition_review_document(document);
    const auto voice_ready_document =
        is_voice_ready_document(document);
    const auto live_action_detail_document =
        is_live_action_detail_document(document);
    const auto media_player_document =
        is_media_player_document(document);
    const auto task_toggle_count =
        task_list_document
            ? count_kind(document, ComponentKind::toggle)
            : 0U;
    const auto notification_card_count =
        notification_stack_document
            ? count_kind(document, ComponentKind::card)
            : 0U;
    const auto notification_button_count =
        notification_stack_document
            ? count_kind(document, ComponentKind::button)
            : 0U;
    const auto calendar_card_count =
        calendar_agenda_document
            ? count_kind(document, ComponentKind::card)
            : 0U;
    const auto calendar_button_count =
        calendar_agenda_document
            ? count_kind(document, ComponentKind::button)
            : 0U;
    objects[0] = factory.screen(root);
    document.nodes[0].mounted_object = root;
    if (voice_ready_document || live_action_detail_document ||
        media_player_document) {
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_set_style_pad_all(
        root,
        keypad_document
            ? 5
            : countdown_document || weather_hero_document ||
                    notification_stack_document ||
                    calendar_agenda_document || task_list_document ||
                    workout_set_document || workout_rest_document ||
                    workout_summary_document ||
                    nutrition_dashboard_document ||
                    nutrition_quick_add_document ||
                    nutrition_review_document ||
                    voice_ready_document ||
                    live_action_detail_document
                    || media_player_document
                ? 0
                : px(12),
        0);
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
                if (calendar_agenda_document &&
                    node.kind == ComponentKind::column) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (workout_summary_document &&
                           node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(44));
                    lv_obj_set_pos(object, px(4), px(28));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (
                    (nutrition_quick_add_document ||
                     nutrition_review_document ||
                    live_action_detail_document) &&
                    node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(48));
                    lv_obj_set_pos(object, px(4), px(136));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (media_player_document &&
                           node.kind == ComponentKind::row) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(80));
                    lv_obj_set_pos(object, px(4), px(104));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                }
                break;
            case ComponentKind::scroll:
                object = layout(factory, parent, node, true);
                if (notification_stack_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (calendar_agenda_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_size(object, px(184), px(184));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                }
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
                } else if (countdown_document && node.variant == 4) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(112), px(58));
                    lv_obj_set_pos(
                        object,
                        (240 - px(112)) / 2,
                        px(41));
                    lv_obj_set_style_text_font(
                        object, &m3e_timer_font_55, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object, px(10), 0);
                } else if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    if (node.variant == 1) {
                        lv_obj_set_size(object, px(44), px(44));
                        lv_obj_set_pos(object, px(16), px(56));
                        lv_obj_set_style_text_opa(
                            object, LV_OPA_TRANSP, 0);

                        auto* halo = lv_obj_create(object);
                        ComponentFactory::reset(halo);
                        lv_obj_set_size(halo, px(40), px(40));
                        lv_obj_center(halo);
                        lv_obj_set_style_radius(
                            halo, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            halo,
                            lv_color_make(0xFF, 0xDC, 0xC2),
                            0);
                        lv_obj_set_style_bg_opa(halo, 82, 0);

                        auto* core = lv_obj_create(object);
                        ComponentFactory::reset(core);
                        lv_obj_set_size(core, px(28), px(28));
                        lv_obj_center(core);
                        lv_obj_set_style_radius(
                            core, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            core,
                            lv_color_make(0xFF, 0xDC, 0xC2),
                            0);
                        lv_obj_set_style_bg_opa(
                            core, LV_OPA_COVER, 0);
                    } else if (node.variant == 2) {
                        lv_obj_set_size(object, px(184), px(24));
                        lv_obj_set_pos(object, px(4), px(4));
                        lv_obj_set_style_text_font(
                            object, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_CENTER, 0);
                        lv_obj_set_style_pad_top(object, px(3), 0);
                    } else if (node.variant == 4) {
                        lv_obj_set_size(object, px(112), px(64));
                        lv_obj_set_pos(object, px(60), px(48));
                        lv_obj_set_style_text_font(
                            object, &m3e_weather_font_55, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_CENTER, 0);
                        lv_obj_set_style_pad_top(object, px(8), 0);
                    } else if (node.variant == 5) {
                        lv_obj_set_size(object, px(108), px(16));
                        lv_obj_set_pos(object, px(20), px(168));
                        lv_obj_set_style_text_font(
                            object, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_opa(object, 184, 0);
                        lv_obj_set_style_text_align(
                            object, LV_TEXT_ALIGN_LEFT, 0);
                    }
                } else if (notification_stack_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (calendar_agenda_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, 0, 0);
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_set_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_rest_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        node.variant == 4 ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        node.variant == 4 ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        node.variant == 4
                            ? &m3e_timer_value_font_28
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        node.variant == 4
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        node.variant == 4 ? px(5) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (workout_summary_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    if (node.parent_index == 0) {
                        lv_obj_set_size(object, px(184), px(20));
                        lv_obj_set_pos(object, px(4), px(4));
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                    } else {
                        const auto ordinal =
                            kind_ordinal(
                                document, index, ComponentKind::text) -
                            1;
                        lv_obj_set_size(
                            object,
                            ordinal == 0 ? 112 : 113,
                            px(44));
                        lv_obj_set_pos(
                            object,
                            ordinal == 0 ? 0 : 117,
                            0);
                        lv_obj_set_style_radius(object, px(16), 0);
                        lv_obj_set_style_bg_color(
                            object,
                            lv_color_make(0x33, 0x2E, 0x3C),
                            0);
                        lv_obj_set_style_bg_opa(
                            object, LV_OPA_COVER, 0);
                        lv_obj_set_style_text_color(
                            object,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                    }
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        node.parent_index == 0 ? px(2) : px(12),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (
                    nutrition_dashboard_document ||
                    nutrition_review_document) {
                    const auto is_total = node.variant == 4;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        is_total ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_total ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        is_total
                            ? &m3e_nutrition_font_32
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_total
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        is_total ? px(6) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (nutrition_quick_add_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(20));
                    lv_obj_set_pos(object, px(4), px(4));
                    lv_obj_set_style_text_font(
                        object, &lv_font_montserrat_18, 0);
                    lv_obj_set_style_text_color(
                        object,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(object, px(2), 0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (voice_ready_document ||
                           live_action_detail_document) {
                    const auto is_value =
                        live_action_detail_document && node.variant == 4;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        px(184),
                        is_value ? px(48) : px(20));
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_value ? px(24) : px(4));
                    lv_obj_set_style_text_font(
                        object,
                        is_value
                            ? &m3e_live_action_font_32
                            : &lv_font_montserrat_18,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_value
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_pad_top(
                        object,
                        is_value ? px(6) : px(2),
                        0);
                    lv_label_set_long_mode(
                        object, LV_LABEL_LONG_DOT);
                } else if (media_player_document) {
                    const auto is_title = node.variant == 1;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object, px(104), is_title ? px(44) : px(32));
                    lv_obj_set_pos(
                        object, px(84), is_title ? px(4) : px(48));
                    lv_obj_set_style_text_font(
                        object,
                        is_title
                            ? &lv_font_montserrat_18
                            : &lv_font_montserrat_14,
                        0);
                    lv_obj_set_style_text_color(
                        object,
                        is_title
                            ? lv_color_make(0xF6, 0xED, 0xFF)
                            : lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        object, LV_TEXT_ALIGN_LEFT, 0);
                    lv_obj_set_style_pad_top(
                        object, is_title ? px(4) : px(2), 0);
                    lv_label_set_long_mode(
                        object,
                        is_title ? LV_LABEL_LONG_WRAP : LV_LABEL_LONG_DOT);
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
                if (countdown_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(120), px(40));
                    lv_obj_set_pos(
                        object,
                        (240 - px(120)) / 2,
                        px(140));
                    lv_obj_set_height(object, px(48));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                    }
                } else if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(48), px(48));
                    lv_obj_set_pos(object, px(136), px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    lv_obj_set_style_opa(
                        object,
                        LV_OPA_40,
                        static_cast<lv_style_selector_t>(
                            LV_STATE_DISABLED));
                    if (lv_obj_get_child_count(object) == 1) {
                        lv_obj_set_style_text_opa(
                            lv_obj_get_child(object, 0),
                            LV_OPA_TRANSP,
                            0);
                    }

                    auto* refresh = lv_arc_create(object);
                    lv_obj_remove_flag(
                        refresh, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_size(refresh, px(24), px(24));
                    lv_obj_center(refresh);
                    lv_arc_set_bg_angles(refresh, 35, 310);
                    lv_obj_set_style_arc_width(
                        refresh, px(3), LV_PART_MAIN);
                    lv_obj_set_style_arc_color(
                        refresh,
                        lv_color_make(0x35, 0x11, 0x51),
                        LV_PART_MAIN);
                    lv_obj_set_style_arc_opa(
                        refresh,
                        LV_OPA_TRANSP,
                        LV_PART_INDICATOR);
                    lv_obj_set_style_bg_opa(
                        refresh, LV_OPA_TRANSP, LV_PART_KNOB);
                    lv_obj_set_style_border_opa(
                        refresh, LV_OPA_TRANSP, LV_PART_KNOB);

                    auto* arrow = lv_obj_create(object);
                    ComponentFactory::reset(arrow);
                    lv_obj_set_size(arrow, px(4), px(4));
                    lv_obj_set_pos(arrow, px(31), px(14));
                    lv_obj_set_style_radius(
                        arrow, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        arrow,
                        lv_color_make(0x35, 0x11, 0x51),
                        0);
                    lv_obj_set_style_bg_opa(
                        arrow, LV_OPA_COVER, 0);
                } else if (notification_stack_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired =
                        notification_button_count == 3 &&
                        ordinal < 2;
                    const auto x =
                        paired
                            ? ordinal == 0 ? 0 : px(94)
                            : px(32);
                    const auto y = paired ? px(88) : px(136);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, y);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                } else if (calendar_agenda_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired = calendar_button_count == 2;
                    const auto x =
                        paired
                            ? px(static_cast<std::int32_t>(
                                ordinal) * 94)
                            : px(32);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                } else if (task_list_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(48));
                    lv_obj_set_pos(object, 0, px(136));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (workout_set_document ||
                           workout_rest_document ||
                           workout_summary_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto paired = workout_rest_document;
                    const auto x =
                        paired
                            ? px(4 + static_cast<std::int32_t>(
                                ordinal) * 94)
                            : px(36);
                    const auto width =
                        paired ? px(90) : px(120);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, width, px(48));
                    lv_obj_set_pos(object, x, px(140));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (nutrition_dashboard_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(120), px(48));
                    lv_obj_set_pos(object, px(36), px(140));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (
                    nutrition_quick_add_document ||
                    nutrition_review_document ||
                    live_action_detail_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        ordinal == 0 ? 112 : 113,
                        px(48));
                    lv_obj_set_pos(
                        object,
                        ordinal == 0 ? 0 : 117,
                        0);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_DOT);
                    }
                } else if (media_player_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::button);
                    const auto filled =
                        node.variant ==
                        static_cast<std::uint8_t>(
                            ButtonVariant::filled);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object,
                        ordinal == 0 ? 113 : 112,
                        px(80));
                    lv_obj_set_pos(
                        object,
                        ordinal == 0 ? 0 : 118,
                        0);
                    lv_obj_set_style_radius(
                        object, px(28), 0);
                    lv_obj_set_style_pad_hor(
                        object, px(8), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        filled
                            ? lv_color_make(0xD8, 0xB9, 0xFF)
                            : lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 1) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            label,
                            filled
                                ? lv_color_make(0x35, 0x11, 0x51)
                                : lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            label, LV_TEXT_ALIGN_CENTER, 0);
                        lv_label_set_long_mode(
                            label, LV_LABEL_LONG_WRAP);
                    }
                }
                break;
            case ComponentKind::card:
                object = factory.card(
                    parent,
                    {
                        primary,
                        secondary,
                        tone(node.tone),
                        node.event_count > 0,
                    });
                if (weather_hero_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(156));
                    lv_obj_set_pos(object, px(4), px(32));
                    lv_obj_set_style_radius(object, px(32), 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x4E, 0x28, 0x6E),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(152), px(24));
                        lv_obj_set_pos(title, px(16), px(78));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(108), px(38));
                        lv_obj_set_pos(body, px(16), px(102));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xF0, 0xDB, 0xFF),
                            0);
                        lv_obj_set_style_text_opa(body, 200, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (notification_stack_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::card);
                    const auto y =
                        notification_card_count == 2
                            ? px(24 + static_cast<std::int32_t>(
                                    ordinal) * 58)
                            : px(24);
                    const auto height =
                        notification_card_count == 2
                            ? px(54)
                            : notification_button_count == 3
                                ? px(60)
                                : px(108);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, 0, y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            title, px(160), px(18));
                        lv_obj_set_pos(
                            title, px(12), px(6));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            title, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            body,
                            px(160),
                            std::max(px(20), height - px(24)));
                        lv_obj_set_pos(
                            body, px(12), px(24));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_text_opa(
                            body, LV_OPA_COVER, 0);
                        lv_obj_set_style_text_align(
                            body, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (calendar_agenda_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::card);
                    const auto y =
                        calendar_card_count == 2
                            ? px(24 + static_cast<std::int32_t>(
                                    ordinal) * 54)
                            : px(24);
                    const auto height =
                        calendar_card_count == 2
                            ? px(50)
                            : px(104);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, 0, y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            title, px(160), px(20));
                        lv_obj_set_pos(
                            title, px(12), px(6));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_align(
                            title, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);

                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            body,
                            px(160),
                            std::max(px(18), height - px(26)));
                        lv_obj_set_pos(
                            body, px(12), px(26));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_text_align(
                            body, LV_TEXT_ALIGN_LEFT, 0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_WRAP);
                    }
                } else if (workout_summary_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(60));
                    lv_obj_set_pos(object, px(4), px(76));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(20));
                        lv_obj_set_pos(title, px(12), px(6));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(24));
                        lv_obj_set_pos(body, px(12), px(28));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                } else if (
                    nutrition_dashboard_document ||
                    nutrition_quick_add_document ||
                    nutrition_review_document) {
                    const auto y =
                        nutrition_dashboard_document
                            ? px(92)
                            : nutrition_quick_add_document
                                ? px(80)
                                : px(76);
                    const auto height =
                        nutrition_dashboard_document
                            ? px(44)
                            : nutrition_quick_add_document
                                ? px(52)
                                : px(56);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(object, px(4), y);
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            body,
                            px(160),
                            std::max(px(16), height - px(23)));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                } else if (voice_ready_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(56));
                    lv_obj_set_pos(object, px(4), px(132));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* title = lv_obj_get_child(object, 0);
                        auto* body = lv_obj_get_child(object, 1);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(28));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                    }
                }
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
                        (node.property_mask & (1UL << 2U)) != 0
                            ? node.maximum
                            : 0,
                        tone(node.tone),
                    });
                if (workout_set_document ||
                    workout_rest_document ||
                    live_action_detail_document) {
                    const auto is_rest = workout_rest_document;
                    const auto height =
                        is_rest ? px(60) : px(56);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_scrollbar_mode(
                        object, LV_SCROLLBAR_MODE_OFF);
                    lv_obj_set_size(object, px(184), height);
                    lv_obj_set_pos(
                        object,
                        px(4),
                        is_rest || live_action_detail_document
                            ? px(76)
                            : px(80));
                    lv_obj_set_style_min_height(object, 0, 0);
                    lv_obj_set_style_pad_all(object, 0, 0);
                    lv_obj_set_style_pad_gap(object, 0, 0);
                    lv_obj_set_style_radius(object, px(24), 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    if (lv_obj_get_child_count(object) >= 3) {
                        auto* app = lv_obj_get_child(object, 0);
                        lv_obj_add_flag(app, LV_OBJ_FLAG_HIDDEN);
                        auto* title = lv_obj_get_child(object, 1);
                        auto* body = lv_obj_get_child(object, 2);
                        lv_obj_add_flag(title, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(title, px(160), px(18));
                        lv_obj_set_pos(title, px(12), px(5));
                        lv_obj_set_style_text_font(
                            title, &lv_font_montserrat_16, 0);
                        lv_obj_set_style_text_color(
                            title,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_label_set_long_mode(
                            title, LV_LABEL_LONG_DOT);
                        lv_obj_add_flag(body, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(body, px(160), px(16));
                        lv_obj_set_pos(body, px(12), px(23));
                        lv_obj_set_style_text_font(
                            body, &lv_font_montserrat_14, 0);
                        lv_obj_set_style_text_color(
                            body,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_label_set_long_mode(
                            body, LV_LABEL_LONG_DOT);
                        if ((node.property_mask & (1UL << 2U)) != 0 &&
                            node.maximum > 0 &&
                            lv_obj_get_child_count(object) >= 4) {
                            auto* progress =
                                lv_obj_get_child(object, 3);
                            lv_obj_add_flag(
                                progress, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                progress, px(160), px(12));
                            lv_obj_set_pos(
                                progress, px(12), px(34));
                            lv_obj_set_style_bg_color(
                                progress,
                                lv_color_make(0x33, 0x2E, 0x3C),
                                LV_PART_MAIN);
                            lv_obj_set_style_bg_color(
                                progress,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                LV_PART_INDICATOR);

                            auto* stop = lv_obj_create(object);
                            ComponentFactory::reset(stop);
                            lv_obj_add_flag(
                                stop, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(stop, px(4), px(4));
                            lv_obj_set_pos(
                                stop, px(162), px(37));
                            lv_obj_set_style_radius(
                                stop, LV_RADIUS_CIRCLE, 0);
                            lv_obj_set_style_bg_color(
                                stop,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                0);
                            lv_obj_set_style_bg_opa(
                                stop, LV_OPA_COVER, 0);
                        }
                    }
                }
                break;
            case ComponentKind::progress:
                object = node.variant == 1
                    ? factory.circular_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)})
                    : factory.linear_progress(
                          parent,
                          {primary, node.value, node.maximum, tone(node.tone)});
                if (countdown_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(132), px(132));
                    lv_obj_set_pos(
                        object,
                        (240 - px(132) + 1) / 2,
                        px(4));
                    lv_obj_set_style_arc_width(
                        object, px(6), LV_PART_MAIN);
                    lv_obj_set_style_arc_width(
                        object, px(6), LV_PART_INDICATOR);
                    lv_obj_set_style_arc_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_arc_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                    lv_arc_set_bg_angles(object, 0, 360);
                    lv_arc_set_rotation(object, 270);
                } else if (nutrition_dashboard_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(12));
                    lv_obj_set_pos(object, px(4), px(76));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                } else if (media_player_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(184), px(16));
                    lv_obj_set_pos(object, px(4), px(84));
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x49, 0x44, 0x53),
                        LV_PART_MAIN);
                    lv_obj_set_style_bg_color(
                        object,
                        node.tone == static_cast<std::uint8_t>(Tone::error)
                            ? lv_color_make(0xFF, 0xB4, 0xAB)
                            : lv_color_make(0xD8, 0xB9, 0xFF),
                        LV_PART_INDICATOR);
                }
                break;
            case ComponentKind::stepper:
                if (!countdown_document) {
                    object = factory.stepper(
                        parent,
                        {
                            primary,
                            node.value,
                            secondary,
                            node.value <= node.minimum,
                            node.value >= node.maximum,
                        });
                    if (workout_set_document ||
                        nutrition_quick_add_document) {
                        lv_obj_add_flag(
                            object, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(
                            object, px(184), px(48));
                        lv_obj_set_pos(
                            object, px(4), px(28));
                        lv_obj_set_style_pad_all(
                            object, 0, 0);
                        lv_obj_set_style_pad_column(
                            object, px(4), 0);
                        lv_obj_set_style_bg_opa(
                            object, LV_OPA_TRANSP, 0);
                        if (lv_obj_get_child_count(object) == 3) {
                            auto* decrement =
                                lv_obj_get_child(object, 0);
                            auto* value =
                                lv_obj_get_child(object, 1);
                            auto* increment =
                                lv_obj_get_child(object, 2);
                            lv_obj_set_size(
                                decrement, px(48), px(48));
                            lv_obj_set_size(
                                increment, px(48), px(48));
                            lv_obj_set_style_bg_color(
                                decrement,
                                lv_color_make(0x33, 0x2E, 0x3C),
                                0);
                            lv_obj_set_style_bg_color(
                                increment,
                                lv_color_make(0xD8, 0xB9, 0xFF),
                                0);
                            auto* value_box = value;
                            lv_obj_set_height(value_box, px(48));
                            lv_obj_set_flex_grow(value_box, 1);
                            lv_obj_set_style_radius(
                                value_box, px(20), 0);
                            lv_obj_set_style_bg_color(
                                value_box,
                                lv_color_make(0x49, 0x44, 0x53),
                                0);
                            lv_obj_set_style_bg_opa(
                                value_box, LV_OPA_COVER, 0);
                            lv_obj_set_style_text_color(
                                value_box,
                                lv_color_make(0x49, 0x44, 0x53),
                                0);

                            char weight[16];
                            std::snprintf(
                                weight,
                                sizeof(weight),
                                "%ld",
                                static_cast<long>(node.value));
                            auto* weight_label = factory.text(
                                value_box,
                                weight,
                                generated::TypographyRole::numeral_small);
                            lv_obj_add_flag(
                                weight_label, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                weight_label, px(80), px(30));
                            lv_obj_set_pos(weight_label, 0, px(1));
                            lv_obj_set_style_text_font(
                                weight_label,
                                &m3e_timer_value_font_28,
                                0);
                            lv_obj_set_style_text_align(
                                weight_label, LV_TEXT_ALIGN_CENTER, 0);

                            auto* unit_label = factory.text(
                                value_box,
                                secondary,
                                generated::TypographyRole::body_extra_small);
                            lv_obj_add_flag(
                                unit_label, LV_OBJ_FLAG_FLOATING);
                            lv_obj_set_size(
                                unit_label, px(80), px(14));
                            lv_obj_set_pos(unit_label, 0, px(29));
                            lv_obj_set_style_text_color(
                                unit_label,
                                lv_color_make(0xCA, 0xC4, 0xD0),
                                0);
                            lv_obj_set_style_text_align(
                                unit_label, LV_TEXT_ALIGN_CENTER, 0);
                        }
                    }
                } else {
                    object = lv_obj_create(parent);
                    ComponentFactory::reset(object);
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(object, px(140), px(48));
                    lv_obj_set_pos(
                        object,
                        (240 - px(140) + 1) / 2,
                        px(88));
                    lv_obj_set_style_pad_all(object, px(4), 0);
                    lv_obj_set_style_pad_column(object, px(4), 0);
                    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
                    lv_obj_set_flex_align(
                        object,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

                    auto* decrement = factory.button(
                        object,
                        {
                            "decrement",
                            "-",
                            Tone::neutral,
                            ButtonVariant::tonal,
                            ComponentSize::compact,
                            node.value > node.minimum && node.enabled,
                            false,
                        });
                    lv_obj_set_size(decrement, px(40), px(40));
                    if (lv_obj_get_child_count(decrement) == 1) {
                        lv_obj_set_style_text_font(
                            lv_obj_get_child(decrement, 0),
                            &lv_font_montserrat_18,
                            0);
                    }

                    auto* value_box = lv_obj_create(object);
                    ComponentFactory::reset(value_box);
                    lv_obj_set_size(
                        value_box, px(64), px(40));
                    lv_obj_set_style_radius(
                        value_box, px(14), 0);
                    lv_obj_set_style_bg_color(
                        value_box,
                        lv_color_make(0x49, 0x44, 0x53),
                        0);
                    lv_obj_set_style_bg_opa(
                        value_box, LV_OPA_COVER, 0);
                    lv_obj_set_flex_flow(
                        value_box, LV_FLEX_FLOW_COLUMN);
                    lv_obj_set_flex_align(
                        value_box,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
                    char value_text[16]{};
                    std::snprintf(
                        value_text,
                        sizeof(value_text),
                        "%ld",
                        static_cast<long>(node.value));
                    auto* value_label = factory.text(
                        value_box,
                        value_text,
                        generated::TypographyRole::title_large);
                    lv_obj_set_style_text_font(
                        value_label, &m3e_timer_value_font_28, 0);
                    lv_obj_set_style_text_color(
                        value_label,
                        lv_color_make(0xF6, 0xED, 0xFF),
                        0);
                    auto* unit_label = factory.text(
                        value_box,
                        secondary,
                        generated::TypographyRole::body_extra_small);
                    lv_obj_set_style_text_color(
                        unit_label,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);

                    auto* increment = factory.button(
                        object,
                        {
                            "increment",
                            "+",
                            Tone::primary,
                            ButtonVariant::filled,
                            ComponentSize::compact,
                            node.value < node.maximum && node.enabled,
                            false,
                        });
                    lv_obj_set_size(increment, px(40), px(40));
                    lv_obj_set_style_bg_color(
                        increment,
                        lv_color_make(0xD8, 0xB9, 0xFF),
                        0);
                    if (lv_obj_get_child_count(increment) == 1) {
                        auto* label = lv_obj_get_child(increment, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                    }
                }
                break;
            case ComponentKind::toggle:
                object = factory.toggle_row(
                    parent, primary, node.value != 0, node.enabled);
                lv_obj_add_flag(object, LV_OBJ_FLAG_CHECKABLE);
                if (node.value != 0) {
                    lv_obj_add_state(object, LV_STATE_CHECKED);
                }
                if (task_list_document) {
                    const auto ordinal =
                        kind_ordinal(
                            document, index, ComponentKind::toggle);
                    const auto height =
                        task_toggle_count == 3 ? 48 : 52;
                    const auto step =
                        task_toggle_count == 3 ? 52 : 56;
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        object, px(184), px(height));
                    lv_obj_set_pos(
                        object,
                        0,
                        px(24 + static_cast<std::int32_t>(
                                ordinal) * step));
                    lv_obj_set_style_pad_hor(object, px(12), 0);
                    lv_obj_set_style_radius(
                        object, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x33, 0x2E, 0x3C),
                        0);
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_COVER, 0);
                    lv_obj_set_style_bg_color(
                        object,
                        lv_color_make(0x4E, 0x28, 0x6E),
                        static_cast<lv_style_selector_t>(
                            LV_PART_MAIN) |
                            static_cast<lv_style_selector_t>(
                                LV_STATE_CHECKED));
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* label = lv_obj_get_child(object, 0);
                        lv_obj_set_style_text_font(
                            label, &lv_font_montserrat_18, 0);
                        lv_obj_set_style_text_color(
                            label,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);

                        auto* indicator =
                            lv_obj_get_child(object, 1);
                        lv_obj_clean(indicator);
                        ComponentFactory::reset(indicator);
                        lv_obj_set_size(
                            indicator, px(24), px(24));
                        lv_obj_set_style_radius(
                            indicator, px(6), 0);
                        lv_obj_set_style_border_width(
                            indicator, px(2), 0);
                        lv_obj_set_style_border_color(
                            indicator,
                            lv_color_make(0xCA, 0xC4, 0xD0),
                            0);
                        lv_obj_set_style_bg_opa(
                            indicator, LV_OPA_TRANSP, 0);
                        lv_obj_set_style_bg_color(
                            indicator,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        lv_obj_set_style_bg_opa(
                            indicator,
                            LV_OPA_COVER,
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        lv_obj_set_style_border_color(
                            indicator,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            static_cast<lv_style_selector_t>(
                                LV_PART_MAIN) |
                                static_cast<lv_style_selector_t>(
                                    LV_STATE_CHECKED));
                        if (node.value != 0) {
                            lv_obj_add_state(
                                indicator, LV_STATE_CHECKED);
                        }
                        auto* mark = factory.text(
                            indicator,
                            node.value != 0 ? LV_SYMBOL_OK : "",
                            generated::TypographyRole::label_large);
                        lv_obj_set_style_text_color(
                            mark,
                            lv_color_make(0x35, 0x11, 0x51),
                            0);
                        lv_obj_center(mark);
                    }
                }
                break;
            case ComponentKind::voice_orb:
                object = factory.voice_orb(
                    parent, primary, tone(node.tone));
                if (voice_ready_document) {
                    lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                    lv_obj_add_flag(
                        object, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                    lv_obj_set_size(object, px(96), px(97));
                    lv_obj_set_pos(object, px(48), px(28));
                    lv_obj_set_style_bg_opa(
                        object, LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(object, 0, 0);
                    if (lv_obj_get_child_count(object) == 2) {
                        auto* symbol = lv_obj_get_child(object, 0);
                        auto* state = lv_obj_get_child(object, 1);
                        auto* circle = lv_obj_create(object);
                        ComponentFactory::reset(circle);
                        lv_obj_add_flag(
                            circle, LV_OBJ_FLAG_FLOATING);
                        lv_obj_set_size(circle, px(76), px(76));
                        lv_obj_set_pos(circle, px(10), 0);
                        lv_obj_set_style_radius(
                            circle, LV_RADIUS_CIRCLE, 0);
                        lv_obj_set_style_bg_color(
                            circle,
                            lv_color_make(0x33, 0x2E, 0x3C),
                            0);
                        lv_obj_set_style_bg_opa(
                            circle, LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(
                            circle, px(3), 0);
                        lv_obj_set_style_border_color(
                            circle,
                            lv_color_make(0xD8, 0xB9, 0xFF),
                            0);
                        lv_obj_set_parent(symbol, circle);
                        lv_obj_set_parent(state, circle);
                        lv_obj_set_style_text_color(
                            symbol,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_set_style_text_color(
                            state,
                            lv_color_make(0xF6, 0xED, 0xFF),
                            0);
                        lv_obj_align(
                            symbol, LV_ALIGN_CENTER, 0, -px(8));
                        lv_obj_align(
                            state, LV_ALIGN_CENTER, 0, px(16));
                    }
                    auto* transcript = factory.text(
                        object,
                        secondary,
                        generated::TypographyRole::body_extra_small,
                        true);
                    lv_obj_add_flag(
                        transcript, LV_OBJ_FLAG_FLOATING);
                    lv_obj_set_size(
                        transcript, px(96), px(20));
                    lv_obj_set_pos(
                        transcript, 0, px(78));
                    lv_obj_set_style_text_font(
                        transcript, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(
                        transcript,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_text_align(
                        transcript, LV_TEXT_ALIGN_CENTER, 0);
                    lv_label_set_long_mode(
                        transcript, LV_LABEL_LONG_DOT);
                }
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
            case ComponentKind::image: {
                object = lv_obj_create(parent);
                ComponentFactory::reset(object);
                lv_obj_add_flag(object, LV_OBJ_FLAG_FLOATING);
                lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_size(object, px(76), px(76));
                lv_obj_set_pos(object, px(4), px(4));
                lv_obj_set_style_radius(object, px(24), 0);
                lv_obj_set_style_clip_corner(object, true, 0);
                lv_obj_set_style_bg_color(
                    object,
                    lv_color_make(0x33, 0x2E, 0x3C),
                    0);
                lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);

                ImageAssetView asset{};
                if (resolve_image_asset(primary, asset)) {
                    auto* canvas = lv_canvas_create(object);
                    lv_canvas_set_buffer(
                        canvas,
                        const_cast<std::uint8_t*>(asset.pixels),
                        asset.width,
                        asset.height,
                        LV_COLOR_FORMAT_RGB565);
                    const auto width_scale =
                        (px(76) * 256 + asset.width - 1) /
                        asset.width;
                    const auto height_scale =
                        (px(76) * 256 + asset.height - 1) /
                        asset.height;
                    lv_image_set_scale(
                        canvas,
                        node.variant == 1
                            ? std::min(width_scale, height_scale)
                            : std::max(width_scale, height_scale));
                    lv_obj_center(canvas);
                } else {
                    auto* circle = lv_obj_create(object);
                    ComponentFactory::reset(circle);
                    lv_obj_set_size(circle, px(36), px(36));
                    lv_obj_center(circle);
                    lv_obj_set_style_radius(
                        circle, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(
                        circle,
                        lv_color_make(0x7D, 0x52, 0x9C),
                        0);
                    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
                    auto* horizontal = lv_obj_create(object);
                    ComponentFactory::reset(horizontal);
                    lv_obj_set_size(
                        horizontal, px(22), px(3));
                    lv_obj_center(horizontal);
                    lv_obj_set_style_bg_color(
                        horizontal,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_bg_opa(
                        horizontal, LV_OPA_COVER, 0);
                    auto* vertical = lv_obj_create(object);
                    ComponentFactory::reset(vertical);
                    lv_obj_set_size(
                        vertical, px(3), px(22));
                    lv_obj_center(vertical);
                    lv_obj_set_style_bg_color(
                        vertical,
                        lv_color_make(0xCA, 0xC4, 0xD0),
                        0);
                    lv_obj_set_style_bg_opa(
                        vertical, LV_OPA_COVER, 0);
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
