#include "bt/CompositeNode.h"

namespace bt {

void CompositeNode::reset() {
    currentChildIndex_ = 0;
    for (const auto& child : children()) {
        child->reset();
    }
}

}  // namespace bt
