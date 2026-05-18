#include "bt/Sequence.h"

namespace bt {

Status Sequence::tick() {
    while (currentChildIndex() < children().size()) {
        Status status = children()[currentChildIndex()]->tick();
        if (status == Status::RUNNING) {
            return Status::RUNNING;
        }
        if (status == Status::FAILURE) {
            reset();
            return Status::FAILURE;
        }
        advanceChildIndex();
    }
    reset();
    return Status::SUCCESS;
}

}  // namespace bt
