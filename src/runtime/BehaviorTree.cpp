#include "bt/BehaviorTree.h"

#include <string>
#include <vector>

namespace bt {

namespace {

void collectActivePath(const Node* node, std::uint64_t tickId, std::vector<ActiveNode>& path) {
    if (node->lastTickId() != tickId) {
        return;
    }
    path.push_back(ActiveNode{std::string(node->name()), node->lastStatus()});
    for (const auto& child : node->children()) {
        collectActivePath(child.get(), tickId, path);
    }
}

}  // namespace

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
                currentBehaviorName_ = highest.has_value()
                                           ? behaviors_[*highest].name
                                           : std::string{};
            }
        }
    }

    ++tickCount_;
    Node::setCurrentTickId(tickCount_);
    Status result = root_->tick();

    if (emitter_ != nullptr) {
        std::vector<ActiveNode> activePath;
        collectActivePath(root_.get(), tickCount_, activePath);
        emitter_->record(tickCount_, currentBehaviorName_, result, blackboard_,
                         std::move(activePath));
    }

    if (result != Status::RUNNING) {
        currentBehaviorIndex_.reset();
        currentBehaviorName_.clear();
    }

    return result;
}

void BehaviorTree::reset() noexcept {
    if (root_) { root_->reset(); }
    currentBehaviorIndex_.reset();
    currentBehaviorName_.clear();
}

void BehaviorTree::reload(BehaviorTree next) noexcept {
    if (root_) {
        root_->reset();
    }
    root_ = std::move(next.root_);
    behaviors_ = std::move(next.behaviors_);
    blackboard_ = std::move(next.blackboard_);
    currentBehaviorIndex_.reset();
    currentBehaviorName_.clear();
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
