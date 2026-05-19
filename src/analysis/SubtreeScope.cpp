#include "bt/SubtreeScope.h"

#include <cassert>

namespace bt {

SubtreeScope::SubtreeScope(std::string name, std::unique_ptr<Node> subtreeRoot)
    : Node(std::move(name)) {
    assert(subtreeRoot != nullptr);
    children_.push_back(std::move(subtreeRoot));
}

std::span<const std::unique_ptr<Node>> SubtreeScope::children() const noexcept {
    return children_;
}

void SubtreeScope::reset() {
    children_[0]->reset();
}

Status SubtreeScope::doTick() {
    return children_[0]->tick();
}

}  // namespace bt
