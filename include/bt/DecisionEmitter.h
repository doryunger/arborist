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
//
// capacity > 0: ring buffer — oldest record evicted once the buffer is full.
// capacity == 0 (default): unbounded growth (suitable for tests / small trees).
// setCaptureBlackboard(false): skip the per-tick blackboard copy for production
// workloads where snapshot data is not needed.
class DecisionEmitter {
public:
    explicit DecisionEmitter(std::size_t capacity = 0) noexcept : capacity_(capacity) {}

    void record(std::size_t tickNumber, std::string behaviorName, Status result,
                const Blackboard& blackboard, std::vector<ActiveNode> activePath);

    void setCaptureBlackboard(bool capture) noexcept { captureBlackboard_ = capture; }
    [[nodiscard]] bool capturesBlackboard() const noexcept { return captureBlackboard_; }

    [[nodiscard]] const std::vector<TickRecord>& history() const noexcept { return history_; }
    void clear() noexcept { history_.clear(); }

private:
    std::vector<TickRecord> history_;
    std::size_t             capacity_{0};
    bool                    captureBlackboard_{true};
};

}  // namespace bt
