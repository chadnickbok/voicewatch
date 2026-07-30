#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/appspec/wire.hpp"
#include "m3e/state/store.hpp"

namespace m3e::appspec {

constexpr std::size_t kMaximumCommandBatchBytes = 4096;
constexpr std::size_t kMaximumCommands = 64;
constexpr std::size_t kMaximumCommandStrings = 4096;

enum class CommandKind : std::uint8_t {
    set_property,
    set_visibility,
    set_enabled,
    state_put,
    state_remove,
};

enum class PropertyKind : std::uint8_t {
    primary_text,
    secondary_text,
    value,
    maximum,
};

enum class CommandDomain : std::uint8_t {
    none,
    ui,
    state,
};

struct Command {
    CommandKind kind;
    PropertyKind property;
    state::ValueType value_type;
    std::uint16_t target_offset;
    std::uint16_t text_offset;
    std::int64_t integer_value;
    bool boolean_value;
};

struct CommandBatch {
    std::array<Command, kMaximumCommands> commands{};
    std::array<char, kMaximumCommandStrings> strings{};
    std::size_t command_count = 0;
    std::size_t string_bytes = 1;
    std::uint32_t schema_version = 0;
    CommandDomain domain = CommandDomain::none;

    const char* string_at(std::uint16_t offset) const;
};

enum class CommandError : std::uint8_t {
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
    invalid_identifier,
    invalid_path,
    string_capacity,
    too_many_commands,
    mixed_domains,
    invalid_command,
    trailing_data,
    target_not_found,
    unsupported_property,
    value_out_of_range,
    document_string_capacity,
    allocation_failed,
    state_transaction_rejected,
};

struct CommandResult {
    CommandError error;
    std::uint16_t command_index;
    std::uint16_t byte_offset;

    constexpr bool ok() const { return error == CommandError::none; }
};

CommandResult decode_command_batch_canonical_cbor(
    const std::uint8_t* bytes,
    std::size_t size,
    CommandBatch& output);

CommandResult apply_ui_command_batch(
    const CommandBatch& batch,
    WireDocument& document);

CommandResult apply_state_command_batch(
    const CommandBatch& batch,
    state::Store& store,
    const state::Permission* permissions = nullptr,
    std::size_t permission_count = 0);

const char* command_error_name(CommandError error);

}  // namespace m3e::appspec
