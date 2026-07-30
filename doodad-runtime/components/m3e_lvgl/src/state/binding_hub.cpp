#include "m3e/state/binding_hub.hpp"

#include <cstdio>
#include <cstring>

namespace m3e::state {
namespace {

bool copy_bounded(char* output, std::size_t capacity, const char* input) {
    if (output == nullptr || capacity == 0 || input == nullptr) {
        return false;
    }
    const auto length = std::strlen(input);
    if (length >= capacity) return false;
    std::memcpy(output, input, length + 1);
    return true;
}

bool fits_bounded(const char* input, std::size_t capacity) {
    return input != nullptr && std::strlen(input) < capacity;
}

std::uint64_t hash_text(const char* text) {
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    auto result = kOffset;
    if (text == nullptr) return result;
    for (std::size_t index = 0; text[index] != '\0'; ++index) {
        result ^= static_cast<std::uint8_t>(text[index]);
        result *= kPrime;
    }
    return result;
}

bool predicate_value(
    BindingPredicate predicate,
    std::int64_t predicate_operand,
    const Entry* entry,
    bool& output,
    BindingError& error) {
    if (predicate == BindingPredicate::exists) {
        output = entry != nullptr;
        return true;
    }
    if (entry == nullptr) {
        error = BindingError::missing_state;
        return false;
    }
    const auto& value = entry->value;
    switch (predicate) {
        case BindingPredicate::value:
            if (value.type != ValueType::boolean) {
                error = BindingError::type_mismatch;
                return false;
            }
            output = value.boolean_value;
            return true;
        case BindingPredicate::equals_integer:
        case BindingPredicate::not_equals_integer:
        case BindingPredicate::less_than_integer:
        case BindingPredicate::greater_than_integer:
            if (value.type != ValueType::integer &&
                value.type != ValueType::fixed_q16_16) {
                error = BindingError::type_mismatch;
                return false;
            }
            break;
        case BindingPredicate::exists:
            return true;
    }
    switch (predicate) {
        case BindingPredicate::equals_integer:
            output = value.integer_value == predicate_operand;
            break;
        case BindingPredicate::not_equals_integer:
            output = value.integer_value != predicate_operand;
            break;
        case BindingPredicate::less_than_integer:
            output = value.integer_value < predicate_operand;
            break;
        case BindingPredicate::greater_than_integer:
            output = value.integer_value > predicate_operand;
            break;
        default:
            error = BindingError::type_mismatch;
            return false;
    }
    return true;
}

bool render_value(
    BindingFormat format,
    const char* unit,
    const Entry* entry,
    char* output,
    std::size_t output_size,
    BindingError& error) {
    if (entry == nullptr) {
        error = BindingError::missing_state;
        return false;
    }
    switch (format) {
        case BindingFormat::raw:
            if (!format_value(entry->value, nullptr, output, output_size)) {
                error = BindingError::formatting_failed;
                return false;
            }
            return true;
        case BindingFormat::number_with_unit:
            if (entry->value.type != ValueType::integer &&
                entry->value.type != ValueType::fixed_q16_16) {
                error = BindingError::type_mismatch;
                return false;
            }
            if (!format_value(
                    entry->value,
                    unit,
                    output,
                    output_size)) {
                error = BindingError::formatting_failed;
                return false;
            }
            return true;
        case BindingFormat::duration_seconds: {
            if (entry->value.type != ValueType::integer ||
                entry->value.integer_value < 0) {
                error = BindingError::type_mismatch;
                return false;
            }
            const auto seconds = entry->value.integer_value;
            const auto written = std::snprintf(
                output,
                output_size,
                "%lld:%02lld",
                static_cast<long long>(seconds / 60),
                static_cast<long long>(seconds % 60));
            if (written < 0 ||
                static_cast<std::size_t>(written) >= output_size) {
                error = BindingError::formatting_failed;
                return false;
            }
            return true;
        }
    }
    error = BindingError::invalid_format;
    return false;
}

}  // namespace

BindingResult BindingHub::mount(
    const BindingSpec* bindings,
    std::size_t count,
    const appspec::Reconciler& reconciler) {
    if ((bindings == nullptr && count != 0)) {
        return {BindingError::invalid_arguments, 0, 0};
    }
    if (count > kCapacity) {
        return {BindingError::too_many_bindings, 0, 0};
    }

    for (std::size_t index = 0; index < count; ++index) {
        const auto& source = bindings[index];
        if (reconciler.find(source.node_id) == nullptr ||
            !fits_bounded(source.node_id, 65)) {
            return {
                BindingError::invalid_node,
                static_cast<std::uint16_t>(index),
                0};
        }
        if (!valid_path(source.state_path) ||
            !fits_bounded(source.state_path, 97)) {
            return {
                BindingError::invalid_path,
                static_cast<std::uint16_t>(index),
                0};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (bindings[previous].target == source.target &&
                std::strcmp(bindings[previous].node_id, source.node_id) == 0) {
                return {
                    BindingError::duplicate_target,
                    static_cast<std::uint16_t>(index),
                    0};
            }
        }
        if (source.target != BindingTarget::properties &&
            source.format != BindingFormat::raw) {
            return {
                BindingError::invalid_format,
                static_cast<std::uint16_t>(index),
                0};
        }
        if (!fits_bounded(source.unit == nullptr ? "" : source.unit, 17)) {
            return {
                BindingError::invalid_format,
                static_cast<std::uint16_t>(index),
                0};
        }
    }

    bindings_ = {};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& source = bindings[index];
        auto& destination = bindings_[index];
        copy_bounded(
            destination.node_id.data(),
            destination.node_id.size(),
            source.node_id);
        copy_bounded(
            destination.state_path.data(),
            destination.state_path.size(),
            source.state_path);
        destination.target = source.target;
        destination.predicate = source.predicate;
        destination.predicate_operand = source.predicate_operand;
        destination.format = source.format;
        copy_bounded(
            destination.unit.data(),
            destination.unit.size(),
            source.unit == nullptr ? "" : source.unit);
    }
    size_ = count;
    observed_store_revision_ = 0;
    has_synced_ = false;
    return {BindingError::none, 0, 0};
}

BindingResult BindingHub::sync(
    const Store& store,
    appspec::Reconciler& reconciler) {
    if (has_synced_ && observed_store_revision_ == store.revision()) {
        return {BindingError::none, 0, 0};
    }

    std::array<appspec::Patch, kCapacity> patches{};
    std::size_t patch_count = 0;

    for (std::size_t index = 0; index < size_; ++index) {
        const auto& binding = bindings_[index];
        if (reconciler.find(binding.node_id.data()) == nullptr) {
            return {
                BindingError::invalid_node,
                static_cast<std::uint16_t>(index),
                0};
        }
        const auto* entry = store.get(binding.state_path.data());
        BindingError error = BindingError::none;
        bool boolean_output = false;
        std::uint64_t signature = 0;
        std::array<char, 97> rendered{};

        if (binding.target == BindingTarget::properties) {
            if (binding.predicate != BindingPredicate::value) {
                if (!predicate_value(
                        binding.predicate,
                        binding.predicate_operand,
                        entry,
                        boolean_output,
                        error)) {
                    return {
                        error,
                        static_cast<std::uint16_t>(index),
                        0};
                }
                const char* rendered_text =
                    boolean_output ? "true" : "false";
                copy_bounded(
                    rendered.data(), rendered.size(), rendered_text);
            } else if (!render_value(
                           binding.format,
                           binding.unit.data(),
                           entry,
                           rendered.data(),
                           rendered.size(),
                           error)) {
                return {
                    error,
                    static_cast<std::uint16_t>(index),
                    0};
            }
            signature = hash_text(rendered.data());
        } else {
            if (!predicate_value(
                    binding.predicate,
                    binding.predicate_operand,
                    entry,
                    boolean_output,
                    error)) {
                return {
                    error,
                    static_cast<std::uint16_t>(index),
                    0};
            }
            signature = boolean_output ? 1U : 0U;
            copy_bounded(
                rendered.data(),
                rendered.size(),
                boolean_output ? "true" : "false");
        }

        if (has_synced_ && signature == binding.signature) continue;
        auto& patch = patches[patch_count++];
        patch.node_id = binding.node_id.data();
        patch.parent_id = nullptr;
        patch.component_kind = appspec::ComponentKind::text;
        patch.semantic_label = nullptr;
        patch.boolean_value = boolean_output;
        if (binding.target == BindingTarget::properties) {
            patch.kind = appspec::PatchKind::set_properties;
            patch.props_hash = signature;
        } else if (binding.target == BindingTarget::visible) {
            patch.kind = appspec::PatchKind::set_visibility;
        } else {
            patch.kind = appspec::PatchKind::set_enabled;
        }
    }

    if (patch_count != 0 &&
        !reconciler.apply_transaction(patches.data(), patch_count)) {
        return {BindingError::reconcile_failed, 0, 0};
    }
    for (std::size_t index = 0; index < size_; ++index) {
        auto& binding = bindings_[index];
        const auto* entry = store.get(binding.state_path.data());
        BindingError error = BindingError::none;
        bool boolean_output = false;
        if (binding.target == BindingTarget::properties) {
            if (binding.predicate != BindingPredicate::value) {
                predicate_value(
                    binding.predicate,
                    binding.predicate_operand,
                    entry,
                    boolean_output,
                    error);
                copy_bounded(
                    binding.rendered.data(),
                    binding.rendered.size(),
                    boolean_output ? "true" : "false");
            } else {
                render_value(
                    binding.format,
                    binding.unit.data(),
                    entry,
                    binding.rendered.data(),
                    binding.rendered.size(),
                    error);
            }
            binding.signature = hash_text(binding.rendered.data());
        } else {
            predicate_value(
                binding.predicate,
                binding.predicate_operand,
                entry,
                boolean_output,
                error);
            copy_bounded(
                binding.rendered.data(),
                binding.rendered.size(),
                boolean_output ? "true" : "false");
            binding.signature = boolean_output ? 1U : 0U;
        }
    }
    observed_store_revision_ = store.revision();
    has_synced_ = true;
    return {
        BindingError::none,
        0,
        static_cast<std::uint16_t>(patch_count)};
}

void BindingHub::clear() {
    bindings_ = {};
    size_ = 0;
    observed_store_revision_ = 0;
    has_synced_ = false;
}

std::size_t BindingHub::size() const {
    return size_;
}

std::uint32_t BindingHub::observed_store_revision() const {
    return observed_store_revision_;
}

const char* BindingHub::rendered_value(
    const char* node_id,
    BindingTarget target) const {
    if (node_id == nullptr) return nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (bindings_[index].target == target &&
            std::strcmp(bindings_[index].node_id.data(), node_id) == 0) {
            return bindings_[index].rendered.data();
        }
    }
    return nullptr;
}

}  // namespace m3e::state
