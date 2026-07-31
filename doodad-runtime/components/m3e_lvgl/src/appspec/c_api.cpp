#include "m3e/appspec/c_api.h"

#include <cstdio>
#include <cstring>
#include <new>

#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/renderer.hpp"
#include "m3e/appspec/scene_snapshot.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/semantics/semantic_tree.hpp"
#include "m3e/theme/resolved_theme.hpp"

namespace {

struct EventBridge {
    m3e_appspec_event_callback_t callback;
    void* context;
};

m3e::appspec::WireDocument* g_event_document = nullptr;
EventBridge g_event_bridge{};

class SnapshotWriter {
 public:
    SnapshotWriter(char* output, std::size_t size)
        : output_(output), size_(size) {
        if (output_ != nullptr && size_ != 0) output_[0] = '\0';
    }

    void text(const char* value) {
        if (value == nullptr) value = "";
        while (*value != '\0') byte(*value++);
    }

    void json_string(const char* value) {
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

    void unsigned_integer(std::uint64_t value) {
        char encoded[24]{};
        std::snprintf(
            encoded,
            sizeof(encoded),
            "%llu",
            static_cast<unsigned long long>(value));
        text(encoded);
    }

    std::size_t length() {
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

const char* semantic_role_name(m3e::SemanticRole role) {
    switch (role) {
        case m3e::SemanticRole::screen: return "screen";
        case m3e::SemanticRole::heading: return "heading";
        case m3e::SemanticRole::text: return "text";
        case m3e::SemanticRole::button: return "button";
        case m3e::SemanticRole::toggle: return "toggle";
        case m3e::SemanticRole::slider: return "slider";
        case m3e::SemanticRole::progress: return "progress";
        case m3e::SemanticRole::list: return "list";
        case m3e::SemanticRole::list_item: return "list_item";
        case m3e::SemanticRole::dialog: return "dialog";
        case m3e::SemanticRole::timer: return "timer";
        case m3e::SemanticRole::image: return "image";
    }
    return "unknown";
}

void forward_event(
    const m3e::appspec::UiEvent& event,
    void* context) {
    auto* bridge = static_cast<EventBridge*>(context);
    if (bridge == nullptr || bridge->callback == nullptr) return;
    std::uint8_t encoded[512]{};
    const auto size = m3e::appspec::encode_event_canonical_cbor(
        event, encoded, sizeof(encoded));
    if (size != 0) {
        bridge->callback(encoded, size, bridge->context);
    }
}

bool ensure_styles(
    m3e::StyleRegistry& styles,
    char* error,
    std::size_t error_size) {
    if (styles.initialized() ||
        styles.initialize(m3e::baseline_dark_theme())) {
        return true;
    }
    if (error != nullptr && error_size != 0) {
        std::snprintf(error, error_size, "theme initialization failed");
    }
    return false;
}

}  // namespace

extern "C" int m3e_appspec_render_canonical_cbor(
    lv_obj_t* root,
    const std::uint8_t* bytes,
    std::size_t size,
    char* error,
    std::size_t error_size) {
    m3e::appspec::WireDocument document;
    const auto result =
        m3e::appspec::decode_canonical_cbor(bytes, size, document);
    if (!result.ok()) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(
                error,
                error_size,
                "%s at byte %u node %u",
                m3e::appspec::wire_error_name(result.error),
                static_cast<unsigned>(result.byte_offset),
                static_cast<unsigned>(result.node_index));
        }
        return 0;
    }
    static m3e::StyleRegistry styles;
    if (!ensure_styles(styles, error, error_size)) return 0;
    m3e::appspec::Renderer renderer(styles);
    if (!renderer.mount(root, document)) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "renderer rejected document");
        }
        return 0;
    }
    if (error != nullptr && error_size != 0) error[0] = '\0';
    return 1;
}

extern "C" int m3e_appspec_render_canonical_cbor_with_events(
    lv_obj_t* root,
    const std::uint8_t* bytes,
    std::size_t size,
    m3e_appspec_event_callback_t callback,
    void* callback_context,
    char* error,
    std::size_t error_size) {
    auto* document =
        new (std::nothrow) m3e::appspec::WireDocument{};
    if (document == nullptr) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "document allocation failed");
        }
        return 0;
    }
    const auto result =
        m3e::appspec::decode_canonical_cbor(bytes, size, *document);
    if (!result.ok()) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(
                error,
                error_size,
                "%s at byte %u node %u",
                m3e::appspec::wire_error_name(result.error),
                static_cast<unsigned>(result.byte_offset),
                static_cast<unsigned>(result.node_index));
        }
        delete document;
        return 0;
    }
    static m3e::StyleRegistry styles;
    if (!ensure_styles(styles, error, error_size)) {
        delete document;
        return 0;
    }
    g_event_bridge = {callback, callback_context};
    m3e::appspec::Renderer renderer(styles);
    if (!renderer.mount(
            root,
            *document,
            callback == nullptr ? nullptr : forward_event,
            &g_event_bridge)) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "renderer rejected document");
        }
        delete document;
        return 0;
    }
    delete g_event_document;
    g_event_document = document;
    if (error != nullptr && error_size != 0) error[0] = '\0';
    return 1;
}

extern "C" int m3e_appspec_apply_command_batch(
    const std::uint8_t* bytes,
    std::size_t size,
    char* error,
    std::size_t error_size) {
    if (g_event_document == nullptr) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "no mounted event document");
        }
        return 0;
    }
    m3e::appspec::CommandBatch batch;
    const auto decoded =
        m3e::appspec::decode_command_batch_canonical_cbor(
            bytes, size, batch);
    if (!decoded.ok()) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(
                error,
                error_size,
                "%s at byte %u command %u",
                m3e::appspec::command_error_name(decoded.error),
                static_cast<unsigned>(decoded.byte_offset),
                static_cast<unsigned>(decoded.command_index));
        }
        return 0;
    }
    if (batch.domain != m3e::appspec::CommandDomain::ui) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "batch is not UI domain");
        }
        return 0;
    }
    const auto applied =
        m3e::appspec::apply_ui_command_batch(
            batch, *g_event_document);
    if (!applied.ok()) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(
                error,
                error_size,
                "%s at command %u",
                m3e::appspec::command_error_name(applied.error),
                static_cast<unsigned>(applied.command_index));
        }
        return 0;
    }
    if (error != nullptr && error_size != 0) error[0] = '\0';
    return 1;
}

extern "C" int m3e_appspec_emit_semantic_event(
    const char* node_id,
    const char* action_id,
    int event_kind,
    std::uint64_t timestamp_ms,
    int value_kind,
    std::int32_t integer_value,
    int boolean_value,
    const char* text_value,
    char* error,
    std::size_t error_size) {
    auto fail = [&](const char* message) {
        if (error != nullptr && error_size != 0) {
            std::snprintf(error, error_size, "%s", message);
        }
        return 0;
    };
    if (g_event_document == nullptr) {
        return fail("no mounted event document");
    }
    if (node_id == nullptr || action_id == nullptr ||
        event_kind < 0 ||
        event_kind >
            static_cast<int>(m3e::appspec::EventKind::cancel)) {
        return fail("invalid semantic action identity");
    }
    m3e::appspec::WireEvent* matched = nullptr;
    for (std::size_t index = 0;
         index < g_event_document->event_count;
         ++index) {
        auto& candidate = g_event_document->events[index];
        if (candidate.node_index >= g_event_document->node_count) {
            continue;
        }
        const auto& node =
            g_event_document->nodes[candidate.node_index];
        if (std::strcmp(
                g_event_document->string_at(node.id_offset),
                node_id) == 0 &&
            std::strcmp(
                g_event_document->string_at(
                    candidate.action_id_offset),
                action_id) == 0 &&
            static_cast<int>(candidate.kind) == event_kind) {
            if (matched != nullptr) {
                return fail("semantic action identity is ambiguous");
            }
            matched = &candidate;
        }
    }
    if (matched == nullptr) {
        return fail("semantic action identity is stale or unsupported");
    }
    const auto& node =
        g_event_document->nodes[matched->node_index];
    if (!node.visible || !node.enabled || matched->sink == nullptr) {
        return fail("semantic action target is unavailable");
    }
    m3e::appspec::EventValue value{};
    switch (value_kind) {
        case M3E_APPSPEC_EVENT_VALUE_NONE:
            break;
        case M3E_APPSPEC_EVENT_VALUE_INTEGER:
            value = m3e::appspec::EventValue::integer(integer_value);
            break;
        case M3E_APPSPEC_EVENT_VALUE_BOOLEAN:
            value = m3e::appspec::EventValue::boolean(boolean_value != 0);
            break;
        case M3E_APPSPEC_EVENT_VALUE_TEXT:
            if (text_value == nullptr || text_value[0] == '\0') {
                return fail("semantic text value is empty");
            }
            value = m3e::appspec::EventValue::text(text_value);
            break;
        default:
            return fail("semantic event value kind is unsupported");
    }
    const m3e::appspec::UiEvent event{
        1,
        g_event_document->string_at(
            g_event_document->app_id_offset),
        g_event_document->string_at(
            g_event_document->nodes[0].id_offset),
        g_event_document->string_at(node.id_offset),
        g_event_document->string_at(matched->action_id_offset),
        matched->kind,
        timestamp_ms,
        value,
    };
    if (!m3e::appspec::event_is_valid(event)) {
        return fail("semantic event envelope is invalid");
    }
    matched->sink(event, matched->sink_context);
    if (error != nullptr && error_size != 0) error[0] = '\0';
    return 1;
}

extern "C" const char* m3e_appspec_mounted_text(
    const char* node_id,
    int secondary) {
    if (g_event_document == nullptr || node_id == nullptr) return nullptr;
    for (std::size_t index = 0;
         index < g_event_document->node_count;
         ++index) {
        auto& node = g_event_document->nodes[index];
        if (std::strcmp(
                g_event_document->string_at(node.id_offset),
                node_id) != 0 ||
            node.mounted_object == nullptr) {
            continue;
        }
        auto* object = static_cast<lv_obj_t*>(node.mounted_object);
        lv_obj_t* label = nullptr;
        if (node.kind == m3e::appspec::ComponentKind::text) {
            label = secondary == 0 ? object : nullptr;
        } else if (
            node.kind == m3e::appspec::ComponentKind::button ||
            node.kind == m3e::appspec::ComponentKind::toggle ||
            node.kind == m3e::appspec::ComponentKind::voice_orb) {
            label = secondary == 0 &&
                            lv_obj_get_child_count(object) > 0
                        ? lv_obj_get_child(object, 0)
                        : nullptr;
        } else if (
            node.kind == m3e::appspec::ComponentKind::card ||
            node.kind == m3e::appspec::ComponentKind::live_card) {
            const auto child = secondary == 0 ? 0 : 1;
            label = lv_obj_get_child_count(object) >
                            static_cast<std::uint32_t>(child)
                        ? lv_obj_get_child(object, child)
                        : nullptr;
        } else if (
            node.kind == m3e::appspec::ComponentKind::stepper) {
            label = secondary == 0 &&
                            lv_obj_get_child_count(object) > 1
                        ? lv_obj_get_child(object, 1)
                        : nullptr;
        }
        return label != nullptr &&
                       lv_obj_check_type(label, &lv_label_class)
                   ? lv_label_get_text(label)
                   : nullptr;
    }
    return nullptr;
}

extern "C" std::size_t m3e_appspec_semantic_snapshot(
    char* output,
    std::size_t output_size) {
    if (g_event_document == nullptr) {
        if (output != nullptr && output_size != 0) output[0] = '\0';
        return 0;
    }
    m3e::SemanticTree tree;
    if (!m3e::appspec::build_semantic_tree(
            *g_event_document, tree)) {
        if (output != nullptr && output_size != 0) output[0] = '\0';
        return 0;
    }

    SnapshotWriter writer(output, output_size);
    writer.text("{\"app\":");
    writer.json_string(
        g_event_document->string_at(g_event_document->app_id_offset));
    writer.text(",\"nodes\":[");
    for (std::size_t index = 0; index < tree.size(); ++index) {
        const auto& node = tree.at(index);
        if (index != 0) writer.text(",");
        writer.text("{\"id\":");
        writer.json_string(node.id);
        writer.text(",\"role\":");
        writer.json_string(semantic_role_name(node.role));
        writer.text(",\"label\":");
        writer.json_string(node.label);
        writer.text(",\"value\":");
        writer.json_string(node.value);
        const auto& wire_node = g_event_document->nodes[index];
        writer.text(",\"text\":");
        writer.json_string(
            g_event_document->string_at(
                wire_node.primary_text_offset));
        writer.text(",\"secondary\":");
        writer.json_string(
            g_event_document->string_at(
                wire_node.secondary_text_offset));
        writer.text(",\"state\":");
        writer.unsigned_integer(node.state);
        writer.text(",\"visible\":");
        writer.text(wire_node.visible ? "true" : "false");
        writer.text(",\"enabled\":");
        writer.text(wire_node.enabled ? "true" : "false");
        writer.text(",\"parent\":");
        if (node.parent_index == m3e::SemanticTree::kNoIndex) {
            writer.text("null");
        } else {
            writer.unsigned_integer(node.parent_index);
        }
        writer.text(",\"children\":");
        writer.unsigned_integer(node.child_count);
        writer.text("}");
    }
    writer.text("]}");
    return writer.length();
}

extern "C" std::size_t m3e_appspec_scene_snapshot_json(
    char* output,
    std::size_t output_size) {
    if (g_event_document == nullptr) {
        if (output != nullptr && output_size != 0) output[0] = '\0';
        return 0;
    }
    return m3e::appspec::scene_snapshot_json(
        *g_event_document,
        output,
        output_size);
}

extern "C" std::size_t m3e_appspec_node_layout_evidence_json(
    char* output,
    std::size_t output_size) {
    if (g_event_document == nullptr) {
        if (output != nullptr && output_size != 0) output[0] = '\0';
        return 0;
    }
    lv_obj_update_layout(lv_screen_active());
    return m3e::appspec::node_layout_evidence_json(
        *g_event_document,
        output,
        output_size);
}

extern "C" void m3e_appspec_reset_mounted_document(void) {
    delete g_event_document;
    g_event_document = nullptr;
    g_event_bridge = {};
}

extern "C" std::size_t m3e_appspec_mounted_node_count(void) {
    return g_event_document == nullptr
               ? 0
               : g_event_document->node_count;
}

extern "C" std::size_t m3e_appspec_mounted_event_count(void) {
    return g_event_document == nullptr
               ? 0
               : g_event_document->event_count;
}
