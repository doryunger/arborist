#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "bt/Blackboard.h"
#include "bt/DecisionEmitter.h"
#include "bt/Node.h"
#include "bt/Status.h"

namespace bt {

// Metadata the tick loop needs per registered behavior for interruption.
struct BehaviorMeta {
    std::string name;
    std::function<bool()> condition;  // null means always valid
    bool interruptible{true};
};

// Owns the root node and drives the tick loop.
// Before each tick: refreshes the blackboard.
// Interruption: if the highest-priority valid behavior changes and the
// currently running behavior is interruptible, resets the tree before ticking.
class BehaviorTree {
public:
    explicit BehaviorTree(std::unique_ptr<Node> root, Blackboard blackboard = {},
                          std::vector<BehaviorMeta> behaviors = {},
                          DecisionEmitter* emitter = nullptr)
        : root_(std::move(root)),
          blackboard_(std::move(blackboard)),
          behaviors_(std::move(behaviors)),
          emitter_(emitter) {}

    Status tick();

    // Hot-swap the tree between ticks. Resets any in-progress RUNNING state,
    // installs the new root and behavior list, preserves tickCount and emitter.
    void reload(BehaviorTree next) noexcept;

    [[nodiscard]] const Node& root() const noexcept { return *root_; }
    [[nodiscard]] const Blackboard& blackboard() const noexcept { return blackboard_; }
    [[nodiscard]] std::size_t tickCount() const noexcept { return tickCount_; }
    void setEmitter(DecisionEmitter* emitter) noexcept { emitter_ = emitter; }

private:
    std::unique_ptr<Node> root_;
    Blackboard blackboard_;
    std::vector<BehaviorMeta> behaviors_;
    DecisionEmitter* emitter_{nullptr};
    std::size_t tickCount_{0};
    std::optional<std::size_t> currentBehaviorIndex_;
    std::string currentBehaviorName_;

    [[nodiscard]] std::optional<std::size_t> highestPriorityValid() const;
};

}  // namespace bt
