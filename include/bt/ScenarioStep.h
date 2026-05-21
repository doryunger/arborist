#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "bt/DecisionEmitter.h"
#include "bt/Status.h"

namespace bt {

struct ScenarioStepResult {
    std::size_t tick{0};
    std::string expectedBehavior;
    std::string actualBehavior;
    bool passed{false};
};

struct ScenarioResult {
    std::deque<TickRecord> history;
    std::vector<ScenarioStepResult> stepResults;
    Status finalStatus{Status::RUNNING};
    std::size_t ticksRun{0};
    bool allPassed{false};
};

}  // namespace bt
