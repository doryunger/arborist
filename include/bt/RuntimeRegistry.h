#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bt/RegistrySpec.h"
#include "bt/RegistryStore.h"
#include "bt/Status.h"

namespace bt {

class RuntimeRegistry;

class ActionBuilder {
public:
    ActionBuilder(RuntimeRegistry* registry, std::string name);

    ActionBuilder& intent(std::string_view text);
    ActionBuilder& reads(std::string_view stateKey);
    ActionBuilder& writes(std::string_view stateKey);
    void impl(std::function<Status()> func);

private:
    RuntimeRegistry* registry_;
    ActionSpec spec_;
};

class ConditionBuilder {
public:
    ConditionBuilder(RuntimeRegistry* registry, std::string name);

    ConditionBuilder& intent(std::string_view text);
    ConditionBuilder& reads(std::string_view stateKey);
    void impl(std::function<bool()> func);

private:
    RuntimeRegistry* registry_;
    ConditionSpec spec_;
};

// Combines the spec layer (SQLite) with the logic layer (in-memory lambdas).
// The spec is persisted for tooling. The lambdas are in-memory for runtime.
class RuntimeRegistry {
public:
    explicit RuntimeRegistry(std::string_view dbPath = ":memory:");

    [[nodiscard]] ActionBuilder action(std::string_view name);
    [[nodiscard]] ConditionBuilder condition(std::string_view name);
    void state(std::string_view key, std::string_view type);

    // Runtime lookup — returns nullptr if name is not registered.
    [[nodiscard]] const std::function<Status()>* findAction(std::string_view name) const;
    [[nodiscard]] const std::function<bool()>* findCondition(std::string_view name) const;

    [[nodiscard]] const RegistryStore& store() const noexcept { return store_; }

private:
    friend class ActionBuilder;
    friend class ConditionBuilder;

    void commitAction(const ActionSpec& spec, std::function<Status()> func);
    void commitCondition(const ConditionSpec& spec, std::function<bool()> func);

    RegistryStore store_;
    std::unordered_map<std::string, std::function<Status()>> actions_;
    std::unordered_map<std::string, std::function<bool()>> conditions_;
};

}  // namespace bt
