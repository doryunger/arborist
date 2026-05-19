#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

#include "bt/BehaviorTree.h"
#include "bt/DecisionEmitter.h"
#include "bt/Status.h"

namespace bt {

struct SimulatorResult {
    std::vector<TickRecord> history;
    Status finalStatus{Status::RUNNING};
    std::size_t ticksRun{0};
};

// Drives a BehaviorTree tick loop for headless scenario testing.
// Owns the tree and a DecisionEmitter. State changes are injected via
// atTick() hooks that fire before the specified tick number.
class Simulator {
public:
    explicit Simulator(BehaviorTree tree);

    ~Simulator() = default;
    Simulator(const Simulator&) = delete;
    Simulator& operator=(const Simulator&) = delete;
    Simulator(Simulator&&) = delete;
    Simulator& operator=(Simulator&&) = delete;

    // Register a callback to fire before tick number `tick` (1-based).
    // Multiple hooks at the same tick fire in registration order.
    Simulator& atTick(std::size_t tick, std::function<void()> hook);

    // Run for up to maxTicks. Stops early if the tree returns a terminal
    // status (SUCCESS or FAILURE). Returns the full tick history.
    [[nodiscard]] SimulatorResult run(std::size_t maxTicks);

private:
    BehaviorTree tree_;
    DecisionEmitter emitter_;
    std::unordered_map<std::size_t, std::vector<std::function<void()>>> hooks_;
};

}  // namespace bt
