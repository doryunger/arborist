#include "bt/ScenarioRunner.h"
#include <algorithm>

namespace bt {

ScenarioRunner::ScenarioRunner(BehaviorTree tree)
    : tree_(std::move(tree)) {
    tree_.setEmitter(&emitter_);
}

ScenarioRunner& ScenarioRunner::atTick(std::size_t tick, std::function<void()> hook) {
    hooks_[tick].push_back(std::move(hook));
    return *this;
}

ScenarioRunner& ScenarioRunner::expect(std::size_t tick, std::string behaviorName) {
    expectations_[tick] = std::move(behaviorName);
    return *this;
}

ScenarioResult ScenarioRunner::run(std::size_t maxTicks) {
    ScenarioResult result;

    for (std::size_t tick = 1; tick <= maxTicks; ++tick) {
        auto hooksIt = hooks_.find(tick);
        if (hooksIt != hooks_.end()) {
            for (const auto& hook : hooksIt->second) {
                hook();
            }
        }

        Status status = tree_.tick();
        result.ticksRun = tick;
        result.finalStatus = status;

        auto expectIt = expectations_.find(tick);
        if (expectIt != expectations_.end()) {
            const auto& actual = emitter_.history().back().behaviorName;
            ScenarioStepResult stepResult;
            stepResult.tick = tick;
            stepResult.expectedBehavior = expectIt->second;
            stepResult.actualBehavior = actual;
            stepResult.passed = (actual == expectIt->second);
            result.stepResults.push_back(std::move(stepResult));
        }

        if (status != Status::RUNNING) {
            break;
        }
    }

    result.history = emitter_.history();
    result.allPassed = result.stepResults.empty() ||
                       std::all_of(result.stepResults.begin(), result.stepResults.end(),
                                   [](const ScenarioStepResult& step) { return step.passed; });

    return result;
}

}  // namespace bt
