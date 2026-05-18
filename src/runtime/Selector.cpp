#include "bt/Selector.h"

namespace bt {

Status Selector::tick() {
    while (currentChildIndex() < children().size()) {
        Status status = children()[currentChildIndex()]->tick();
        if (status == Status::RUNNING) {
            return Status::RUNNING;
        }
        if (status == Status::SUCCESS) {
            reset();
            return Status::SUCCESS;
        }
        advanceChildIndex();
    }
    reset();
    return Status::FAILURE;
}

}  // namespace bt
