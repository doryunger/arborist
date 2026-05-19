#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "bt/BehaviorTree.h"
#include "bt/DecisionEmitter.h"
#include "bt/ScenarioStep.h"

namespace bt {

// Drives a BehaviorTree tick loop with per-tick state hooks and
// per-tick behavior expectations. Similar to Simulator but adds
// pass/fail assertions that feed the test harness.
class ScenarioRunner {
public:
    explicit ScenarioRunner(BehaviorTree tree);

    ~ScenarioRunner() = default;
    ScenarioRunner(const ScenarioRunner&) = delete;
    ScenarioRunner& operator=(const ScenarioRunner&) = delete;
    ScenarioRunner(ScenarioRunner&&) = delete;
    ScenarioRunner& operator=(ScenarioRunner&&) = delete;

    // Register a state-change hook to fire before tick `tick` (1-based).
    ScenarioRunner& atTick(std::size_t tick, std::function<void()> hook);

    // Assert that a specific behavior runs at tick `tick`.
    ScenarioRunner& expect(std::size_t tick, std::string behaviorName);

    // Run for up to maxTicks. Stops early on a terminal status.
    [[nodiscard]] ScenarioResult run(std::size_t maxTicks);

private:
    BehaviorTree tree_;
    DecisionEmitter emitter_;
    std::unordered_map<std::size_t, std::vector<std::function<void()>>> hooks_;
    std::unordered_map<std::size_t, std::string> expectations_;
};

}  // namespace bt
