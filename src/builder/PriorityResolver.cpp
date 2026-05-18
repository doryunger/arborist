#include "bt/PriorityResolver.h"

namespace bt {

std::optional<std::size_t> PriorityResolver::resolve(
    const std::vector<BehaviorEntry>& entries) {
    for (std::size_t idx = 0; idx < entries.size(); ++idx) {
        const auto& entry = entries[idx];
        if (!entry.condition || entry.condition()) {
            return idx;
        }
    }
    return std::nullopt;
}

}  // namespace bt
