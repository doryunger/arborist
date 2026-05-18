#pragma once

#include <memory>
#include <vector>

#include "bt/BehaviorEntry.h"
#include "bt/BehaviorTree.h"

namespace bt {

// Builds a BehaviorTree from a list of BehaviorEntry definitions.
//
// Generated tree structure:
//   Selector (root)
//   ├── Sequence (behavior 0)
//   │   ├── Condition: entry.condition   (omitted if no condition)
//   │   └── BehaviorAction: entry.onTick (wraps onEnter/onExit)
//   ├── Sequence (behavior 1)
//   │   └── ...
//   └── ...
//
// The BehaviorMeta list is also passed to BehaviorTree so it can
// enforce interruption policy on each tick.
class TreeAssembler {
public:
    [[nodiscard]] static BehaviorTree assemble(std::vector<BehaviorEntry> entries,
                                               Blackboard blackboard = {});
};

}  // namespace bt
