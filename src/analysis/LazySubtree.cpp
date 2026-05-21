#include "bt/LazySubtree.h"

#include <cassert>
#include <stdexcept>

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
    if (!factoryError_.empty()) { return Status::FAILURE; }
    if (children_.empty()) {
        try {
            children_.push_back(factory_());
        } catch (const std::exception& exc) {
            factoryError_ = exc.what();
            return Status::FAILURE;
        } catch (...) {
            factoryError_ = "unknown error during LazySubtree materialization";
            return Status::FAILURE;
        }
    }
    return children_[0]->tick();
}

}  // namespace bt
