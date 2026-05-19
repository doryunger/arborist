#include "bt/DecisionEmitter.h"

namespace bt {

void DecisionEmitter::record(std::size_t tickNumber, std::string behaviorName, Status result,
                              const Blackboard& blackboard, std::vector<ActiveNode> activePath) {
    TickRecord rec;
    rec.tickNumber   = tickNumber;
    rec.behaviorName = std::move(behaviorName);
    rec.result       = result;
    rec.activePath   = std::move(activePath);
    if (captureBlackboard_) {
        rec.blackboardSnapshot = blackboard.values();
    }

    if (capacity_ > 0 && history_.size() >= capacity_) {
        history_.erase(history_.begin());
    }
    history_.push_back(std::move(rec));
}

}  // namespace bt
