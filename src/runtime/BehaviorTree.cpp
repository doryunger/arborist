#include "bt/BehaviorTree.h"

namespace bt {

Status BehaviorTree::tick() {
    blackboard_.refresh();

    if (!behaviors_.empty()) {
        auto highest = highestPriorityValid();
        if (highest != currentBehaviorIndex_) {
            bool shouldInterrupt = !currentBehaviorIndex_.has_value() ||
                                   behaviors_[*currentBehaviorIndex_].interruptible;
            if (shouldInterrupt) {
                root_->reset();
                currentBehaviorIndex_ = highest;
            }
        }
    }

    ++tickCount_;
    Status result = root_->tick();

    if (emitter_ != nullptr) {
        emitter_->record(tickCount_, currentBehaviorName_, result, blackboard_);
    }

    if (result != Status::RUNNING) {
        currentBehaviorIndex_.reset();
        currentBehaviorName_.clear();
    }

    return result;
}

std::optional<std::size_t> BehaviorTree::highestPriorityValid() const {
    for (std::size_t idx = 0; idx < behaviors_.size(); ++idx) {
        const auto& meta = behaviors_[idx];
        if (!meta.condition || meta.condition()) {
            return idx;
        }
    }
    return std::nullopt;
}

}  // namespace bt
