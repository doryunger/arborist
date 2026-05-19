#include "bt/LazySubtree.h"

#include <cassert>

namespace bt {

LazySubtree::LazySubtree(std::string name,
                          std::function<std::unique_ptr<Node>()> factory)
    : Node(std::move(name)), factory_(std::move(factory)) {
    assert(factory_ != nullptr);
}

std::span<const std::unique_ptr<Node>> LazySubtree::children() const noexcept {
    return children_;
}

void LazySubtree::reset() {
    if (!children_.empty()) {
        children_[0]->reset();
    }
}

Status LazySubtree::doTick() {
    if (children_.empty()) {
        children_.push_back(factory_());
    }
    return children_[0]->tick();
}

}  // namespace bt
