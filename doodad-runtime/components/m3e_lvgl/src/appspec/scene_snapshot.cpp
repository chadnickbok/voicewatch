#include "m3e/appspec/scene_snapshot.hpp"

#include <cstdio>
#include <cstdint>

#include "lvgl.h"
#include "m3e/foundation/display_profile.hpp"

namespace m3e::appspec {
namespace {

class JsonWriter {
 public:
    JsonWriter(char* output, std::size_t size)
        : output_(output), size_(size) {
        if (output_ != nullptr && size_ != 0) output_[0] = '\0';
    }

    void text(const char* value) {
        if (value == nullptr) value = "";
        while (*value != '\0') byte(*value++);
    }

    void string(const char* value) {
        byte('"');
        if (value == nullptr) value = "";
        for (const auto* cursor = value; *cursor != '\0'; ++cursor) {
            const auto character =
                static_cast<unsigned char>(*cursor);
            switch (character) {
                case '"': text("\\\""); break;
                case '\\': text("\\\\"); break;
                case '\b': text("\\b"); break;
                case '\f': text("\\f"); break;
                case '\n': text("\\n"); break;
                case '\r': text("\\r"); break;
                case '\t': text("\\t"); break;
                default:
                    if (character < 0x20U) {
                        char escaped[7]{};
                        std::snprintf(
                            escaped,
                            sizeof(escaped),
                            "\\u%04x",
                            static_cast<unsigned>(character));
                        text(escaped);
                    } else {
                        byte(static_cast<char>(character));
                    }
                    break;
            }
        }
        byte('"');
    }

    void integer(std::int64_t value) {
        char encoded[32]{};
        std::snprintf(
            encoded,
            sizeof(encoded),
            "%lld",
            static_cast<long long>(value));
        text(encoded);
    }

    void boolean(bool value) {
        text(value ? "true" : "false");
    }

    std::size_t finish() {
        if (output_ != nullptr && size_ != 0) {
            output_[written_ < size_ ? written_ : size_ - 1] = '\0';
        }
        return written_;
    }

 private:
    void byte(char value) {
        if (output_ != nullptr && written_ + 1 < size_) {
            output_[written_] = value;
        }
        ++written_;
    }

    char* output_;
    std::size_t size_;
    std::size_t written_ = 0;
};

const char* component_name(ComponentKind kind) {
    switch (kind) {
        case ComponentKind::screen: return "screen";
        case ComponentKind::column: return "column";
        case ComponentKind::row: return "row";
        case ComponentKind::scroll: return "scroll";
        case ComponentKind::text: return "text";
        case ComponentKind::button: return "button";
        case ComponentKind::card: return "card";
        case ComponentKind::progress: return "progress";
        case ComponentKind::stepper: return "stepper";
        case ComponentKind::toggle: return "toggle";
        case ComponentKind::keypad: return "keypad";
        case ComponentKind::voice_orb: return "voice_orb";
        case ComponentKind::live_card: return "live_card";
    }
    return "text";
}

const char* semantic_role_name(const WireNode& node) {
    switch (node.kind) {
        case ComponentKind::screen: return "screen";
        case ComponentKind::column:
        case ComponentKind::row:
        case ComponentKind::scroll: return "list";
        case ComponentKind::text:
            return node.variant <= 1 ? "heading" : "text";
        case ComponentKind::button:
        case ComponentKind::keypad:
        case ComponentKind::voice_orb: return "button";
        case ComponentKind::card:
        case ComponentKind::live_card: return "list_item";
        case ComponentKind::progress: return "progress";
        case ComponentKind::stepper: return "slider";
        case ComponentKind::toggle: return "toggle";
    }
    return "text";
}

const char* event_name(EventKind kind) {
    switch (kind) {
        case EventKind::tap: return "tap";
        case EventKind::long_press: return "long_press";
        case EventKind::repeat: return "repeat";
        case EventKind::value_changing: return "value_changing";
        case EventKind::value_committed: return "value_committed";
        case EventKind::checked_changed: return "checked_changed";
        case EventKind::page_changed: return "page_changed";
        case EventKind::dismissed: return "dismissed";
        case EventKind::submit: return "submit";
        case EventKind::retry: return "retry";
        case EventKind::cancel: return "cancel";
    }
    return "tap";
}

const char* tone_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "primary", "secondary", "tertiary", "neutral", "error"};
    return value < 5 ? kNames[value] : "primary";
}

const char* size_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "compact", "default", "large"};
    return value < 3 ? kNames[value] : "default";
}

const char* gap_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "none", "xs", "sm", "md", "lg"};
    return value < 5 ? kNames[value] : "md";
}

const char* alignment_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "start", "center", "end", "stretch"};
    return value < 4 ? kNames[value] : "center";
}

const char* text_style_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "display", "title", "label", "body", "numeral", "caption"};
    return value < 6 ? kNames[value] : "body";
}

const char* button_variant_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "filled", "tonal", "outlined", "text"};
    return value < 4 ? kNames[value] : "filled";
}

const char* progress_style_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "linear", "circular", "segmented"};
    return value < 3 ? kNames[value] : "linear";
}

const char* voice_state_name(std::uint8_t value) {
    static constexpr const char* kNames[] = {
        "idle", "listening", "thinking", "speaking", "error"};
    return value < 5 ? kNames[value] : "idle";
}

void property_string(
    JsonWriter& writer,
    bool& first,
    const char* key,
    const char* value) {
    if (!first) writer.text(",");
    first = false;
    writer.string(key);
    writer.text(":");
    writer.string(value);
}

void property_integer(
    JsonWriter& writer,
    bool& first,
    const char* key,
    std::int64_t value) {
    if (!first) writer.text(",");
    first = false;
    writer.string(key);
    writer.text(":");
    writer.integer(value);
}

void write_props(
    JsonWriter& writer,
    const WireDocument& document,
    const WireNode& node) {
    writer.text("{");
    bool first = true;
    const auto* primary = document.string_at(node.primary_text_offset);
    const auto* secondary =
        document.string_at(node.secondary_text_offset);
    switch (node.kind) {
        case ComponentKind::screen:
        case ComponentKind::column:
        case ComponentKind::row:
        case ComponentKind::scroll:
            property_string(writer, first, "gap", gap_name(node.gap));
            property_string(
                writer,
                first,
                "alignment",
                alignment_name(node.alignment));
            break;
        case ComponentKind::text:
            property_string(writer, first, "primary_text", primary);
            property_string(
                writer,
                first,
                "variant",
                text_style_name(node.variant));
            property_string(
                writer,
                first,
                "alignment",
                alignment_name(node.alignment));
            property_integer(writer, first, "max_lines", node.max_lines);
            break;
        case ComponentKind::button:
            property_string(writer, first, "primary_text", primary);
            property_string(
                writer,
                first,
                "variant",
                button_variant_name(node.variant));
            property_string(writer, first, "tone", tone_name(node.tone));
            property_string(writer, first, "size", size_name(node.size));
            if (node.icon_offset != 0) {
                property_string(
                    writer,
                    first,
                    "icon",
                    document.string_at(node.icon_offset));
            }
            break;
        case ComponentKind::card:
            property_string(writer, first, "primary_text", primary);
            property_string(writer, first, "secondary_text", secondary);
            property_string(writer, first, "tone", tone_name(node.tone));
            break;
        case ComponentKind::progress:
            property_string(writer, first, "primary_text", primary);
            property_integer(writer, first, "value", node.value);
            property_integer(writer, first, "maximum", node.maximum);
            property_string(
                writer,
                first,
                "variant",
                progress_style_name(node.variant));
            property_string(writer, first, "tone", tone_name(node.tone));
            break;
        case ComponentKind::stepper:
            property_string(writer, first, "primary_text", primary);
            if (secondary[0] != '\0') {
                property_string(
                    writer, first, "secondary_text", secondary);
            }
            property_integer(writer, first, "value", node.value);
            property_integer(writer, first, "minimum", node.minimum);
            property_integer(writer, first, "maximum", node.maximum);
            property_integer(writer, first, "step", node.step);
            break;
        case ComponentKind::toggle:
            property_string(writer, first, "primary_text", primary);
            if (!first) writer.text(",");
            first = false;
            writer.string("checked");
            writer.text(":");
            writer.boolean(node.value != 0);
            property_string(writer, first, "tone", tone_name(node.tone));
            break;
        case ComponentKind::keypad:
            if (!first) writer.text(",");
            first = false;
            writer.string("keys");
            writer.text(":[");
            for (std::size_t index = 0; index < node.key_count; ++index) {
                if (index != 0) writer.text(",");
                writer.string(document.string_at(
                    document.key_offsets[node.key_start + index]));
            }
            writer.text("]");
            property_integer(
                writer, first, "key_columns", node.key_columns);
            break;
        case ComponentKind::voice_orb:
            property_string(writer, first, "primary_text", primary);
            if (secondary[0] != '\0') {
                property_string(
                    writer, first, "secondary_text", secondary);
            }
            property_string(
                writer,
                first,
                "state",
                voice_state_name(node.voice_state));
            property_string(writer, first, "tone", tone_name(node.tone));
            break;
        case ComponentKind::live_card:
            property_string(writer, first, "primary_text", primary);
            property_string(writer, first, "secondary_text", secondary);
            if ((node.property_mask & (1UL << 2U)) != 0) {
                property_integer(writer, first, "value", node.value);
                property_integer(writer, first, "maximum", node.maximum);
            }
            property_string(writer, first, "tone", tone_name(node.tone));
            break;
    }
    writer.text("}");
}

const char* semantic_label(
    const WireDocument& document,
    const WireNode& node) {
    const auto* label = document.string_at(node.semantic_label_offset);
    if (label[0] == '\0') {
        label = document.string_at(node.primary_text_offset);
    }
    if (label[0] == '\0' && node.kind == ComponentKind::screen) {
        label = document.string_at(document.app_id_offset);
    }
    return label;
}

void write_semantics(
    JsonWriter& writer,
    const WireDocument& document,
    const WireNode& node) {
    writer.text("{\"role\":");
    writer.string(semantic_role_name(node));
    writer.text(",\"label\":");
    writer.string(semantic_label(document, node));
    if (node.semantic_value_offset != 0) {
        writer.text(",\"value\":");
        writer.string(document.string_at(node.semantic_value_offset));
    }
    if (node.semantic_hint_offset != 0) {
        writer.text(",\"hint\":");
        writer.string(document.string_at(node.semantic_hint_offset));
    }
    writer.text("}");
}

void write_actions(
    JsonWriter& writer,
    const WireDocument& document,
    std::size_t node_index) {
    writer.text("[");
    bool first = true;
    for (std::size_t index = 0; index < document.event_count; ++index) {
        const auto& event = document.events[index];
        if (event.node_index != node_index) continue;
        if (!first) writer.text(",");
        first = false;
        writer.text("{\"kind\":");
        writer.string(event_name(event.kind));
        writer.text(",\"action_id\":");
        writer.string(document.string_at(event.action_id_offset));
        writer.text("}");
    }
    writer.text("]");
}

std::int64_t dp_q8_8(std::int32_t pixels) {
    const auto density =
        static_cast<std::int64_t>(watch_square_192.density_q8_8);
    auto numerator =
        static_cast<std::int64_t>(pixels) * 256LL * 256LL;
    numerator += numerator >= 0 ? density / 2 : -(density / 2);
    return numerator / density;
}

void write_bounds(
    JsonWriter& writer,
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height,
    bool logical) {
    writer.text("{\"x\":");
    writer.integer(logical ? dp_q8_8(x) : x);
    writer.text(",\"y\":");
    writer.integer(logical ? dp_q8_8(y) : y);
    writer.text(",\"width\":");
    writer.integer(logical ? dp_q8_8(width) : width);
    writer.text(",\"height\":");
    writer.integer(logical ? dp_q8_8(height) : height);
    writer.text("}");
}

void write_token_roles(JsonWriter& writer, const WireNode& node) {
    writer.text("{");
    switch (node.kind) {
        case ComponentKind::screen:
            writer.text("\"background\":\"background\"");
            break;
        case ComponentKind::column:
        case ComponentKind::row:
        case ComponentKind::scroll:
            writer.text("\"layout\":\"surface\"");
            break;
        case ComponentKind::text:
            writer.text("\"typography\":");
            writer.string(text_style_name(node.variant));
            break;
        case ComponentKind::button:
            writer.text("\"container\":");
            writer.string(
                node.variant >= 2 ? "transparent" : tone_name(node.tone));
            writer.text(",\"content\":\"on_surface\"");
            break;
        case ComponentKind::card:
        case ComponentKind::live_card:
            writer.text(
                "\"container\":\"surface_container\","
                "\"content\":\"on_surface\"");
            break;
        case ComponentKind::progress:
            writer.text("\"indicator\":");
            writer.string(tone_name(node.tone));
            break;
        case ComponentKind::stepper:
            writer.text(
                "\"control\":\"primary\","
                "\"content\":\"on_surface\"");
            break;
        case ComponentKind::toggle:
            writer.text("\"control\":");
            writer.string(tone_name(node.tone));
            break;
        case ComponentKind::keypad:
            writer.text(
                "\"container\":\"surface_container\","
                "\"content\":\"on_surface\"");
            break;
        case ComponentKind::voice_orb:
            writer.text("\"container\":");
            writer.string(tone_name(node.tone));
            break;
    }
    writer.text("}");
}

}  // namespace

std::size_t scene_snapshot_json(
    const WireDocument& document,
    char* output,
    std::size_t output_size,
    const char* origin) {
    JsonWriter writer(output, output_size);
    if (document.node_count == 0) return writer.finish();
    writer.text("{\"schema_version\":1,\"app_id\":");
    writer.string(document.string_at(document.app_id_offset));
    writer.text(",\"screen_id\":");
    writer.string(document.string_at(document.nodes[0].id_offset));
    writer.text(",\"origin\":");
    writer.string(origin);
    writer.text(",\"nodes\":[");
    for (std::size_t index = 0; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (index != 0) writer.text(",");
        writer.text("{\"id\":");
        writer.string(document.string_at(node.id_offset));
        writer.text(",\"parent_id\":");
        if (node.parent_index == kWireNoParent) {
            writer.text("null");
        } else {
            writer.string(document.string_at(
                document.nodes[node.parent_index].id_offset));
        }
        writer.text(",\"kind\":");
        writer.string(component_name(node.kind));
        writer.text(",\"depth\":");
        writer.integer(node.depth);
        writer.text(",\"child_count\":");
        writer.integer(node.child_count);
        writer.text(",\"visible\":");
        writer.boolean(node.visible);
        writer.text(",\"enabled\":");
        writer.boolean(node.enabled);
        writer.text(",\"props\":");
        write_props(writer, document, node);
        writer.text(",\"semantics\":");
        write_semantics(writer, document, node);
        writer.text(",\"actions\":");
        write_actions(writer, document, index);
        writer.text("}");
    }
    writer.text("]}");
    return writer.finish();
}

std::size_t node_layout_evidence_json(
    const WireDocument& document,
    char* output,
    std::size_t output_size) {
    JsonWriter writer(output, output_size);
    if (document.node_count == 0) return writer.finish();
    writer.text("{\"nodes\":[");
    for (std::size_t index = 0; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        if (index != 0) writer.text(",");
        writer.text("{\"id\":");
        writer.string(document.string_at(node.id_offset));
        writer.text(",\"parent_id\":");
        if (node.parent_index == kWireNoParent) {
            writer.text("null");
        } else {
            writer.string(document.string_at(
                document.nodes[node.parent_index].id_offset));
        }
        writer.text(",\"role\":");
        writer.string(semantic_role_name(node));
        writer.text(",\"label\":");
        writer.string(semantic_label(document, node));
        writer.text(",\"value\":");
        writer.string(document.string_at(node.semantic_value_offset));
        writer.text(",\"state_description\":");
        writer.string(document.string_at(node.semantic_hint_offset));
        writer.text(",\"visible\":");
        writer.boolean(node.visible);
        writer.text(",\"enabled\":");
        writer.boolean(node.enabled);
        if (node.kind == ComponentKind::toggle) {
            writer.text(",\"checked\":");
            writer.boolean(node.value != 0);
        }
        writer.text(",\"actions\":");
        write_actions(writer, document, index);

        lv_area_t bounds{};
        if (node.mounted_object != nullptr) {
            lv_obj_get_coords(
                static_cast<lv_obj_t*>(node.mounted_object),
                &bounds);
        }
        const auto width =
            node.mounted_object == nullptr
                ? 0
                : static_cast<std::int32_t>(
                      bounds.x2 - bounds.x1 + 1);
        const auto height =
            node.mounted_object == nullptr
                ? 0
                : static_cast<std::int32_t>(
                      bounds.y2 - bounds.y1 + 1);
        const auto x =
            node.mounted_object == nullptr ? 0 : bounds.x1;
        const auto y =
            node.mounted_object == nullptr ? 0 : bounds.y1;
        writer.text(",\"bounds_px\":");
        write_bounds(writer, x, y, width, height, false);
        writer.text(",\"bounds_dp_q8_8\":");
        write_bounds(writer, x, y, width, height, true);
        writer.text(",\"token_roles\":");
        write_token_roles(writer, node);
        writer.text("}");
    }
    writer.text("]}");
    return writer.finish();
}

}  // namespace m3e::appspec
