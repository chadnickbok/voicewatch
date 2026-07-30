#include "m3e/appspec/c_api.h"

#include <cstdio>
#include <new>

#include "m3e/appspec/command_batch.hpp"
#include "m3e/appspec/renderer.hpp"
#include "m3e/appspec/wire.hpp"
#include "m3e/theme/resolved_theme.hpp"

namespace {

struct EventBridge {
    m3e_appspec_event_callback_t callback;
    void* context;
};

m3e::appspec::WireDocument* g_event_document = nullptr;
EventBridge g_event_bridge{};

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
