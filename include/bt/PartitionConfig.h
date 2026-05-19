#pragma once

#include <cstddef>

namespace bt {

struct PartitionConfig {
    std::size_t maxNodesPerScope{50};
    // When > 0, subtrees with more than this many nodes are wrapped in a
    // LazySubtree (deferred instantiation) instead of being built eagerly.
    // Takes priority over autoPartition / maxNodesPerScope when set.
    std::size_t lazyThreshold{0};
    bool autoPartition{true};
};

}  // namespace bt
