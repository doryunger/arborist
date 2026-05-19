#include "bt/SchemaNode.h"

namespace bt {

std::unique_ptr<SchemaNode> SchemaNode::deepClone() const {
    auto clone       = std::make_unique<SchemaNode>();
    clone->type      = type;
    clone->name      = name;
    clone->intent    = intent;
    clone->policy    = policy;
    clone->threshold = threshold;
    clone->children.reserve(children.size());
    for (const auto& child : children) {
        clone->children.push_back(child->deepClone());
    }
    return clone;
}

}  // namespace bt
