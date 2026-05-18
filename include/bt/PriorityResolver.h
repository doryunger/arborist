#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "bt/BehaviorEntry.h"

namespace bt {

// Scans behaviors in priority order and returns the index of the first
// whose condition is satisfied. Behaviors with no condition are always valid.
class PriorityResolver {
public:
    [[nodiscard]] static std::optional<std::size_t> resolve(
        const std::vector<BehaviorEntry>& entries);
};

}  // namespace bt
