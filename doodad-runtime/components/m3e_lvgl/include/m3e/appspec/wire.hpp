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
constexpr std::size_t kMaximumMountedEventBindings = 256;
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
    std::uint16_t semantic_value_offset;
    std::uint16_t semantic_hint_offset;
    std::uint16_t icon_offset;
    std::uint16_t key_start;
    std::array<std::uint16_t, 13> samples{};
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
    std::uint8_t sample_count;
    std::uint8_t event_count;
    std::uint8_t max_lines;
    std::uint8_t voice_state;
    std::uint32_t property_mask;
    std::int32_t value;
    std::int32_t minimum;
    std::int32_t maximum;
    std::int32_t step;
    bool visible;
    bool enabled;
    bool checked;
    void* mounted_object;
};

enum class MountedEventValue : std::uint8_t {
    none,
    integer,
    node_value,
    stepper_decrement,
    stepper_increment,
    checked_state,
    keypad_key,
};

struct MountedEventBinding {
    WireEvent* event = nullptr;
    MountedEventValue value_kind = MountedEventValue::none;
    std::int32_t integer_value = 0;
    std::uint16_t key_index = 0;
};

struct WireDocument {
    std::array<WireNode, Reconciler::kCapacity> nodes{};
    std::array<WireEvent, kMaximumWireEvents> events{};
    std::array<std::uint16_t, kMaximumWireKeys> key_offsets{};
    std::array<MountedEventBinding, kMaximumMountedEventBindings>
        mounted_event_bindings{};
    std::array<char, kMaximumWireStrings> strings{};
    std::size_t node_count = 0;
    std::size_t event_count = 0;
    std::size_t key_count = 0;
    std::size_t mounted_event_binding_count = 0;
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
