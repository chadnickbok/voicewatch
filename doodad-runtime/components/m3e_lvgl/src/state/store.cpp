#include "m3e/state/store.hpp"

#include <cstdio>
#include <cstring>

namespace m3e::state {
namespace {

bool copy_bounded(char* output, std::size_t capacity, const char* input) {
    if (output == nullptr || capacity == 0 || input == nullptr) {
        return false;
    }
    const auto length = std::strlen(input);
    if (length >= capacity) {
        return false;
    }
    std::memcpy(output, input, length + 1);
    return true;
}

std::int32_t find_entry(
    const std::array<Entry, Store::kCapacity>& entries,
    std::size_t size,
    const char* path) {
    for (std::size_t index = 0; index < size; ++index) {
        if (std::strcmp(entries[index].path.data(), path) == 0) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool prefix_matches(const char* path, const char* prefix) {
    if (path == nullptr || prefix == nullptr) return false;
    const auto length = std::strlen(prefix);
    return std::strncmp(path, prefix, length) == 0 &&
        (path[length] == '\0' || path[length] == '.');
}

bool may_write(
    const char* path,
    const Permission* permissions,
    std::size_t permission_count) {
    const auto name_space = namespace_of(path);
    if (name_space == Namespace::system ||
        name_space == Namespace::invalid) {
        return false;
    }
    if (name_space != Namespace::shared) {
        return true;
    }
    for (std::size_t index = 0; index < permission_count; ++index) {
        if (permissions[index].can_write &&
            prefix_matches(path, permissions[index].prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace

Value Value::boolean(bool value) {
    return Value{ValueType::boolean, value, 0, {}};
}

Value Value::integer(std::int64_t value) {
    return Value{ValueType::integer, false, value, {}};
}

Value Value::fixed_q16_16(std::int32_t value) {
    return Value{ValueType::fixed_q16_16, false, value, {}};
}

Value Value::string(const char* value) {
    Value result{ValueType::string, false, 0, {}};
    copy_bounded(
        result.string_value.data(), result.string_value.size(), value);
    return result;
}

bool Value::equals(const Value& other) const {
    if (type != other.type) return false;
    switch (type) {
        case ValueType::boolean:
            return boolean_value == other.boolean_value;
        case ValueType::integer:
        case ValueType::fixed_q16_16:
            return integer_value == other.integer_value;
        case ValueType::string:
            return std::strcmp(
                string_value.data(), other.string_value.data()) == 0;
    }
    return false;
}

Namespace namespace_of(const char* path) {
    if (path == nullptr) return Namespace::invalid;
    struct Mapping {
        const char* prefix;
        Namespace value;
    };
    constexpr Mapping mappings[] = {
        {"screen.", Namespace::screen},
        {"app.", Namespace::app},
        {"shared.", Namespace::shared},
        {"system.", Namespace::system},
        {"session.", Namespace::session},
    };
    for (const auto& mapping : mappings) {
        if (std::strncmp(
                path, mapping.prefix, std::strlen(mapping.prefix)) == 0) {
            return mapping.value;
        }
    }
    return Namespace::invalid;
}

bool valid_path(const char* path) {
    if (namespace_of(path) == Namespace::invalid) return false;
    const auto length = std::strlen(path);
    if (length == 0 || length > 96 || path[length - 1] == '.') return false;
    bool previous_dot = false;
    for (std::size_t index = 0; index < length; ++index) {
        const char value = path[index];
        const bool dot = value == '.';
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '-'
              || dot) ||
            (dot && previous_dot)) {
            return false;
        }
        previous_dot = dot;
    }
    return true;
}

bool Store::apply(
    const Operation* operations,
    std::size_t count,
    const Permission* permissions,
    std::size_t permission_count) {
    if ((operations == nullptr && count != 0) ||
        count > kMaxTransactionOperations ||
        (permissions == nullptr && permission_count != 0)) {
        return false;
    }
    if (count == 0) return true;
    auto staged = entries_;
    auto staged_size = size_;
    for (std::size_t operation_index = 0;
         operation_index < count;
         ++operation_index) {
        const auto& operation = operations[operation_index];
        if (!valid_path(operation.path) ||
            !may_write(operation.path, permissions, permission_count)) {
            return false;
        }
        const auto found =
            find_entry(staged, staged_size, operation.path);
        if (operation.kind == OperationKind::put) {
            std::size_t index;
            if (found < 0) {
                if (staged_size >= kCapacity) return false;
                index = staged_size++;
                if (!copy_bounded(
                        staged[index].path.data(),
                        staged[index].path.size(),
                        operation.path)) {
                    return false;
                }
            } else {
                index = static_cast<std::size_t>(found);
            }
            staged[index].value = operation.value;
        } else {
            if (found < 0) return false;
            for (std::size_t index = static_cast<std::size_t>(found);
                 index + 1 < staged_size;
                 ++index) {
                staged[index] = staged[index + 1];
            }
            --staged_size;
        }
    }
    const auto next_revision = revision_ + 1;
    for (std::size_t index = 0; index < staged_size; ++index) {
        if (index >= size_ ||
            !staged[index].value.equals(entries_[index].value) ||
            std::strcmp(
                staged[index].path.data(), entries_[index].path.data()) != 0) {
            staged[index].revision = next_revision;
        }
    }
    entries_ = staged;
    size_ = staged_size;
    revision_ = next_revision;
    return true;
}

const Entry* Store::get(const char* path) const {
    if (!valid_path(path)) return nullptr;
    const auto index = find_entry(entries_, size_, path);
    return index < 0 ? nullptr : &entries_[static_cast<std::size_t>(index)];
}

std::size_t Store::size() const {
    return size_;
}

std::uint32_t Store::revision() const {
    return revision_;
}

bool format_value(
    const Value& value,
    const char* unit,
    char* output,
    std::size_t output_size) {
    if (output == nullptr || output_size == 0) return false;
    const char* safe_unit = unit == nullptr ? "" : unit;
    int written = -1;
    switch (value.type) {
        case ValueType::boolean:
            written = std::snprintf(
                output, output_size, "%s",
                value.boolean_value ? "On" : "Off");
            break;
        case ValueType::integer:
            written = std::snprintf(
                output, output_size, "%lld%s%s",
                static_cast<long long>(value.integer_value),
                safe_unit[0] == '\0' ? "" : " ",
                safe_unit);
            break;
        case ValueType::fixed_q16_16: {
            const auto raw = static_cast<std::int32_t>(value.integer_value);
            const auto whole = raw / 65536;
            const auto fraction =
                static_cast<unsigned>(
                    (raw < 0 ? -raw : raw) % 65536 * 100 / 65536);
            written = std::snprintf(
                output, output_size, "%ld.%02u%s%s",
                static_cast<long>(whole), fraction,
                safe_unit[0] == '\0' ? "" : " ", safe_unit);
            break;
        }
        case ValueType::string:
            written = std::snprintf(
                output, output_size, "%s", value.string_value.data());
            break;
    }
    return written >= 0 &&
        static_cast<std::size_t>(written) < output_size;
}

}  // namespace m3e::state
