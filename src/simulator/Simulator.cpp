#include "bt/Simulator.h"

namespace bt {

Simulator::Simulator(BehaviorTree tree) : tree_(std::move(tree)) {
    tree_.setEmitter(&emitter_);
}

Simulator& Simulator::atTick(std::size_t tick, std::function<void()> hook) {
    hooks_[tick].push_back(std::move(hook));
    return *this;
}

SimulatorResult Simulator::run(std::size_t maxTicks) {
    emitter_.clear();
    SimulatorResult result;

    for (std::size_t tickNum = 1; tickNum <= maxTicks; ++tickNum) {
        auto hookIt = hooks_.find(tickNum);
        if (hookIt != hooks_.end()) {
            for (const auto& hook : hookIt->second) {
                hook();
            }
        }

        result.finalStatus = tree_.tick();
        ++result.ticksRun;

        if (result.finalStatus != Status::RUNNING) {
            break;
        }
    }

    result.history = emitter_.history();
    return result;
}

}  // namespace bt
