#include "m3e/appspec/command_batch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>

#include "lvgl.h"
#include "m3e/appspec/canvas_display_list.hpp"
#include "m3e/assets/weather_icon_assets.hpp"
#include "m3e/generated/weather_icons.hpp"
#include "m3e/generated/weather_tokens.hpp"

namespace m3e::appspec {
namespace {

enum : std::uint8_t {
    kMajorUnsigned = 0,
    kMajorNegative = 1,
    kMajorText = 3,
    kMajorArray = 4,
    kMajorMap = 5,
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
        const char c = value[length];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return length != 0;
}

class Reader {
 public:
    Reader(
        const std::uint8_t* bytes,
        std::size_t size,
        CommandBatch& batch)
        : bytes_(bytes), size_(size), batch_(batch) {}

    std::size_t offset() const { return offset_; }
    bool at_end() const { return offset_ == size_; }
    CommandError error() const { return error_; }

    bool unsigned_integer(std::uint64_t& value) {
        std::uint8_t major = 0;
        return head(major, value) && require_major(major, kMajorUnsigned);
    }

    bool signed_integer(std::int64_t& value) {
        std::uint8_t major = 0;
        std::uint64_t argument = 0;
        if (!head(major, argument)) return false;
        if (major == kMajorUnsigned) {
            if (argument >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return fail(CommandError::invalid_integer);
            }
            value = static_cast<std::int64_t>(argument);
            return true;
        }
        if (major == kMajorNegative) {
            if (argument >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return fail(CommandError::invalid_integer);
            }
            value = -1 - static_cast<std::int64_t>(argument);
            return true;
        }
        return fail(CommandError::invalid_integer);
    }

    bool map(std::size_t& count) { return collection(kMajorMap, count); }
    bool array(std::size_t& count) { return collection(kMajorArray, count); }

    bool boolean(bool& value) {
        if (offset_ >= size_) return fail(CommandError::truncated);
        const auto byte = bytes_[offset_++];
        if (byte == 0xf4U) {
            value = false;
            return true;
        }
        if (byte == 0xf5U) {
            value = true;
            return true;
        }
        return fail(CommandError::invalid_boolean);
    }

    bool text(std::uint16_t& destination, std::size_t maximum_length) {
        std::uint8_t major = 0;
        std::uint64_t length = 0;
        if (!head(major, length) || !require_major(major, kMajorText)) {
            return false;
        }
        if (length > maximum_length) return fail(CommandError::invalid_text);
        if (length > size_ - offset_) return fail(CommandError::truncated);
        if (!valid_utf8(&bytes_[offset_], static_cast<std::size_t>(length))) {
            return fail(CommandError::invalid_text);
        }
        if (batch_.string_bytes + length + 1 > batch_.strings.size()) {
            return fail(CommandError::string_capacity);
        }
        destination = static_cast<std::uint16_t>(batch_.string_bytes);
        std::memcpy(
            &batch_.strings[batch_.string_bytes],
            &bytes_[offset_],
            static_cast<std::size_t>(length));
        batch_.string_bytes += static_cast<std::size_t>(length);
        batch_.strings[batch_.string_bytes++] = '\0';
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    bool ordered_key(
        std::uint64_t& key,
        std::uint64_t& previous,
        bool& has_previous) {
        if (!unsigned_integer(key)) return false;
        if (has_previous && key <= previous) {
            return fail(CommandError::non_canonical);
        }
        previous = key;
        has_previous = true;
        return true;
    }

    bool fail(CommandError error) {
        if (error_ == CommandError::none) error_ = error;
        return false;
    }

 private:
    bool collection(std::uint8_t expected, std::size_t& count) {
        std::uint8_t major = 0;
        std::uint64_t argument = 0;
        if (!head(major, argument) || !require_major(major, expected)) {
            return false;
        }
        if (argument > std::numeric_limits<std::size_t>::max()) {
            return fail(CommandError::invalid_integer);
        }
        count = static_cast<std::size_t>(argument);
        return true;
    }

    bool require_major(std::uint8_t actual, std::uint8_t expected) {
        return actual == expected || fail(CommandError::unsupported_type);
    }

    bool head(std::uint8_t& major, std::uint64_t& argument) {
        if (offset_ >= size_) return fail(CommandError::truncated);
        const auto initial = bytes_[offset_++];
        major = initial >> 5U;
        const auto additional = initial & 0x1fU;
        if (additional < 24U) {
            argument = additional;
            return true;
        }
        if (additional == 31U || additional > 27U) {
            return fail(CommandError::non_canonical);
        }
        const std::size_t width =
            additional == 24U ? 1U :
            additional == 25U ? 2U :
            additional == 26U ? 4U : 8U;
        if (width > size_ - offset_) return fail(CommandError::truncated);
        argument = 0;
        for (std::size_t index = 0; index < width; ++index) {
            argument = (argument << 8U) | bytes_[offset_++];
        }
        const std::uint64_t minimum =
            width == 1U ? 24ULL :
            width == 2U ? 256ULL :
            width == 4U ? 65536ULL : 4294967296ULL;
        if (argument < minimum) return fail(CommandError::non_canonical);
        return true;
    }

    const std::uint8_t* bytes_;
    std::size_t size_;
    CommandBatch& batch_;
    std::size_t offset_ = 0;
    CommandError error_ = CommandError::none;
};

CommandDomain domain_for(CommandKind kind) {
    return kind == CommandKind::state_put ||
            kind == CommandKind::state_remove
        ? CommandDomain::state
        : CommandDomain::ui;
}

bool parse_command(Reader& reader, CommandBatch& batch, Command& command) {
    std::size_t count = 0;
    if (!reader.map(count) || count < 2 || count > 4) return false;
    bool has_kind = false;
    bool has_target = false;
    bool has_property = false;
    bool has_value = false;
    std::uint64_t previous = 0;
    bool has_previous = false;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint64_t key = 0;
        if (!reader.ordered_key(key, previous, has_previous)) return false;
        switch (key) {
            case 0: {
                std::uint64_t value = 0;
                if (!reader.unsigned_integer(value) || value > 4) {
                    return reader.fail(CommandError::invalid_command);
                }
                command.kind = static_cast<CommandKind>(value);
                has_kind = true;
                break;
            }
            case 1:
                if (!reader.text(command.target_offset, 96)) return false;
                has_target = true;
                break;
            case 2: {
                std::uint64_t value = 0;
                if (!reader.unsigned_integer(value) || value > 8 ||
                    (has_kind && command.kind == CommandKind::state_put &&
                     value > 3)) {
                    return reader.fail(CommandError::invalid_command);
                }
                if (has_kind && command.kind == CommandKind::state_put) {
                    command.value_type =
                        static_cast<state::ValueType>(value);
                } else {
                    command.property =
                        static_cast<PropertyKind>(value);
                }
                has_property = true;
                break;
            }
            case 3:
                if (!has_kind) {
                    return reader.fail(CommandError::non_canonical);
                }
                if (command.kind == CommandKind::set_visibility ||
                    command.kind == CommandKind::set_enabled ||
                    (command.kind == CommandKind::set_property &&
                     command.property == PropertyKind::checked) ||
                    (command.kind == CommandKind::state_put &&
                     command.value_type == state::ValueType::boolean)) {
                    if (!reader.boolean(command.boolean_value)) return false;
                } else if (
                    command.kind == CommandKind::set_property &&
                    (command.property == PropertyKind::primary_text ||
                     command.property == PropertyKind::secondary_text ||
                     command.property == PropertyKind::icon ||
                     command.property == PropertyKind::semantic_label ||
                     command.property == PropertyKind::semantic_value)) {
                    if (!reader.text(command.text_offset, 128)) return false;
                } else if (
                    command.kind == CommandKind::set_property &&
                    command.property == PropertyKind::samples) {
                    std::size_t sample_count = 0;
                    if (!reader.array(sample_count) || sample_count < 1 ||
                        sample_count > command.samples.size()) {
                        return reader.fail(CommandError::value_out_of_range);
                    }
                    command.sample_count =
                        static_cast<std::uint8_t>(sample_count);
                    for (std::size_t sample = 0;
                         sample < sample_count; ++sample) {
                        std::uint64_t value = 0;
                        if (!reader.unsigned_integer(value) ||
                            value > std::numeric_limits<std::uint16_t>::max()) {
                            return reader.fail(CommandError::value_out_of_range);
                        }
                        command.samples[sample] =
                            static_cast<std::uint16_t>(value);
                    }
                } else if (
                    command.kind == CommandKind::state_put &&
                    command.value_type == state::ValueType::string) {
                    if (!reader.text(command.text_offset, 64)) return false;
                } else if (!reader.signed_integer(command.integer_value)) {
                    return false;
                }
                has_value = true;
                break;
            default:
                return reader.fail(CommandError::unexpected_key);
        }
    }
    if (!has_kind || !has_target) {
        return reader.fail(CommandError::missing_field);
    }
    const bool valid_shape =
        (command.kind == CommandKind::set_property &&
         has_property && has_value && count == 4) ||
        ((command.kind == CommandKind::set_visibility ||
          command.kind == CommandKind::set_enabled) &&
         !has_property && has_value && count == 3) ||
        (command.kind == CommandKind::state_put &&
         has_property && has_value && count == 4) ||
        (command.kind == CommandKind::state_remove &&
         !has_property && !has_value && count == 2);
    if (!valid_shape) return reader.fail(CommandError::invalid_command);
    const auto* target = batch.string_at(command.target_offset);
    if (domain_for(command.kind) == CommandDomain::ui) {
        if (!valid_identifier(target)) {
            return reader.fail(CommandError::invalid_identifier);
        }
    } else if (!state::valid_path(target)) {
        return reader.fail(CommandError::invalid_path);
    }
    return true;
}

WireNode* find_node(WireDocument& document, const char* id) {
    for (std::size_t index = 0; index < document.node_count; ++index) {
        if (std::strcmp(
                document.string_at(document.nodes[index].id_offset), id) == 0) {
            return &document.nodes[index];
        }
    }
    return nullptr;
}

bool weather_icon(
    const char* name,
    generated::WeatherIcon& output) {
    if (name == nullptr) return false;
    for (std::size_t index = 0;
         index < generated::kWeatherIconWireNames.size(); ++index) {
        if (generated::kWeatherIconWireNames[index] == name) {
            output = static_cast<generated::WeatherIcon>(index);
            return true;
        }
    }
    return false;
}

lv_color_t weather_color(generated::WeatherColorRole role) {
    const auto& color = generated::kWeatherColors[
        static_cast<std::size_t>(role)].rgb888;
    return lv_color_make(color.red, color.green, color.blue);
}

bool apply_weather_icon(
    lv_obj_t* object,
    const WireNode& node,
    const char* name) {
    generated::WeatherIcon icon{};
    if (object == nullptr || !weather_icon(name, icon)) return false;
    const auto* current = static_cast<const lv_image_dsc_t*>(
        lv_image_get_src(object));
    const auto logical_size = node.size == 0 ? 18 : node.size == 2 ? 32 :
        node.size == 3 ? 64 : 24;
    const auto source_size = current != nullptr && current->header.w > 0
        ? static_cast<std::int32_t>(current->header.w)
        : logical_size;
    const auto* asset = weather_icon_asset(icon, source_size);
    if (asset == nullptr) return false;
    lv_image_set_src(object, asset);
    const auto& spec = generated::kWeatherIcons[
        static_cast<std::size_t>(icon)];
    if (spec.render == generated::WeatherIconRender::mask) {
        lv_obj_set_style_image_recolor(
            object, weather_color(spec.tint_role), 0);
        lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_image_recolor_opa(object, LV_OPA_TRANSP, 0);
    }
    return true;
}

bool supports_text_property(const WireNode& node, PropertyKind property) {
    if (property == PropertyKind::primary_text) {
        return node.kind == ComponentKind::text ||
               node.kind == ComponentKind::button ||
               node.kind == ComponentKind::card ||
               node.kind == ComponentKind::live_card ||
               node.kind == ComponentKind::toggle ||
               node.kind == ComponentKind::voice_orb ||
               node.kind == ComponentKind::canvas;
    }
    return property == PropertyKind::secondary_text &&
           (node.kind == ComponentKind::card ||
            node.kind == ComponentKind::live_card);
}

lv_obj_t* label_for(WireNode& node, PropertyKind property) {
    auto* object = static_cast<lv_obj_t*>(node.mounted_object);
    if (object == nullptr) return nullptr;
    if (node.kind == ComponentKind::text) return object;
    if (node.kind == ComponentKind::button ||
        node.kind == ComponentKind::toggle ||
        node.kind == ComponentKind::voice_orb) {
        const auto count = lv_obj_get_child_count(object);
        for (std::uint32_t index = 0; index < count; ++index) {
            auto* child = lv_obj_get_child(object, index);
            if (child != nullptr &&
                lv_obj_check_type(child, &lv_label_class)) {
                return child;
            }
        }
        return nullptr;
    }
    if (node.kind == ComponentKind::card ||
        node.kind == ComponentKind::live_card) {
        return lv_obj_get_child(
            object,
            property == PropertyKind::primary_text ? 0 : 1);
    }
    return nullptr;
}

lv_obj_t* stepper_value_label(lv_obj_t* object) {
    if (object == nullptr || lv_obj_get_child_count(object) <= 1) {
        return nullptr;
    }
    auto* value_object = lv_obj_get_child(object, 1);
    if (value_object == nullptr) return nullptr;
    if (lv_obj_get_child_count(value_object) > 0) {
        auto* nested = lv_obj_get_child(value_object, 0);
        if (nested != nullptr &&
            lv_obj_check_type(nested, &lv_label_class)) {
            return nested;
        }
    }
    return lv_obj_check_type(value_object, &lv_label_class)
        ? value_object
        : nullptr;
}

CommandResult result(
    CommandError error,
    std::size_t command = 0,
    std::size_t byte = 0) {
    return {
        error,
        static_cast<std::uint16_t>(command),
        static_cast<std::uint16_t>(byte),
    };
}

}  // namespace

const char* CommandBatch::string_at(std::uint16_t offset) const {
    return offset < string_bytes ? &strings[offset] : "";
}

CommandResult decode_command_batch_canonical_cbor(
    const std::uint8_t* bytes,
    std::size_t size,
    CommandBatch& output) {
    output = {};
    if (bytes == nullptr || size == 0) return result(CommandError::empty);
    if (size > kMaximumCommandBatchBytes) {
        return result(CommandError::too_large);
    }
    Reader reader(bytes, size, output);
    std::size_t fields = 0;
    if (!reader.map(fields)) {
        return result(reader.error(), 0, reader.offset());
    }
    if (fields != 2) return result(CommandError::missing_field, 0, reader.offset());
    std::uint64_t previous = 0;
    bool has_previous = false;
    bool has_schema = false;
    bool has_commands = false;
    for (std::size_t field = 0; field < fields; ++field) {
        std::uint64_t key = 0;
        if (!reader.ordered_key(key, previous, has_previous)) {
            return result(reader.error(), 0, reader.offset());
        }
        if (key == 0) {
            std::uint64_t version = 0;
            if (!reader.unsigned_integer(version) || version != 1) {
                return result(CommandError::invalid_integer, 0, reader.offset());
            }
            output.schema_version = 1;
            has_schema = true;
        } else if (key == 1) {
            std::size_t count = 0;
            if (!reader.array(count)) {
                return result(reader.error(), 0, reader.offset());
            }
            if (count == 0) {
                return result(CommandError::empty, 0, reader.offset());
            }
            if (count > kMaximumCommands) {
                return result(CommandError::too_many_commands, 0, reader.offset());
            }
            for (std::size_t index = 0; index < count; ++index) {
                if (!parse_command(
                        reader, output, output.commands[index])) {
                    return result(reader.error(), index, reader.offset());
                }
                const auto command_domain =
                    domain_for(output.commands[index].kind);
                if (output.domain != CommandDomain::none &&
                    output.domain != command_domain) {
                    return result(
                        CommandError::mixed_domains, index, reader.offset());
                }
                output.domain = command_domain;
                ++output.command_count;
            }
            has_commands = true;
        } else {
            return result(CommandError::unexpected_key, 0, reader.offset());
        }
    }
    if (!has_schema || !has_commands) {
        return result(CommandError::missing_field, 0, reader.offset());
    }
    if (!reader.at_end()) {
        return result(CommandError::trailing_data, 0, reader.offset());
    }
    return result(CommandError::none);
}

CommandResult apply_ui_command_batch(
    const CommandBatch& batch,
    WireDocument& document) {
    if (batch.domain != CommandDomain::ui || batch.command_count == 0) {
        return result(CommandError::invalid_command);
    }
    constexpr std::uint16_t kNoReplacement = 0xffffU;
    std::array<std::uint16_t, Reconciler::kCapacity>
        primary_replacements{};
    std::array<std::uint16_t, Reconciler::kCapacity>
        secondary_replacements{};
    std::array<std::uint16_t, Reconciler::kCapacity>
        icon_replacements{};
    std::array<std::uint16_t, Reconciler::kCapacity>
        semantic_label_replacements{};
    std::array<std::uint16_t, Reconciler::kCapacity>
        semantic_value_replacements{};
    primary_replacements.fill(kNoReplacement);
    secondary_replacements.fill(kNoReplacement);
    icon_replacements.fill(kNoReplacement);
    semantic_label_replacements.fill(kNoReplacement);
    semantic_value_replacements.fill(kNoReplacement);
    bool has_string_replacement = false;
    std::array<std::int32_t, Reconciler::kCapacity> staged_values{};
    std::array<std::int32_t, Reconciler::kCapacity> staged_maxima{};
    std::array<bool, Reconciler::kCapacity> numeric_touched{};
    std::array<bool, Reconciler::kCapacity> staged_checked{};
    std::array<bool, Reconciler::kCapacity> checked_touched{};
    std::array<std::array<std::uint16_t, 13>, Reconciler::kCapacity>
        staged_samples{};
    std::array<std::uint8_t, Reconciler::kCapacity> staged_sample_counts{};
    std::array<bool, Reconciler::kCapacity> samples_touched{};
    for (std::size_t index = 0; index < document.node_count; ++index) {
        staged_values[index] = document.nodes[index].value;
        staged_maxima[index] = document.nodes[index].maximum;
        staged_checked[index] = document.nodes[index].value != 0;
        staged_samples[index] = document.nodes[index].samples;
        staged_sample_counts[index] = document.nodes[index].sample_count;
    }
    for (std::size_t index = 0; index < batch.command_count; ++index) {
        const auto& command = batch.commands[index];
        auto* node = find_node(
            document, batch.string_at(command.target_offset));
        if (node == nullptr || node->mounted_object == nullptr) {
            return result(CommandError::target_not_found, index);
        }
        if (command.kind == CommandKind::set_property) {
            if (command.property == PropertyKind::primary_text ||
                command.property == PropertyKind::secondary_text) {
                const auto canvas_display_list =
                    node->kind == ComponentKind::canvas &&
                    command.property == PropertyKind::primary_text;
                if (!supports_text_property(*node, command.property) ||
                    (!canvas_display_list &&
                     label_for(*node, command.property) == nullptr) ||
                    (canvas_display_list &&
                     !validate_canvas_display_list(
                         batch.string_at(command.text_offset),
                         document.string_at(node->secondary_text_offset),
                         node->value,
                         node->maximum))) {
                    return result(CommandError::unsupported_property, index);
                }
                const auto node_index =
                    static_cast<std::size_t>(
                        node - document.nodes.data());
                const auto* current_text = document.string_at(
                    command.property == PropertyKind::primary_text
                        ? node->primary_text_offset
                        : node->secondary_text_offset);
                if (std::strcmp(
                        current_text,
                        batch.string_at(command.text_offset)) != 0) {
                    if (command.property == PropertyKind::primary_text) {
                        primary_replacements[node_index] =
                            command.text_offset;
                    } else {
                        secondary_replacements[node_index] =
                            command.text_offset;
                    }
                } else if (
                    command.property == PropertyKind::primary_text) {
                    primary_replacements[node_index] = kNoReplacement;
                } else {
                    secondary_replacements[node_index] = kNoReplacement;
                }
            } else if (command.property == PropertyKind::icon) {
                generated::WeatherIcon icon{};
                if (node->kind != ComponentKind::icon ||
                    !weather_icon(batch.string_at(command.text_offset), icon)) {
                    return result(CommandError::unsupported_property, index);
                }
                const auto node_index = static_cast<std::size_t>(
                    node - document.nodes.data());
                if (std::strcmp(
                        document.string_at(node->icon_offset),
                        batch.string_at(command.text_offset)) != 0) {
                    icon_replacements[node_index] = command.text_offset;
                } else {
                    icon_replacements[node_index] = kNoReplacement;
                }
            } else if (
                command.property == PropertyKind::semantic_label ||
                command.property == PropertyKind::semantic_value) {
                const auto node_index = static_cast<std::size_t>(
                    node - document.nodes.data());
                const auto current_offset =
                    command.property == PropertyKind::semantic_label
                        ? node->semantic_label_offset
                        : node->semantic_value_offset;
                auto& replacement =
                    command.property == PropertyKind::semantic_label
                        ? semantic_label_replacements[node_index]
                        : semantic_value_replacements[node_index];
                if (std::strcmp(
                        document.string_at(current_offset),
                        batch.string_at(command.text_offset)) != 0) {
                    replacement = command.text_offset;
                } else {
                    replacement = kNoReplacement;
                }
            } else if (
                command.property == PropertyKind::value ||
                command.property == PropertyKind::maximum) {
                if ((node->kind != ComponentKind::progress &&
                     node->kind != ComponentKind::stepper &&
                     node->kind != ComponentKind::chart &&
                     node->kind != ComponentKind::pager) ||
                    (node->kind == ComponentKind::pager &&
                     command.property != PropertyKind::value) ||
                    command.integer_value <
                        std::numeric_limits<std::int32_t>::min() ||
                    command.integer_value >
                        std::numeric_limits<std::int32_t>::max()) {
                    return result(CommandError::unsupported_property, index);
                }
                if (node->kind == ComponentKind::stepper) {
                    auto* object =
                        static_cast<lv_obj_t*>(node->mounted_object);
                    if (stepper_value_label(object) == nullptr) {
                        return result(
                            CommandError::unsupported_property,
                            index);
                    }
                }
                const auto value = static_cast<std::int32_t>(
                    command.integer_value);
                if ((node->kind == ComponentKind::progress &&
                     ((command.property == PropertyKind::maximum &&
                       value < 1) ||
                      (command.property == PropertyKind::value &&
                       value < 0))) ||
                    (node->kind == ComponentKind::stepper &&
                     ((command.property == PropertyKind::maximum &&
                       value < node->minimum) ||
                      (command.property == PropertyKind::value &&
                       value < node->minimum))) ||
                    (node->kind == ComponentKind::pager &&
                     (value < 0 || value >= node->maximum))) {
                    return result(CommandError::value_out_of_range, index);
                }
                const auto node_index =
                    static_cast<std::size_t>(
                        node - document.nodes.data());
                numeric_touched[node_index] = true;
                if (command.property == PropertyKind::maximum) {
                    staged_maxima[node_index] = value;
                } else {
                    staged_values[node_index] = value;
                }
            } else if (command.property == PropertyKind::samples) {
                if (node->kind != ComponentKind::chart ||
                    command.sample_count < 1 || command.sample_count > 13) {
                    return result(CommandError::unsupported_property, index);
                }
                const auto node_index = static_cast<std::size_t>(
                    node - document.nodes.data());
                staged_samples[node_index] = command.samples;
                staged_sample_counts[node_index] = command.sample_count;
                samples_touched[node_index] = true;
            } else if (command.property == PropertyKind::checked) {
                if (node->kind != ComponentKind::toggle) {
                    return result(CommandError::unsupported_property, index);
                }
                const auto node_index =
                    static_cast<std::size_t>(
                        node - document.nodes.data());
                checked_touched[node_index] = true;
                staged_checked[node_index] = command.boolean_value;
            } else {
                return result(CommandError::unsupported_property, index);
            }
        } else if (
            command.kind != CommandKind::set_visibility &&
            command.kind != CommandKind::set_enabled) {
            return result(CommandError::invalid_command, index);
        }
    }
    for (std::size_t index = 0; index < document.node_count; ++index) {
        if (numeric_touched[index] &&
            (staged_maxima[index] < 1 ||
             staged_values[index] > staged_maxima[index] ||
             (document.nodes[index].kind == ComponentKind::stepper &&
              staged_values[index] <
              document.nodes[index].minimum))) {
            return result(CommandError::value_out_of_range, index);
        }
        if (samples_touched[index]) {
            for (std::size_t sample = 0;
                 sample < staged_sample_counts[index]; ++sample) {
                if (staged_samples[index][sample] >
                    static_cast<std::uint32_t>(staged_maxima[index])) {
                    return result(CommandError::value_out_of_range, index);
                }
            }
        }
        has_string_replacement =
            has_string_replacement ||
            primary_replacements[index] != kNoReplacement ||
            secondary_replacements[index] != kNoReplacement ||
            icon_replacements[index] != kNoReplacement ||
            semantic_label_replacements[index] != kNoReplacement ||
            semantic_value_replacements[index] != kNoReplacement;
    }
    std::array<char, kMaximumWireStrings>* old_strings = nullptr;
    if (has_string_replacement) {
        auto required_string_bytes = std::size_t{1};
        auto add_required = [&](const char* value) {
            if (value != nullptr && value[0] != '\0') {
                required_string_bytes += std::strlen(value) + 1;
            }
        };
        add_required(document.string_at(document.app_id_offset));
        for (std::size_t index = 0;
             index < document.node_count;
             ++index) {
            const auto& node = document.nodes[index];
            add_required(document.string_at(node.id_offset));
            add_required(
                primary_replacements[index] != kNoReplacement
                    ? batch.string_at(primary_replacements[index])
                    : document.string_at(node.primary_text_offset));
            add_required(
                secondary_replacements[index] != kNoReplacement
                    ? batch.string_at(secondary_replacements[index])
                    : document.string_at(node.secondary_text_offset));
            add_required(
                semantic_label_replacements[index] != kNoReplacement
                    ? batch.string_at(semantic_label_replacements[index])
                    : document.string_at(node.semantic_label_offset));
            add_required(
                semantic_value_replacements[index] != kNoReplacement
                    ? batch.string_at(semantic_value_replacements[index])
                    : document.string_at(node.semantic_value_offset));
            add_required(
                document.string_at(node.semantic_hint_offset));
            add_required(
                icon_replacements[index] != kNoReplacement
                    ? batch.string_at(icon_replacements[index])
                    : document.string_at(node.icon_offset));
        }
        for (std::size_t index = 0;
             index < document.key_count;
             ++index) {
            add_required(
                document.string_at(document.key_offsets[index]));
        }
        for (std::size_t index = 0;
             index < document.event_count;
             ++index) {
            add_required(document.string_at(
                document.events[index].action_id_offset));
        }
        if (required_string_bytes > document.strings.size()) {
            return result(CommandError::document_string_capacity);
        }
        old_strings =
            new (std::nothrow)
                std::array<char, kMaximumWireStrings>{};
        if (old_strings == nullptr) {
            return result(CommandError::allocation_failed);
        }
        *old_strings = document.strings;
        const auto old_string_bytes = document.string_bytes;
        auto old_string_at = [&](std::uint16_t offset) {
            return offset < old_string_bytes
                ? old_strings->data() + offset
                : "";
        };
        document.string_bytes = 1;
        document.strings[0] = '\0';
        auto compact_copy = [&](const char* value) {
            if (value == nullptr || value[0] == '\0') {
                return std::uint16_t{0};
            }
            const auto offset =
                static_cast<std::uint16_t>(
                    document.string_bytes);
            const auto length = std::strlen(value);
            std::memcpy(
                document.strings.data() + document.string_bytes,
                value,
                length + 1);
            document.string_bytes += length + 1;
            return offset;
        };
        document.app_id_offset = compact_copy(
            old_string_at(document.app_id_offset));
        for (std::size_t index = 0;
             index < document.node_count;
             ++index) {
            auto& node = document.nodes[index];
            const auto old_id = node.id_offset;
            const auto old_primary = node.primary_text_offset;
            const auto old_secondary = node.secondary_text_offset;
            const auto old_semantic = node.semantic_label_offset;
            const auto old_semantic_value = node.semantic_value_offset;
            const auto old_semantic_hint = node.semantic_hint_offset;
            const auto old_icon = node.icon_offset;
            node.id_offset = compact_copy(old_string_at(old_id));
            node.primary_text_offset = compact_copy(
                primary_replacements[index] != kNoReplacement
                    ? batch.string_at(primary_replacements[index])
                    : old_string_at(old_primary));
            node.secondary_text_offset = compact_copy(
                secondary_replacements[index] != kNoReplacement
                    ? batch.string_at(secondary_replacements[index])
                    : old_string_at(old_secondary));
            node.semantic_label_offset = compact_copy(
                semantic_label_replacements[index] != kNoReplacement
                    ? batch.string_at(semantic_label_replacements[index])
                    : old_string_at(old_semantic));
            node.semantic_value_offset = compact_copy(
                semantic_value_replacements[index] != kNoReplacement
                    ? batch.string_at(semantic_value_replacements[index])
                    : old_string_at(old_semantic_value));
            node.semantic_hint_offset = compact_copy(
                old_string_at(old_semantic_hint));
            node.icon_offset = compact_copy(
                icon_replacements[index] != kNoReplacement
                    ? batch.string_at(icon_replacements[index])
                    : old_string_at(old_icon));
        }
        for (std::size_t index = 0;
             index < document.key_count;
             ++index) {
            document.key_offsets[index] = compact_copy(
                old_string_at(document.key_offsets[index]));
        }
        for (std::size_t index = 0;
             index < document.event_count;
             ++index) {
            document.events[index].action_id_offset = compact_copy(
                old_string_at(
                    document.events[index].action_id_offset));
        }
        // Mounted AppSpec objects keep their stable node id in user data for
        // semantic inspection. String compaction can move every id, so repair
        // those borrowed pointers before releasing the old snapshot.
        for (std::size_t index = 0;
             index < document.node_count;
             ++index) {
            auto& node = document.nodes[index];
            if (node.mounted_object != nullptr) {
                lv_obj_set_user_data(
                    static_cast<lv_obj_t*>(node.mounted_object),
                    const_cast<char*>(
                        document.string_at(node.id_offset)));
            }
        }
        delete old_strings;
        old_strings = nullptr;
    }

    for (std::size_t index = 0; index < batch.command_count; ++index) {
        const auto& command = batch.commands[index];
        auto* node = find_node(
            document, batch.string_at(command.target_offset));
        auto* object = static_cast<lv_obj_t*>(node->mounted_object);
        if (command.kind == CommandKind::set_visibility) {
            node->visible = command.boolean_value;
            if (command.boolean_value) {
                lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (command.kind == CommandKind::set_enabled) {
            node->enabled = command.boolean_value;
            if (command.boolean_value) {
                lv_obj_remove_state(object, LV_STATE_DISABLED);
            } else {
                lv_obj_add_state(object, LV_STATE_DISABLED);
            }
        }
    }
    if (has_string_replacement) {
        for (std::size_t index = 0;
             index < document.node_count;
             ++index) {
            auto& node = document.nodes[index];
            if (primary_replacements[index] != kNoReplacement) {
                if (node.kind == ComponentKind::canvas) {
                    if (!render_canvas_display_list(
                            static_cast<lv_obj_t*>(
                                node.mounted_object),
                            document.string_at(
                                node.primary_text_offset),
                            document.string_at(
                                node.secondary_text_offset),
                            node.value,
                            node.maximum)) {
                        return result(
                            CommandError::unsupported_property,
                            index);
                    }
                } else {
                    lv_label_set_text(
                        label_for(node, PropertyKind::primary_text),
                        document.string_at(node.primary_text_offset));
                }
            }
            if (secondary_replacements[index] != kNoReplacement) {
                lv_label_set_text(
                    label_for(node, PropertyKind::secondary_text),
                    document.string_at(node.secondary_text_offset));
            }
            if (icon_replacements[index] != kNoReplacement &&
                !apply_weather_icon(
                    static_cast<lv_obj_t*>(node.mounted_object),
                    node,
                    document.string_at(node.icon_offset))) {
                return result(
                    CommandError::unsupported_property,
                    index);
            }
        }
    }
    for (std::size_t index = 0; index < document.node_count; ++index) {
        if (!numeric_touched[index]) continue;
        auto& node = document.nodes[index];
        auto* object = static_cast<lv_obj_t*>(node.mounted_object);
        node.maximum = staged_maxima[index];
        node.value = staged_values[index];
        if (node.kind == ComponentKind::stepper) {
            char value[40]{};
            std::snprintf(
                value,
                sizeof(value),
                "%ld %s",
                static_cast<long>(node.value),
                document.string_at(node.secondary_text_offset));
            auto* value_label = stepper_value_label(object);
            auto* value_object = lv_obj_get_child(object, 1);
            if (value_label == value_object) {
                lv_label_set_text(value_label, value);
            } else if (value_label != nullptr) {
                if (lv_obj_check_type(
                        value_object, &lv_label_class)) {
                    lv_label_set_text(value_object, value);
                }
                char number[24]{};
                std::snprintf(
                    number,
                    sizeof(number),
                    "%ld",
                    static_cast<long>(node.value));
                lv_label_set_text(value_label, number);
            }
        } else if (node.kind == ComponentKind::chart) {
            lv_chart_set_range(
                object, LV_CHART_AXIS_PRIMARY_Y, 0, node.maximum);
        } else if (node.kind == ComponentKind::pager) {
            for (std::size_t page_index = index + 1;
                 page_index < document.node_count; ++page_index) {
                auto& page = document.nodes[page_index];
                if (page.parent_index != index ||
                    page.mounted_object == nullptr) {
                    continue;
                }
                std::size_t ordinal = 0;
                for (std::size_t previous = index + 1;
                     previous < page_index; ++previous) {
                    if (document.nodes[previous].parent_index == index) {
                        ++ordinal;
                    }
                }
                auto* page_object =
                    static_cast<lv_obj_t*>(page.mounted_object);
                if (ordinal == static_cast<std::size_t>(node.value)) {
                    lv_obj_remove_flag(page_object, LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(page_object, LV_OBJ_FLAG_HIDDEN);
                }
            }
            if (node.checked && lv_obj_get_child_count(object) > 0) {
                auto* indicator = lv_obj_get_child(object, 0);
                if (indicator != nullptr &&
                    lv_obj_get_child_count(indicator) ==
                        static_cast<std::uint32_t>(node.maximum)) {
                    using generated::WeatherColorRole;
                    for (std::int32_t page = 0;
                         page < node.maximum; ++page) {
                        auto* dot = lv_obj_get_child(indicator, page);
                        const auto active = page == node.value;
                        const auto dot_size = active ? 6 : 4;
                        lv_obj_set_size(dot, dot_size, dot_size);
                        const auto role = active
                            ? WeatherColorRole::primary
                            : WeatherColorRole::outline_variant;
                        const auto& color = generated::kWeatherColors[
                            static_cast<std::size_t>(role)].rgb888;
                        lv_obj_set_style_bg_color(
                            dot,
                            lv_color_make(
                                color.red, color.green, color.blue),
                            0);
                    }
                }
            }
        } else if (node.variant == 1) {
            lv_arc_set_range(object, 0, node.maximum);
            lv_arc_set_value(object, node.value);
        } else {
            lv_bar_set_range(object, 0, node.maximum);
            lv_bar_set_value(object, node.value, LV_ANIM_OFF);
        }
    }
    for (std::size_t index = 0; index < document.node_count; ++index) {
        if (!samples_touched[index]) continue;
        auto& node = document.nodes[index];
        auto* object = static_cast<lv_obj_t*>(node.mounted_object);
        node.samples = staged_samples[index];
        node.sample_count = staged_sample_counts[index];
        lv_chart_set_point_count(object, node.sample_count);
        auto* series = lv_chart_get_series_next(object, nullptr);
        if (series == nullptr) {
            return result(CommandError::unsupported_property, index);
        }
        lv_chart_set_all_values(object, series, LV_CHART_POINT_NONE);
        for (std::size_t sample = 0; sample < node.sample_count; ++sample) {
            lv_chart_set_next_value(object, series, node.samples[sample]);
        }
        if (std::strcmp(
                document.string_at(node.id_offset),
                "weather.rain-bars") == 0 &&
            lv_obj_get_child_count(object) >= node.sample_count) {
            for (std::size_t sample = 0; sample < node.sample_count; ++sample) {
                auto* bar = lv_obj_get_child(object, sample);
                const auto height_dp = std::max<std::int32_t>(
                    2,
                    static_cast<std::int32_t>(node.samples[sample]) * 35 /
                        std::max<std::int32_t>(1, node.maximum));
                const auto height_px = (height_dp * 5 + 2) / 4;
                const auto y_px = ((35 - height_dp) * 5 + 2) / 4;
                lv_obj_set_height(bar, height_px);
                lv_obj_set_y(bar, y_px);
            }
        }
        lv_chart_refresh(object);
    }
    for (std::size_t index = 0; index < document.node_count; ++index) {
        if (!checked_touched[index]) continue;
        auto& node = document.nodes[index];
        auto* object = static_cast<lv_obj_t*>(node.mounted_object);
        node.value = staged_checked[index] ? 1 : 0;
        if (staged_checked[index]) {
            lv_obj_add_state(object, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(object, LV_STATE_CHECKED);
        }
        if (lv_obj_get_child_count(object) < 2) continue;
        auto* indicator = lv_obj_get_child(object, 1);
        if (indicator == nullptr) continue;
        if (staged_checked[index]) {
            lv_obj_add_state(indicator, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(indicator, LV_STATE_CHECKED);
        }
        if (lv_obj_get_child_count(indicator) == 0) continue;
        auto* mark = lv_obj_get_child(indicator, 0);
        if (lv_obj_check_type(mark, &lv_label_class)) {
            lv_label_set_text(
                mark, staged_checked[index] ? LV_SYMBOL_OK : "");
        } else {
            lv_obj_align(
                mark,
                staged_checked[index]
                    ? LV_ALIGN_RIGHT_MID
                    : LV_ALIGN_LEFT_MID,
                staged_checked[index] ? -3 : 3,
                0);
        }
    }
    return result(CommandError::none);
}

CommandResult apply_state_command_batch(
    const CommandBatch& batch,
    state::Store& store,
    const state::Permission* permissions,
    std::size_t permission_count) {
    if (batch.domain != CommandDomain::state ||
        batch.command_count == 0 ||
        batch.command_count > state::Store::kMaxTransactionOperations) {
        return result(CommandError::invalid_command);
    }
    std::array<state::Operation, state::Store::kMaxTransactionOperations>
        operations{};
    for (std::size_t index = 0; index < batch.command_count; ++index) {
        const auto& command = batch.commands[index];
        auto& operation = operations[index];
        operation.path = batch.string_at(command.target_offset);
        if (command.kind == CommandKind::state_remove) {
            operation.kind = state::OperationKind::remove;
            continue;
        }
        if (command.kind != CommandKind::state_put) {
            return result(CommandError::invalid_command, index);
        }
        operation.kind = state::OperationKind::put;
        switch (command.value_type) {
            case state::ValueType::boolean:
                operation.value = state::Value::boolean(
                    command.boolean_value);
                break;
            case state::ValueType::integer:
                operation.value = state::Value::integer(
                    command.integer_value);
                break;
            case state::ValueType::fixed_q16_16:
                if (command.integer_value <
                        std::numeric_limits<std::int32_t>::min() ||
                    command.integer_value >
                        std::numeric_limits<std::int32_t>::max()) {
                    return result(CommandError::value_out_of_range, index);
                }
                operation.value = state::Value::fixed_q16_16(
                    static_cast<std::int32_t>(command.integer_value));
                break;
            case state::ValueType::string:
                operation.value = state::Value::string(
                    batch.string_at(command.text_offset));
                break;
        }
    }
    if (!store.apply(
            operations.data(),
            batch.command_count,
            permissions,
            permission_count)) {
        return result(CommandError::state_transaction_rejected);
    }
    return result(CommandError::none);
}

const char* command_error_name(CommandError error) {
    switch (error) {
        case CommandError::none: return "none";
        case CommandError::empty: return "empty";
        case CommandError::too_large: return "too_large";
        case CommandError::truncated: return "truncated";
        case CommandError::non_canonical: return "non_canonical";
        case CommandError::unsupported_type: return "unsupported_type";
        case CommandError::unexpected_key: return "unexpected_key";
        case CommandError::missing_field: return "missing_field";
        case CommandError::invalid_integer: return "invalid_integer";
        case CommandError::invalid_boolean: return "invalid_boolean";
        case CommandError::invalid_text: return "invalid_text";
        case CommandError::invalid_identifier: return "invalid_identifier";
        case CommandError::invalid_path: return "invalid_path";
        case CommandError::string_capacity: return "string_capacity";
        case CommandError::too_many_commands: return "too_many_commands";
        case CommandError::mixed_domains: return "mixed_domains";
        case CommandError::invalid_command: return "invalid_command";
        case CommandError::trailing_data: return "trailing_data";
        case CommandError::target_not_found: return "target_not_found";
        case CommandError::unsupported_property: return "unsupported_property";
        case CommandError::value_out_of_range: return "value_out_of_range";
        case CommandError::document_string_capacity:
            return "document_string_capacity";
        case CommandError::allocation_failed: return "allocation_failed";
        case CommandError::state_transaction_rejected:
            return "state_transaction_rejected";
    }
    return "unknown";
}

}  // namespace m3e::appspec
