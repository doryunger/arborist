#pragma once

#include <cstddef>

namespace bt {

struct PartitionConfig {
    std::size_t maxNodesPerScope{50};
    bool autoPartition{true};
};

}  // namespace bt
