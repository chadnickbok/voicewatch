#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "m3e/appspec/runtime.hpp"
#include "m3e/state/store.hpp"

namespace m3e::state {

enum class BindingTarget : std::uint8_t {
    properties,
    visible,
    enabled,
};

enum class BindingPredicate : std::uint8_t {
    value,
    exists,
    equals_integer,
    not_equals_integer,
    less_than_integer,
    greater_than_integer,
};

enum class BindingFormat : std::uint8_t {
    raw,
    number_with_unit,
    duration_seconds,
};

struct BindingSpec {
    const char* node_id;
    const char* state_path;
    BindingTarget target;
    BindingPredicate predicate;
    std::int64_t predicate_operand;
    BindingFormat format;
    const char* unit;
};

enum class BindingError : std::uint8_t {
    none,
    invalid_arguments,
    too_many_bindings,
    invalid_node,
    invalid_path,
    duplicate_target,
    invalid_format,
    missing_state,
    type_mismatch,
    formatting_failed,
    reconcile_failed,
};

struct BindingResult {
    BindingError error;
    std::uint16_t binding_index;
    std::uint16_t patch_count;

    constexpr bool ok() const { return error == BindingError::none; }
};

class BindingHub {
 public:
    // Matches the reconciler's atomic patch ceiling. A screen that needs more
    // reactive properties should split them across routes or reusable native
    // components instead of creating an unbounded observer graph.
    static constexpr std::size_t kCapacity = 64;

    BindingResult mount(
        const BindingSpec* bindings,
        std::size_t count,
        const appspec::Reconciler& reconciler);
    BindingResult sync(
        const Store& store,
        appspec::Reconciler& reconciler);
    void clear();

    std::size_t size() const;
    std::uint32_t observed_store_revision() const;
    const char* rendered_value(
        const char* node_id,
        BindingTarget target) const;

 private:
    struct OwnedBinding {
        std::array<char, 65> node_id{};
        std::array<char, 97> state_path{};
        BindingTarget target = BindingTarget::properties;
        BindingPredicate predicate = BindingPredicate::value;
        std::int64_t predicate_operand = 0;
        BindingFormat format = BindingFormat::raw;
        std::array<char, 17> unit{};
        std::array<char, 97> rendered{};
        std::uint64_t signature = 0;
    };

    std::array<OwnedBinding, kCapacity> bindings_{};
    std::size_t size_ = 0;
    std::uint32_t observed_store_revision_ = 0;
    bool has_synced_ = false;
};

}  // namespace m3e::state
