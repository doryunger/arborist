#include "bt/MockEngine.h"

#include <any>
#include <stdexcept>

namespace bt {

MockEngine::MockEngine()
    : state_(std::make_shared<std::unordered_map<std::string, std::any>>()),
      callCounts_(std::make_shared<std::unordered_map<std::string, int>>()) {}

void MockEngine::addCondition(std::string_view name, std::string_view stateKey) {
    conditionDefs_.push_back({std::string(name), std::string(stateKey)});
}

void MockEngine::addAction(std::string_view name, Status result) {
    actionDefs_.push_back({std::string(name), result});
}

void MockEngine::applyTo(RuntimeRegistry& reg) {
    for (const auto& def : conditionDefs_) {
        auto sharedState = state_;
        auto key = def.stateKey;
        reg.condition(def.name).impl([sharedState, key] {
            auto found = sharedState->find(key);
            if (found == sharedState->end()) {
                return false;
            }
            return std::any_cast<bool>(found->second);
        });
    }

    for (const auto& def : actionDefs_) {
        auto sharedCounts = callCounts_;
        auto actionName = def.name;
        auto actionResult = def.result;
        reg.action(def.name).impl([sharedCounts, actionName, actionResult] {
            ++(*sharedCounts)[actionName];
            return actionResult;
        });
    }
}

int MockEngine::callCount(std::string_view name) const {
    auto found = callCounts_->find(std::string(name));
    return found != callCounts_->end() ? found->second : 0;
}

}  // namespace bt
