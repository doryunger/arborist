#pragma once

#include <functional>
#include <string>
#include <vector>

#include "bt/Blackboard.h"

namespace bt {

// Stores named state lambdas and registers them with a Blackboard.
// Decouples state declaration from Blackboard lifetime.
class StateRegistry {
public:
    template <typename T>
    void state(std::string key, std::function<T()> source) {
        appliers_.push_back(
            [key = std::move(key), fn = std::move(source)](Blackboard& board) mutable {
                board.registerSource<T>(key, fn);
            });
    }

    void applyTo(Blackboard& board) const {
        for (const auto& applier : appliers_) {
            applier(board);
        }
    }

private:
    std::vector<std::function<void(Blackboard&)>> appliers_;
};

}  // namespace bt
