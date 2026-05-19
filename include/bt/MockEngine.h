#pragma once

#include <any>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "bt/RuntimeRegistry.h"
#include "bt/Status.h"

namespace bt {

// Fake engine for headless simulation. Owns a state store and records
// action calls. Lambdas registered via applyTo() share ownership of the
// state and call-count maps, so state changes after applyTo() are
// visible to the live lambdas without reloading the tree.
class MockEngine {
public:
    MockEngine();

    // State — set before or after applyTo(); changes are live immediately.
    template <typename T>
    void setState(std::string_view key, T value) {
        (*state_)[std::string(key)] = std::move(value);
    }

    // Register a condition that evaluates a bool state key.
    void addCondition(std::string_view name, std::string_view stateKey);

    // Register an action returning a fixed status.
    void addAction(std::string_view name, Status result = Status::SUCCESS);

    // Wire all registered conditions and actions into a registry.
    // Must be called before building a BehaviorTree from the registry.
    void applyTo(RuntimeRegistry& reg);

    // Number of times the named action has been called since construction.
    [[nodiscard]] int callCount(std::string_view name) const;

private:
    struct ConditionDef {
        std::string name;
        std::string stateKey;
    };
    struct ActionDef {
        std::string name;
        Status result;
    };

    std::shared_ptr<std::unordered_map<std::string, std::any>> state_;
    std::shared_ptr<std::unordered_map<std::string, int>> callCounts_;
    std::vector<ConditionDef> conditionDefs_;
    std::vector<ActionDef> actionDefs_;
};

}  // namespace bt
