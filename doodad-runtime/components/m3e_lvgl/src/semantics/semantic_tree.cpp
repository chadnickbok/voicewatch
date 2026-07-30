#include "m3e/semantics/semantic_tree.hpp"

#include <cstring>

namespace m3e {

void SemanticTree::clear() {
    size_ = 0;
}

bool SemanticTree::add(const SemanticNode& node) {
    if (size_ >= kCapacity || node.id == nullptr || node.id[0] == '\0') {
        return false;
    }
    if (find(node.id) != nullptr) {
        return false;
    }
    if (node.parent_index != kNoIndex && node.parent_index >= size_) {
        return false;
    }
    nodes_[size_++] = node;
    return true;
}

bool SemanticTree::validate() const {
    if (size_ == 0 || nodes_[0].parent_index != kNoIndex) {
        return false;
    }
    for (std::size_t index = 0; index < size_; ++index) {
        const auto& node = nodes_[index];
        if (node.id == nullptr || node.id[0] == '\0') {
            return false;
        }
        if (index > 0 && node.parent_index >= index) {
            return false;
        }
        if ((node.role == SemanticRole::button ||
             node.role == SemanticRole::toggle ||
             node.role == SemanticRole::slider) &&
            (node.label == nullptr || node.label[0] == '\0')) {
            return false;
        }
    }
    return true;
}

const SemanticNode* SemanticTree::find(const char* id) const {
    if (id == nullptr) {
        return nullptr;
    }
    for (std::size_t index = 0; index < size_; ++index) {
        if (std::strcmp(nodes_[index].id, id) == 0) {
            return &nodes_[index];
        }
    }
    return nullptr;
}

const SemanticNode& SemanticTree::at(std::size_t index) const {
    return nodes_[index];
}

std::size_t SemanticTree::size() const {
    return size_;
}

}  // namespace m3e
