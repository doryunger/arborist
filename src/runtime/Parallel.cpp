#include "bt/Parallel.h"

namespace bt {

Status Parallel::tick() {
    std::size_t successCount = 0;
    std::size_t failureCount = 0;
    const std::size_t total = children().size();

    for (const auto& child : children()) {
        Status childStatus = child->tick();
        if (childStatus == Status::SUCCESS) {
            ++successCount;
        } else if (childStatus == Status::FAILURE) {
            ++failureCount;
        }
    }

    if (policy_.satisfied(successCount, failureCount, total)) {
        reset();
        return Status::SUCCESS;
    }
    if (policy_.failed(successCount, failureCount, total)) {
        reset();
        return Status::FAILURE;
    }
    return Status::RUNNING;
}

}  // namespace bt
