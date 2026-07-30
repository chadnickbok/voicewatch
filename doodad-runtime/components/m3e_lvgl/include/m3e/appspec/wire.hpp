#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/appspec/runtime.hpp"
#include "m3e/semantics/semantic_tree.hpp"

namespace m3e::appspec {

struct WireDocument;
using WireEventSink = void (*)(const UiEvent& event, void* context);

constexpr std::size_t kMaximumWireBytes = 4096;
constexpr std::size_t kMaximumWireStrings = 4096;
constexpr std::size_t kMaximumWireEvents = 128;
constexpr std::size_t kMaximumWireKeys = 64;
constexpr std::uint16_t kWireNoParent = 0xffffU;

struct WireEvent {
    std::uint16_t node_index;
    std::uint16_t action_id_offset;
    EventKind kind;
    WireDocument* document;
    WireEventSink sink;
    void* sink_context;
};

struct WireNode {
    std::uint16_t id_offset;
    std::uint16_t primary_text_offset;
    std::uint16_t secondary_text_offset;
    std::uint16_t semantic_label_offset;
    std::uint16_t key_start;
    std::uint16_t parent_index;
    std::uint8_t depth;
    std::uint8_t child_count;
    ComponentKind kind;
    std::uint8_t variant;
    std::uint8_t tone;
    std::uint8_t size;
    std::uint8_t gap;
    std::uint8_t alignment;
    std::uint8_t key_count;
    std::uint8_t key_columns;
    std::uint8_t event_count;
    std::int32_t value;
    std::int32_t maximum;
    bool visible;
    bool enabled;
    void* mounted_object;
};

struct WireDocument {
    std::array<WireNode, Reconciler::kCapacity> nodes{};
    std::array<WireEvent, kMaximumWireEvents> events{};
    std::array<std::uint16_t, kMaximumWireKeys> key_offsets{};
    std::array<char, kMaximumWireStrings> strings{};
    std::size_t node_count = 0;
    std::size_t event_count = 0;
    std::size_t key_count = 0;
    std::size_t string_bytes = 1;
    std::uint16_t app_id_offset = 0;
    std::uint32_t schema_version = 0;

    const char* string_at(std::uint16_t offset) const;
};

enum class WireError : std::uint8_t {
    none,
    empty,
    too_large,
    truncated,
    non_canonical,
    unsupported_type,
    unexpected_key,
    missing_field,
    invalid_integer,
    invalid_boolean,
    invalid_text,
    string_capacity,
    invalid_identifier,
    too_many_nodes,
    too_many_events,
    invalid_component,
    invalid_parent,
    excessive_depth,
    too_many_children,
    missing_semantics,
    trailing_data,
};

struct WireResult {
    WireError error;
    std::uint16_t node_index;
    std::uint16_t byte_offset;

    constexpr bool ok() const { return error == WireError::none; }
};

WireResult decode_canonical_cbor(
    const std::uint8_t* bytes,
    std::size_t size,
    WireDocument& output);
const char* wire_error_name(WireError error);
std::size_t encode_event_canonical_cbor(
    const UiEvent& event,
    std::uint8_t* output,
    std::size_t output_size);
bool build_semantic_tree(
    const WireDocument& document,
    SemanticTree& output);

}  // namespace m3e::appspec
