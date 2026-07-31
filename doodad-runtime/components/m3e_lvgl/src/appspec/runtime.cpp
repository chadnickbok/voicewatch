#include "m3e/appspec/runtime.hpp"

#include <cstring>

namespace m3e::appspec {
namespace {

constexpr std::uint16_t kNoParent = 0xffffU;

bool valid_id(const char* id) {
    if (id == nullptr || id[0] < 'a' || id[0] > 'z') {
        return false;
    }
    std::size_t length = 0;
    for (; id[length] != '\0'; ++length) {
        if (length >= 64) {
            return false;
        }
        const char value = id[length];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= '0' && value <= '9') ||
              value == '_' || value == '.' || value == '-')) {
            return false;
        }
    }
    return length > 0;
}

std::int32_t find_index(
    const std::array<ViewHandle, Reconciler::kCapacity>& handles,
    std::size_t size,
    const char* id) {
    if (id == nullptr) {
        return -1;
    }
    for (std::size_t index = 0; index < size; ++index) {
        if (std::strcmp(handles[index].id.data(), id) == 0) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

bool copy_id(std::array<char, 65>& destination, const char* source) {
    if (!valid_id(source)) {
        return false;
    }
    std::strncpy(destination.data(), source, destination.size() - 1);
    destination.back() = '\0';
    return true;
}

}  // namespace

ValidationResult validate(SpecView spec) {
    if (spec.nodes == nullptr || spec.count == 0) {
        return {ValidationError::empty, 0};
    }
    if (spec.count > Reconciler::kCapacity) {
        return {ValidationError::too_many_nodes, 0};
    }
    if (spec.nodes[0].kind != ComponentKind::screen ||
        spec.nodes[0].parent_index != kNoParent ||
        spec.nodes[0].depth != 0) {
        return {ValidationError::root_not_screen, 0};
    }
    for (std::size_t index = 0; index < spec.count; ++index) {
        const auto& node = spec.nodes[index];
        if (!valid_id(node.id)) {
            return {
                ValidationError::missing_id,
                static_cast<std::uint16_t>(index)};
        }
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (std::strcmp(spec.nodes[previous].id, node.id) == 0) {
                return {
                    ValidationError::duplicate_id,
                    static_cast<std::uint16_t>(index)};
            }
        }
        if (index > 0 &&
            (node.parent_index >= index ||
             node.depth !=
                 static_cast<std::uint8_t>(
                     spec.nodes[node.parent_index].depth + 1U))) {
            return {
                ValidationError::invalid_parent,
                static_cast<std::uint16_t>(index)};
        }
        if (node.depth > 12) {
            return {
                ValidationError::excessive_depth,
                static_cast<std::uint16_t>(index)};
        }
        if (node.child_count > 32) {
            return {
                ValidationError::too_many_children,
                static_cast<std::uint16_t>(index)};
        }
        if (node.interactive &&
            (node.semantic_label == nullptr ||
             node.semantic_label[0] == '\0')) {
            return {
                ValidationError::missing_semantics,
                static_cast<std::uint16_t>(index)};
        }
    }
    return {ValidationError::none, 0};
}

ValidationResult Reconciler::mount(SpecView spec) {
    const auto result = validate(spec);
    if (!result.ok()) {
        return result;
    }
    std::array<ViewHandle, kCapacity> staged{};
    for (std::size_t index = 0; index < spec.count; ++index) {
        const auto& node = spec.nodes[index];
        copy_id(staged[index].id, node.id);
        staged[index].kind = node.kind;
        staged[index].parent_index = node.parent_index;
        staged[index].props_hash = node.props_hash;
        staged[index].visible = node.visible;
        staged[index].enabled = node.enabled;
    }
    handles_ = staged;
    size_ = spec.count;
    ++generation_;
    return result;
}

bool Reconciler::apply_transaction(
    const Patch* patches,
    std::size_t count) {
    if ((patches == nullptr && count != 0) || count > 64) {
        return false;
    }
    auto staged = handles_;
    auto staged_size = size_;
    for (std::size_t patch_index = 0; patch_index < count; ++patch_index) {
        const auto& patch = patches[patch_index];
        const auto index = find_index(staged, staged_size, patch.node_id);
        switch (patch.kind) {
            case PatchKind::set_properties:
                if (index < 0) return false;
                staged[index].props_hash = patch.props_hash;
                break;
            case PatchKind::set_visibility:
                if (index < 0) return false;
                staged[index].visible = patch.boolean_value;
                break;
            case PatchKind::set_enabled:
                if (index < 0) return false;
                staged[index].enabled = patch.boolean_value;
                break;
            case PatchKind::insert_leaf: {
                if (index >= 0 || staged_size >= kCapacity ||
                    !valid_id(patch.node_id) ||
                    (patch.semantic_label == nullptr &&
                     (patch.component_kind == ComponentKind::button ||
                      patch.component_kind == ComponentKind::stepper ||
                      patch.component_kind == ComponentKind::toggle ||
                      patch.component_kind == ComponentKind::keypad ||
                      patch.component_kind == ComponentKind::voice_orb))) {
                    return false;
                }
                const auto parent =
                    find_index(staged, staged_size, patch.parent_id);
                if (parent < 0) return false;
                auto& handle = staged[staged_size++];
                copy_id(handle.id, patch.node_id);
                handle.kind = patch.component_kind;
                handle.parent_index = static_cast<std::uint16_t>(parent);
                handle.props_hash = patch.props_hash;
                handle.visible = true;
                handle.enabled = true;
                break;
            }
            case PatchKind::remove_leaf:
                if (index <= 0) return false;
                for (std::size_t child = 0; child < staged_size; ++child) {
                    if (staged[child].parent_index ==
                        static_cast<std::uint16_t>(index)) {
                        return false;
                    }
                }
                for (std::size_t move = static_cast<std::size_t>(index);
                     move + 1 < staged_size; ++move) {
                    staged[move] = staged[move + 1];
                }
                --staged_size;
                for (std::size_t child = 0; child < staged_size; ++child) {
                    if (staged[child].parent_index >
                        static_cast<std::uint16_t>(index)) {
                        --staged[child].parent_index;
                    }
                }
                break;
            case PatchKind::replace_leaf:
                if (index <= 0) return false;
                for (std::size_t child = 0; child < staged_size; ++child) {
                    if (staged[child].parent_index ==
                        static_cast<std::uint16_t>(index)) {
                        return false;
                    }
                }
                staged[index].kind = patch.component_kind;
                staged[index].props_hash = patch.props_hash;
                break;
        }
    }
    handles_ = staged;
    size_ = staged_size;
    ++generation_;
    return true;
}

const ViewHandle* Reconciler::find(const char* id) const {
    const auto index = find_index(handles_, size_, id);
    return index < 0 ? nullptr : &handles_[static_cast<std::size_t>(index)];
}

std::size_t Reconciler::size() const {
    return size_;
}

std::uint32_t Reconciler::generation() const {
    return generation_;
}

CapabilityManifest capabilities() {
    return CapabilityManifest{
        "1.0",
        "1.0",
        "watch_square_192",
        "rgb565",
        250,
        12,
        12,
        0x3bcd4ab3U,
    };
}

bool event_is_valid(const UiEvent& event) {
    if (event.schema != 1 ||
        !valid_id(event.app_id) ||
        !valid_id(event.screen_id) ||
        !valid_id(event.node_id) ||
        !valid_id(event.action_id)) {
        return false;
    }
    switch (event.value.kind) {
        case EventValueKind::none:
        case EventValueKind::integer:
        case EventValueKind::boolean:
            return true;
        case EventValueKind::text:
            return event.value.text_value != nullptr &&
                   event.value.text_value[0] != '\0' &&
                   std::strlen(event.value.text_value) <= 64;
    }
    return false;
}

}  // namespace m3e::appspec
