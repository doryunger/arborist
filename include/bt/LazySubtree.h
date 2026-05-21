#pragma once

#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "bt/Node.h"
#include "bt/Status.h"

namespace bt {

// A transparent Node wrapper that defers building its child tree until the
// first tick that reaches it. The factory lambda is supplied at construction
// time (by SchemaLoader, which has access to the schema and registry). Until
// the factory fires, the node is invisible to tree walkers — children() is
// empty. After materialization it behaves identically to SubtreeScope.
//
// Use via PartitionConfig::lazyThreshold: set a node-count threshold above
// which SchemaLoader will wrap behavior subtrees in a LazySubtree instead of
// eagerly building them all at load time.
class LazySubtree : public Node {
public:
    explicit LazySubtree(std::string name,
                         std::function<std::unique_ptr<Node>()> factory);

    [[nodiscard]] bool materialized() const noexcept { return !children_.empty(); }

    // Non-empty if the factory threw on its first invocation.
    // The subtree will return FAILURE on every subsequent tick without retrying.
    [[nodiscard]] const std::string& factoryError() const noexcept { return factoryError_; }

    [[nodiscard]] std::string_view typeName() const noexcept override { return "LazySubtree"; }
    [[nodiscard]] std::span<const std::unique_ptr<Node>> children() const noexcept override;
    void reset() override;

protected:
    Status doTick() override;

private:
    std::function<std::unique_ptr<Node>()> factory_;
    std::vector<std::unique_ptr<Node>>     children_;
    std::string                            factoryError_;
};

}  // namespace bt
