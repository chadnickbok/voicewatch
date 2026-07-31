#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e::appspec {

enum class ComponentKind : std::uint8_t {
    screen,
    column,
    row,
    scroll,
    text,
    button,
    card,
    progress,
    stepper,
    toggle,
    keypad,
    voice_orb,
    live_card,
    image,
};

enum class EventKind : std::uint8_t {
    tap,
    long_press,
    repeat,
    value_changing,
    value_committed,
    checked_changed,
    page_changed,
    dismissed,
    submit,
    retry,
    cancel,
};

enum class EventValueKind : std::uint8_t {
    none,
    integer,
    boolean,
    text,
};

struct EventValue {
    EventValueKind kind = EventValueKind::none;
    std::int32_t integer_value = 0;
    bool boolean_value = false;
    const char* text_value = nullptr;

    static constexpr EventValue integer(std::int32_t value) {
        return {EventValueKind::integer, value, false, nullptr};
    }
    static constexpr EventValue boolean(bool value) {
        return {EventValueKind::boolean, 0, value, nullptr};
    }
    static constexpr EventValue text(const char* value) {
        return {EventValueKind::text, 0, false, value};
    }
};

struct Node {
    const char* id;
    ComponentKind kind;
    std::uint16_t parent_index;
    std::uint8_t depth;
    std::uint8_t child_count;
    std::uint64_t props_hash;
    const char* semantic_label;
    bool interactive;
    bool visible;
    bool enabled;
};

struct SpecView {
    const Node* nodes;
    std::size_t count;
};

enum class ValidationError : std::uint8_t {
    none,
    empty,
    too_many_nodes,
    root_not_screen,
    missing_id,
    duplicate_id,
    invalid_parent,
    excessive_depth,
    too_many_children,
    missing_semantics,
};

struct ValidationResult {
    ValidationError error;
    std::uint16_t node_index;

    constexpr bool ok() const { return error == ValidationError::none; }
};

ValidationResult validate(SpecView spec);

enum class PatchKind : std::uint8_t {
    set_properties,
    set_visibility,
    set_enabled,
    insert_leaf,
    remove_leaf,
    replace_leaf,
};

struct Patch {
    PatchKind kind;
    const char* node_id;
    const char* parent_id;
    ComponentKind component_kind;
    std::uint64_t props_hash;
    const char* semantic_label;
    bool boolean_value;
};

struct ViewHandle {
    std::array<char, 65> id;
    ComponentKind kind;
    std::uint16_t parent_index;
    std::uint64_t props_hash;
    bool visible;
    bool enabled;
};

class Reconciler {
 public:
    static constexpr std::size_t kCapacity = 250;

    ValidationResult mount(SpecView spec);
    bool apply_transaction(const Patch* patches, std::size_t count);
    const ViewHandle* find(const char* id) const;
    std::size_t size() const;
    std::uint32_t generation() const;

 private:
    std::array<ViewHandle, kCapacity> handles_{};
    std::size_t size_ = 0;
    std::uint32_t generation_ = 0;
};

struct CapabilityManifest {
    const char* runtime_api;
    const char* appspec_version;
    const char* display_profile;
    const char* color_format;
    std::uint16_t nodes_per_screen;
    std::uint8_t tree_depth;
    std::uint8_t animation_properties;
    std::uint32_t component_set_hash;
};

CapabilityManifest capabilities();

struct UiEvent {
    std::uint8_t schema;
    const char* app_id;
    const char* screen_id;
    const char* node_id;
    const char* action_id;
    EventKind kind;
    std::uint64_t timestamp_monotonic_ms;
    EventValue value{};
};

bool event_is_valid(const UiEvent& event);

}  // namespace m3e::appspec
