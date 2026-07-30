#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace m3e {

enum class SemanticRole : std::uint8_t {
    screen,
    heading,
    text,
    button,
    toggle,
    slider,
    progress,
    list,
    list_item,
    dialog,
    timer,
};

enum SemanticState : std::uint16_t {
    semantic_none = 0,
    semantic_disabled = 1U << 0U,
    semantic_selected = 1U << 1U,
    semantic_checked = 1U << 2U,
    semantic_busy = 1U << 3U,
    semantic_modal = 1U << 4U,
};

struct SemanticNode {
    const char* id;
    SemanticRole role;
    const char* label;
    const char* value;
    std::uint16_t state;
    std::uint16_t parent_index;
    std::uint16_t first_child_index;
    std::uint16_t child_count;
};

class SemanticTree {
 public:
    static constexpr std::size_t kCapacity = 250;
    static constexpr std::uint16_t kNoIndex = 0xffffU;

    void clear();
    bool add(const SemanticNode& node);
    bool validate() const;
    const SemanticNode* find(const char* id) const;
    const SemanticNode& at(std::size_t index) const;
    std::size_t size() const;

 private:
    std::array<SemanticNode, kCapacity> nodes_{};
    std::size_t size_ = 0;
};

}  // namespace m3e
