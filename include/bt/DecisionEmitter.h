#pragma once

#include <any>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "bt/Blackboard.h"
#include "bt/Status.h"

namespace bt {

// One entry in the per-tick active path: which node ran and what it returned.
struct ActiveNode {
    std::string name;
    Status      status{Status::RUNNING};
};

struct TickRecord {
    std::size_t tickNumber{0};
    std::string behaviorName;
    Status result{Status::FAILURE};
    std::unordered_map<std::string, std::any> blackboardSnapshot;
    // Every node ticked this frame, in execution order (root → leaf).
    std::vector<ActiveNode> activePath;
};

// Records a structured entry for every tick. Injected into BehaviorTree
// via the observer callback.
class DecisionEmitter {
public:
    void record(std::size_t tickNumber, std::string behaviorName, Status result,
                const Blackboard& blackboard, std::vector<ActiveNode> activePath);

    [[nodiscard]] const std::vector<TickRecord>& history() const noexcept { return history_; }
    void clear() noexcept { history_.clear(); }

private:
    std::vector<TickRecord> history_;
};

}  // namespace bt
