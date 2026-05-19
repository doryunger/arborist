#include "bt/DecisionEmitter.h"

namespace bt {

void DecisionEmitter::record(std::size_t tickNumber, std::string behaviorName, Status result,
                              const Blackboard& blackboard, std::vector<std::string> activePath) {
    history_.push_back(TickRecord{.tickNumber         = tickNumber,
                                   .behaviorName       = std::move(behaviorName),
                                   .result             = result,
                                   .blackboardSnapshot = blackboard.values(),
                                   .activePath         = std::move(activePath)});
}

}  // namespace bt
