#include "bt/BehaviorAction.h"

namespace bt {

Status BehaviorAction::tick() {
    if (!entered_) {
        if (onEnter_) {
            onEnter_();
        }
        entered_ = true;
    }

    Status result = onTick_();

    if (result != Status::RUNNING) {
        if (onExit_) {
            onExit_();
        }
        entered_ = false;
    }

    return result;
}

void BehaviorAction::reset() {
    if (entered_ && onExit_) {
        onExit_();
    }
    entered_ = false;
}

}  // namespace bt
