#include "m3e/appspec/wire.hpp"

#include <cstring>
#include <limits>

namespace m3e::appspec {
namespace {

enum : std::uint8_t {
    kMajorUnsigned = 0,
    kMajorNegative = 1,
    kMajorText = 3,
    kMajorArray = 4,
    kMajorMap = 5,
    kMajorSimple = 7,
};

bool valid_utf8(const std::uint8_t* bytes, std::size_t size) {
    std::size_t index = 0;
    while (index < size) {
        const auto first = bytes[index++];
        if (first == 0) return false;
        if (first <= 0x7f) continue;
        std::uint32_t code_point = 0;
        std::size_t trailing = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            code_point = first & 0x1fU;
            trailing = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            code_point = first & 0x0fU;
            trailing = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            code_point = first & 0x07U;
            trailing = 3;
        } else {
            return false;
        }
        if (index + trailing > size) return false;
        for (std::size_t offset = 0; offset < trailing; ++offset) {
            const auto next = bytes[index++];
            if ((next & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (next & 0x3fU);
        }
        if ((trailing == 2 && code_point < 0x800U) ||
            (trailing == 3 && code_point < 0x10000U) ||
            (code_point >= 0xd800U && code_point <= 0xdfffU) ||
            code_point > 0x10ffffU) {
            return false;
        }
    }
    return true;
}

bool valid_identifier(const char* value) {
    if (value == nullptr || value[0] < 'a' || value[0] > 'z') return false;
    std::size_t length = 0;
    for (; value[length] != '\0'; ++length) {
        if (length >= 64) return false;
        const auto character = value[length];
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') ||
              character == '_' || character == '.' ||
              character == '-')) {
            return false;
        }
    }
    return length != 0;
}

SemanticRole semantic_role(const WireNode& node) {
    switch (node.kind) {
        case ComponentKind::screen: return SemanticRole::screen;
        case ComponentKind::column:
        case ComponentKind::row:
        case ComponentKind::scroll: return SemanticRole::list;
        case ComponentKind::text:
            return node.variant <= 1
                ? SemanticRole::heading
                : SemanticRole::text;
        case ComponentKind::button:
        case ComponentKind::keypad:
        case ComponentKind::voice_orb: return SemanticRole::button;
        case ComponentKind::card:
        case ComponentKind::live_card: return SemanticRole::list_item;
        case ComponentKind::progress: return SemanticRole::progress;
        case ComponentKind::stepper: return SemanticRole::slider;
        case ComponentKind::toggle: return SemanticRole::toggle;
    }
    return SemanticRole::text;
}

class Reader {
 public:
    Reader(
        const std::uint8_t* bytes,
        std::size_t size,
        WireDocument& document)
        : bytes_(bytes), size_(size), document_(document) {}

    std::size_t offset() const { return offset_; }
    bool at_end() const { return offset_ == size_; }
    WireError error() const { return error_; }

    bool unsigned_integer(std::uint64_t& value) {
        std::uint8_t major = 0;
        return head(major, value) && require_major(major, kMajorUnsigned);
    }

    bool signed_integer(std::int32_t& value) {
        std::uint8_t major = 0;
        std::uint64_t argument = 0;
        if (!head(major, argument)) return false;
        if (major == kMajorUnsigned) {
            if (argument >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
                return fail(WireError::invalid_integer);
            }
            value = static_cast<std::int32_t>(argument);
            return true;
        }
        if (major == kMajorNegative) {
            if (argument >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
                return fail(WireError::invalid_integer);
            }
            value = -1 - static_cast<std::int32_t>(argument);
            return true;
        }
        return fail(WireError::invalid_integer);
    }

    bool map(std::size_t& count) {
        return collection(kMajorMap, count);
    }

    bool array(std::size_t& count) {
        return collection(kMajorArray, count);
    }

    bool boolean(bool& value) {
        if (offset_ >= size_) return fail(WireError::truncated);
        const auto byte = bytes_[offset_++];
        if (byte == 0xf4U) {
            value = false;
            return true;
        }
        if (byte == 0xf5U) {
            value = true;
            return true;
        }
        return fail(WireError::invalid_boolean);
    }

    bool null_value() {
        if (offset_ >= size_) return fail(WireError::truncated);
        if (bytes_[offset_++] != 0xf6U) {
            return fail(WireError::unsupported_type);
        }
        return true;
    }

    bool next_is_null() const {
        return offset_ < size_ && bytes_[offset_] == 0xf6U;
    }

    bool text(
        std::uint16_t& destination,
        std::size_t maximum_length) {
        std::uint8_t major = 0;
        std::uint64_t length = 0;
        if (!head(major, length) || !require_major(major, kMajorText)) {
            return false;
        }
        if (length > maximum_length ||
            length > size_ - offset_) {
            return fail(
                length > size_ - offset_
                    ? WireError::truncated
                    : WireError::invalid_text);
        }
        if (!valid_utf8(&bytes_[offset_], static_cast<std::size_t>(length))) {
            return fail(WireError::invalid_text);
        }
        if (document_.string_bytes + length + 1 >
            document_.strings.size()) {
            return fail(WireError::string_capacity);
        }
        destination =
            static_cast<std::uint16_t>(document_.string_bytes);
        std::memcpy(
            &document_.strings[document_.string_bytes],
            &bytes_[offset_],
            static_cast<std::size_t>(length));
        document_.string_bytes += static_cast<std::size_t>(length);
        document_.strings[document_.string_bytes++] = '\0';
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool ordered_key(
        std::uint64_t& key,
        std::uint64_t& previous,
        bool& has_previous) {
        if (!unsigned_integer(key)) return false;
        if (has_previous && key <= previous) {
            return fail(WireError::non_canonical);
        }
        previous = key;
        has_previous = true;
        return true;
    }

 private:
    bool collection(std::uint8_t expected, std::size_t& count) {
        std::uint8_t major = 0;
        std::uint64_t argument = 0;
        if (!head(major, argument) || !require_major(major, expected)) {
            return false;
        }
        if (argument > std::numeric_limits<std::size_t>::max()) {
            return fail(WireError::invalid_integer);
        }
        count = static_cast<std::size_t>(argument);
        return true;
    }

    bool require_major(std::uint8_t actual, std::uint8_t expected) {
        return actual == expected || fail(WireError::unsupported_type);
    }

    bool head(std::uint8_t& major, std::uint64_t& argument) {
        if (offset_ >= size_) return fail(WireError::truncated);
        const auto initial = bytes_[offset_++];
        major = initial >> 5U;
        const auto additional = initial & 0x1fU;
        if (additional < 24U) {
            argument = additional;
            return true;
        }
        if (additional == 31U || additional > 27U) {
            return fail(WireError::non_canonical);
        }
        const auto width =
            additional == 24U ? 1U :
            additional == 25U ? 2U :
            additional == 26U ? 4U : 8U;
        if (width > size_ - offset_) return fail(WireError::truncated);
        argument = 0;
        for (std::size_t index = 0; index < width; ++index) {
            argument = (argument << 8U) | bytes_[offset_++];
        }
        const std::uint64_t minimum =
            width == 1U ? 24ULL :
            width == 2U ? 256ULL :
            width == 4U ? 65536ULL : 4294967296ULL;
        if (argument < minimum) return fail(WireError::non_canonical);
        return true;
    }

    bool fail(WireError error) {
        if (error_ == WireError::none) error_ = error;
        return false;
    }

    const std::uint8_t* bytes_;
    std::size_t size_;
    WireDocument& document_;
    std::size_t offset_ = 0;
    WireError error_ = WireError::none;
};

bool read_small_enum(
    Reader& reader,
    std::uint8_t& output,
    std::uint8_t maximum) {
    std::uint64_t value = 0;
    if (!reader.unsigned_integer(value) || value > maximum) return false;
    output = static_cast<std::uint8_t>(value);
    return true;
}

bool parse_properties(
    Reader& reader,
    WireDocument& document,
    WireNode& node,
    std::uint16_t node_index) {
    std::size_t count = 0;
    if (!reader.map(count) || count > 12) return false;
    std::uint64_t previous = 0;
    bool has_previous = false;
    bool has_primary = false;
    bool has_value = false;
    bool has_maximum = false;
    std::uint16_t property_mask = 0;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint64_t key = 0;
        if (!reader.ordered_key(key, previous, has_previous)) return false;
        if (key > 11) return false;
        property_mask |= static_cast<std::uint16_t>(1U << key);
        switch (key) {
            case 0:
                if (!reader.text(node.primary_text_offset, 256)) return false;
                has_primary = true;
                break;
            case 1:
                if (!reader.text(node.secondary_text_offset, 256)) return false;
                break;
            case 2:
                if (!reader.signed_integer(node.value)) return false;
                has_value = true;
                break;
            case 3:
                if (!reader.signed_integer(node.maximum)) return false;
                has_maximum = true;
                break;
            case 4:
                if (!read_small_enum(reader, node.variant, 5)) return false;
                break;
            case 5:
                if (!read_small_enum(reader, node.tone, 4)) return false;
                break;
            case 6:
                if (!read_small_enum(reader, node.size, 2)) return false;
                break;
            case 7: {
                bool checked = false;
                if (!reader.boolean(checked)) return false;
                node.value = checked ? 1 : 0;
                has_value = true;
                break;
            }
            case 8:
                if (!read_small_enum(reader, node.gap, 4)) return false;
                break;
            case 9:
                if (!read_small_enum(reader, node.alignment, 3)) return false;
                break;
            case 10: {
                std::size_t key_count = 0;
                if (!reader.array(key_count) || key_count == 0 ||
                    key_count > 20 ||
                    document.key_count + key_count >
                        document.key_offsets.size()) {
                    return false;
                }
                node.key_start =
                    static_cast<std::uint16_t>(document.key_count);
                node.key_count = static_cast<std::uint8_t>(key_count);
                for (std::size_t key_index = 0;
                     key_index < key_count;
                     ++key_index) {
                    if (!reader.text(
                            document.key_offsets[document.key_count++],
                            4)) {
                        return false;
                    }
                }
                break;
            }
            case 11:
                if (!read_small_enum(reader, node.key_columns, 5) ||
                    node.key_columns < 2) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }

    std::uint16_t allowed = 0;
    switch (node.kind) {
        case ComponentKind::screen:
        case ComponentKind::column:
        case ComponentKind::row:
        case ComponentKind::scroll:
            allowed = (1U << 8U) | (1U << 9U);
            break;
        case ComponentKind::text:
            allowed = (1U << 0U) | (1U << 4U) | (1U << 9U);
            if (!has_primary) return false;
            break;
        case ComponentKind::button:
            allowed =
                (1U << 0U) | (1U << 4U) |
                (1U << 5U) | (1U << 6U);
            if (!has_primary) return false;
            break;
        case ComponentKind::card:
            allowed = (1U << 0U) | (1U << 1U) | (1U << 5U);
            if (!has_primary) return false;
            break;
        case ComponentKind::toggle:
            allowed = (1U << 0U) | (1U << 5U) | (1U << 7U);
            if (!has_primary || !has_value) return false;
            break;
        case ComponentKind::voice_orb:
            allowed = (1U << 0U) | (1U << 1U) | (1U << 5U);
            if (!has_primary) return false;
            break;
        case ComponentKind::live_card:
            allowed =
                (1U << 0U) | (1U << 1U) | (1U << 2U) |
                (1U << 3U) | (1U << 5U);
            if (!has_primary) return false;
            break;
        case ComponentKind::progress:
            allowed =
                (1U << 0U) | (1U << 2U) | (1U << 3U) |
                (1U << 4U) | (1U << 5U);
            if (!has_value || !has_maximum ||
                node.maximum <= 0 ||
                node.value < 0 ||
                node.value > node.maximum) {
                return false;
            }
            break;
        case ComponentKind::stepper:
            allowed =
                (1U << 0U) | (1U << 1U) |
                (1U << 2U) | (1U << 3U);
            if (!has_primary || !has_value) return false;
            break;
        case ComponentKind::keypad:
            (void)node_index;
            allowed = (1U << 10U) | (1U << 11U);
            if (node.key_count == 0 || node.key_columns < 2) return false;
            break;
    }
    return (property_mask & ~allowed) == 0;
}

bool parse_events(
    Reader& reader,
    WireDocument& document,
    std::uint16_t node_index) {
    std::size_t count = 0;
    if (!reader.array(count) ||
        document.event_count + count > document.events.size()) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        std::size_t fields = 0;
        if (!reader.map(fields) || fields != 2) return false;
        auto& event = document.events[document.event_count++];
        event.node_index = node_index;
        std::uint64_t previous = 0;
        bool has_previous = false;
        bool has_kind = false;
        bool has_action = false;
        for (std::size_t field = 0; field < fields; ++field) {
            std::uint64_t key = 0;
            if (!reader.ordered_key(key, previous, has_previous)) return false;
            if (key == 0) {
                std::uint64_t kind = 0;
                if (!reader.unsigned_integer(kind) ||
                    kind > static_cast<std::uint8_t>(EventKind::cancel)) {
                    return false;
                }
                event.kind = static_cast<EventKind>(kind);
                has_kind = true;
            } else if (key == 1) {
                if (!reader.text(event.action_id_offset, 64) ||
                    !valid_identifier(
                        document.string_at(event.action_id_offset))) {
                    return false;
                }
                has_action = true;
            } else {
                return false;
            }
        }
        if (!has_kind || !has_action) return false;
    }
    document.nodes[node_index].event_count =
        static_cast<std::uint8_t>(count);
    return true;
}

bool parse_node(
    Reader& reader,
    WireDocument& document,
    std::uint16_t node_index) {
    std::size_t fields = 0;
    if (!reader.map(fields) || fields < 4 || fields > 8) return false;
    auto& node = document.nodes[node_index];
    node.parent_index = kWireNoParent;
    node.visible = true;
    node.enabled = true;
    node.maximum = 100;
    node.gap = 3;
    node.alignment = 1;
    node.key_columns = 4;

    std::uint64_t previous = 0;
    bool has_previous = false;
    std::uint16_t required = 0;
    for (std::size_t field = 0; field < fields; ++field) {
        std::uint64_t key = 0;
        if (!reader.ordered_key(key, previous, has_previous)) return false;
        switch (key) {
            case 0:
                if (!reader.text(node.id_offset, 64) ||
                    !valid_identifier(document.string_at(node.id_offset))) {
                    return false;
                }
                required |= 1U;
                break;
            case 1: {
                std::uint64_t kind = 0;
                if (!reader.unsigned_integer(kind) ||
                    kind > static_cast<std::uint8_t>(
                        ComponentKind::live_card)) {
                    return false;
                }
                node.kind = static_cast<ComponentKind>(kind);
                required |= 2U;
                break;
            }
            case 2:
                if (reader.next_is_null()) {
                    if (!reader.null_value()) return false;
                    node.parent_index = kWireNoParent;
                } else {
                    std::uint64_t parent = 0;
                    if (!reader.unsigned_integer(parent) ||
                        parent >= node_index) {
                        return false;
                    }
                    node.parent_index =
                        static_cast<std::uint16_t>(parent);
                }
                required |= 4U;
                break;
            case 3:
                if ((required & 2U) == 0 ||
                    !parse_properties(
                        reader, document, node, node_index)) {
                    return false;
                }
                required |= 8U;
                break;
            case 4:
                if (!reader.boolean(node.visible)) return false;
                break;
            case 5:
                if (!reader.boolean(node.enabled)) return false;
                break;
            case 6:
                if (!reader.text(node.semantic_label_offset, 128)) return false;
                break;
            case 7:
                if (!parse_events(reader, document, node_index)) return false;
                break;
            default:
                return false;
        }
    }
    return required == 0x0fU;
}

WireResult failure(
    const Reader& reader,
    std::uint16_t node,
    WireError fallback) {
    const auto error =
        reader.error() == WireError::none ? fallback : reader.error();
    return {
        error,
        node,
        static_cast<std::uint16_t>(
            reader.offset() > 0xffffU ? 0xffffU : reader.offset())};
}

}  // namespace

const char* WireDocument::string_at(std::uint16_t offset) const {
    return offset < string_bytes ? &strings[offset] : "";
}

WireResult decode_canonical_cbor(
    const std::uint8_t* bytes,
    std::size_t size,
    WireDocument& output) {
    output = {};
    output.strings[0] = '\0';
    output.string_bytes = 1;
    if (bytes == nullptr || size == 0) {
        return {WireError::empty, 0, 0};
    }
    if (size > kMaximumWireBytes) {
        return {WireError::too_large, 0, 0};
    }

    Reader reader(bytes, size, output);
    std::size_t fields = 0;
    if (!reader.map(fields) || fields != 3) {
        return failure(reader, 0, WireError::missing_field);
    }
    std::uint64_t previous = 0;
    bool has_previous = false;
    std::uint8_t required = 0;
    for (std::size_t field = 0; field < fields; ++field) {
        std::uint64_t key = 0;
        if (!reader.ordered_key(key, previous, has_previous)) {
            return failure(reader, 0, WireError::non_canonical);
        }
        if (key == 0) {
            std::uint64_t version = 0;
            if (!reader.unsigned_integer(version) || version != 1) {
                return failure(reader, 0, WireError::invalid_integer);
            }
            output.schema_version = 1;
            required |= 1U;
        } else if (key == 1) {
            if (!reader.text(output.app_id_offset, 64) ||
                !valid_identifier(output.string_at(output.app_id_offset))) {
                return failure(reader, 0, WireError::invalid_identifier);
            }
            required |= 2U;
        } else if (key == 2) {
            std::size_t count = 0;
            if (!reader.array(count)) {
                return failure(reader, 0, WireError::unsupported_type);
            }
            if (count == 0 || count > output.nodes.size()) {
                return {
                    WireError::too_many_nodes,
                    0,
                    static_cast<std::uint16_t>(reader.offset())};
            }
            output.node_count = count;
            for (std::size_t index = 0; index < count; ++index) {
                if (!parse_node(
                        reader,
                        output,
                        static_cast<std::uint16_t>(index))) {
                    return failure(
                        reader,
                        static_cast<std::uint16_t>(index),
                        WireError::missing_field);
                }
            }
            required |= 4U;
        } else {
            return failure(reader, 0, WireError::unexpected_key);
        }
    }
    if (required != 7U) {
        return failure(reader, 0, WireError::missing_field);
    }
    if (!reader.at_end()) {
        return failure(reader, 0, WireError::trailing_data);
    }

    std::size_t scroll_count = 0;
    for (std::size_t index = 0; index < output.node_count; ++index) {
        auto& node = output.nodes[index];
        if (index == 0) {
            if (node.kind != ComponentKind::screen ||
                node.parent_index != kWireNoParent) {
                return {
                    WireError::invalid_parent,
                    0,
                    static_cast<std::uint16_t>(size)};
            }
            node.depth = 0;
        } else {
            if (node.parent_index >= index) {
                return {
                    WireError::invalid_parent,
                    static_cast<std::uint16_t>(index),
                    static_cast<std::uint16_t>(size)};
            }
            auto& parent = output.nodes[node.parent_index];
            const bool parent_is_container =
                parent.kind == ComponentKind::screen ||
                parent.kind == ComponentKind::column ||
                parent.kind == ComponentKind::row ||
                parent.kind == ComponentKind::scroll;
            if (!parent_is_container || node.kind == ComponentKind::screen) {
                return {
                    WireError::invalid_parent,
                    static_cast<std::uint16_t>(index),
                    static_cast<std::uint16_t>(size)};
            }
            node.depth = static_cast<std::uint8_t>(parent.depth + 1U);
            if (node.depth > 12) {
                return {
                    WireError::excessive_depth,
                    static_cast<std::uint16_t>(index),
                    static_cast<std::uint16_t>(size)};
            }
            if (++parent.child_count > 32) {
                return {
                    WireError::too_many_children,
                    node.parent_index,
                    static_cast<std::uint16_t>(size)};
            }
        }
        if (node.kind == ComponentKind::scroll && ++scroll_count > 1) {
            return {
                WireError::invalid_component,
                static_cast<std::uint16_t>(index),
                static_cast<std::uint16_t>(size)};
        }
        const bool interactive =
            node.kind == ComponentKind::button ||
            node.kind == ComponentKind::stepper ||
            node.kind == ComponentKind::toggle ||
            node.kind == ComponentKind::keypad ||
            node.kind == ComponentKind::voice_orb;
        if (interactive &&
            output.string_at(node.semantic_label_offset)[0] == '\0') {
            return {
                WireError::missing_semantics,
                static_cast<std::uint16_t>(index),
                static_cast<std::uint16_t>(size)};
        }
        if (interactive && node.event_count == 0) {
            return {
                WireError::missing_semantics,
                static_cast<std::uint16_t>(index),
                static_cast<std::uint16_t>(size)};
        }
    }
    for (std::size_t left = 0; left < output.node_count; ++left) {
        for (std::size_t right = left + 1;
             right < output.node_count;
             ++right) {
            if (std::strcmp(
                    output.string_at(output.nodes[left].id_offset),
                    output.string_at(output.nodes[right].id_offset)) == 0) {
                return {
                    WireError::invalid_identifier,
                    static_cast<std::uint16_t>(right),
                    static_cast<std::uint16_t>(size)};
            }
        }
    }
    return {WireError::none, 0, 0};
}

const char* wire_error_name(WireError error) {
    switch (error) {
        case WireError::none: return "none";
        case WireError::empty: return "empty";
        case WireError::too_large: return "too_large";
        case WireError::truncated: return "truncated";
        case WireError::non_canonical: return "non_canonical";
        case WireError::unsupported_type: return "unsupported_type";
        case WireError::unexpected_key: return "unexpected_key";
        case WireError::missing_field: return "missing_field";
        case WireError::invalid_integer: return "invalid_integer";
        case WireError::invalid_boolean: return "invalid_boolean";
        case WireError::invalid_text: return "invalid_text";
        case WireError::string_capacity: return "string_capacity";
        case WireError::invalid_identifier: return "invalid_identifier";
        case WireError::too_many_nodes: return "too_many_nodes";
        case WireError::too_many_events: return "too_many_events";
        case WireError::invalid_component: return "invalid_component";
        case WireError::invalid_parent: return "invalid_parent";
        case WireError::excessive_depth: return "excessive_depth";
        case WireError::too_many_children: return "too_many_children";
        case WireError::missing_semantics: return "missing_semantics";
        case WireError::trailing_data: return "trailing_data";
    }
    return "unknown";
}

std::size_t encode_event_canonical_cbor(
    const UiEvent& event,
    std::uint8_t* output,
    std::size_t output_size) {
    if (output == nullptr || output_size == 0 ||
        !event_is_valid(event)) {
        return 0;
    }
    class Writer {
     public:
        Writer(std::uint8_t* bytes, std::size_t size)
            : bytes_(bytes), size_(size) {}

        bool head(std::uint8_t major, std::uint64_t value) {
            if (value < 24U) {
                return byte(static_cast<std::uint8_t>(
                    (major << 5U) | value));
            }
            if (value <= 0xffU) {
                return byte(static_cast<std::uint8_t>(
                           (major << 5U) | 24U)) &&
                       byte(static_cast<std::uint8_t>(value));
            }
            if (value <= 0xffffU) {
                return byte(static_cast<std::uint8_t>(
                           (major << 5U) | 25U)) &&
                       integer_bytes(value, 2);
            }
            if (value <= 0xffffffffU) {
                return byte(static_cast<std::uint8_t>(
                           (major << 5U) | 26U)) &&
                       integer_bytes(value, 4);
            }
            return byte(static_cast<std::uint8_t>(
                       (major << 5U) | 27U)) &&
                   integer_bytes(value, 8);
        }

        bool text(const char* value) {
            const auto length = std::strlen(value);
            if (!head(kMajorText, length) ||
                length > size_ - offset_) {
                return false;
            }
            std::memcpy(&bytes_[offset_], value, length);
            offset_ += length;
            return true;
        }

        std::size_t size() const { return offset_; }

     private:
        bool byte(std::uint8_t value) {
            if (offset_ >= size_) return false;
            bytes_[offset_++] = value;
            return true;
        }

        bool integer_bytes(std::uint64_t value, std::size_t width) {
            if (width > size_ - offset_) return false;
            for (std::size_t index = 0; index < width; ++index) {
                bytes_[offset_ + width - index - 1] =
                    static_cast<std::uint8_t>(value >> (index * 8U));
            }
            offset_ += width;
            return true;
        }

        std::uint8_t* bytes_;
        std::size_t size_;
        std::size_t offset_ = 0;
    };

    Writer writer(output, output_size);
    const bool ok =
        writer.head(kMajorMap, 7) &&
        writer.head(kMajorUnsigned, 0) &&
        writer.head(kMajorUnsigned, event.schema) &&
        writer.head(kMajorUnsigned, 1) &&
        writer.text(event.app_id) &&
        writer.head(kMajorUnsigned, 2) &&
        writer.text(event.screen_id) &&
        writer.head(kMajorUnsigned, 3) &&
        writer.text(event.node_id) &&
        writer.head(kMajorUnsigned, 4) &&
        writer.text(event.action_id) &&
        writer.head(kMajorUnsigned, 5) &&
        writer.head(
            kMajorUnsigned,
            static_cast<std::uint8_t>(event.kind)) &&
        writer.head(kMajorUnsigned, 6) &&
        writer.head(kMajorUnsigned, event.timestamp_monotonic_ms);
    return ok ? writer.size() : 0;
}

bool build_semantic_tree(
    const WireDocument& document,
    SemanticTree& output) {
    output.clear();
    if (document.node_count == 0) return false;
    for (std::size_t index = 0; index < document.node_count; ++index) {
        const auto& node = document.nodes[index];
        std::uint16_t first_child = SemanticTree::kNoIndex;
        std::uint16_t child_count = 0;
        for (std::size_t candidate = index + 1;
             candidate < document.node_count;
             ++candidate) {
            if (document.nodes[candidate].parent_index != index) continue;
            if (first_child == SemanticTree::kNoIndex) {
                first_child = static_cast<std::uint16_t>(candidate);
            }
            ++child_count;
        }
        const auto* semantic_label =
            document.string_at(node.semantic_label_offset);
        if (semantic_label[0] == '\0') {
            semantic_label =
                document.string_at(node.primary_text_offset);
        }
        if (semantic_label[0] == '\0' &&
            node.kind == ComponentKind::screen) {
            semantic_label = document.string_at(document.app_id_offset);
        }
        std::uint16_t state = semantic_none;
        if (!node.enabled) state |= semantic_disabled;
        if (node.kind == ComponentKind::toggle && node.value != 0) {
            state |= semantic_checked;
        }
        if (!output.add(
                {
                    document.string_at(node.id_offset),
                    semantic_role(node),
                    semantic_label,
                    nullptr,
                    state,
                    node.parent_index == kWireNoParent
                        ? SemanticTree::kNoIndex
                        : node.parent_index,
                    first_child,
                    child_count,
                })) {
            output.clear();
            return false;
        }
    }
    if (!output.validate()) {
        output.clear();
        return false;
    }
    return true;
}

}  // namespace m3e::appspec
