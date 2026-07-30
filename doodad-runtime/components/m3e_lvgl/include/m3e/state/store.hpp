#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e::state {

enum class ValueType : std::uint8_t {
    boolean,
    integer,
    fixed_q16_16,
    string,
};

struct Value {
    ValueType type;
    bool boolean_value;
    std::int64_t integer_value;
    std::array<char, 65> string_value;

    static Value boolean(bool value);
    static Value integer(std::int64_t value);
    static Value fixed_q16_16(std::int32_t value);
    static Value string(const char* value);
    bool equals(const Value& other) const;
};

enum class Namespace : std::uint8_t {
    screen,
    app,
    shared,
    system,
    session,
    invalid,
};

Namespace namespace_of(const char* path);
bool valid_path(const char* path);

struct Permission {
    const char* prefix;
    bool can_read;
    bool can_write;
};

enum class OperationKind : std::uint8_t {
    put,
    remove,
};

struct Operation {
    OperationKind kind;
    const char* path;
    Value value;
};

struct Entry {
    std::array<char, 97> path;
    Value value;
    std::uint32_t revision;
};

class Store {
 public:
    static constexpr std::size_t kCapacity = 128;
    static constexpr std::size_t kMaxTransactionOperations = 32;

    bool apply(
        const Operation* operations,
        std::size_t count,
        const Permission* permissions = nullptr,
        std::size_t permission_count = 0);
    const Entry* get(const char* path) const;
    std::size_t size() const;
    std::uint32_t revision() const;

 private:
    std::array<Entry, kCapacity> entries_{};
    std::size_t size_ = 0;
    std::uint32_t revision_ = 0;
};

bool format_value(
    const Value& value,
    const char* unit,
    char* output,
    std::size_t output_size);

}  // namespace m3e::state
