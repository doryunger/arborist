#include "bt/RuntimeRegistry.h"

namespace bt {

// ── ActionBuilder ─────────────────────────────────────────────────────────────

ActionBuilder::ActionBuilder(RuntimeRegistry* registry, std::string name)
    : registry_(registry), spec_{std::move(name), {}, {}, {}} {}

ActionBuilder& ActionBuilder::intent(std::string_view text) {
    spec_.intent = std::string(text);
    return *this;
}

ActionBuilder& ActionBuilder::reads(std::string_view stateKey) {
    spec_.reads.emplace_back(stateKey);
    return *this;
}

ActionBuilder& ActionBuilder::writes(std::string_view stateKey) {
    spec_.writes.emplace_back(stateKey);
    return *this;
}

void ActionBuilder::impl(std::function<Status()> func) {
    registry_->commitAction(spec_, std::move(func));
}

// ── ConditionBuilder ──────────────────────────────────────────────────────────

ConditionBuilder::ConditionBuilder(RuntimeRegistry* registry, std::string name)
    : registry_(registry), spec_{std::move(name), {}, {}} {}

ConditionBuilder& ConditionBuilder::intent(std::string_view text) {
    spec_.intent = std::string(text);
    return *this;
}

ConditionBuilder& ConditionBuilder::reads(std::string_view stateKey) {
    spec_.reads.emplace_back(stateKey);
    return *this;
}

void ConditionBuilder::impl(std::function<bool()> func) {
    registry_->commitCondition(spec_, std::move(func));
}

// ── RuntimeRegistry ───────────────────────────────────────────────────────────

RuntimeRegistry::RuntimeRegistry(std::string_view dbPath) : store_(dbPath) {}

ActionBuilder RuntimeRegistry::action(std::string_view name) {
    return {this, std::string(name)};
}

ConditionBuilder RuntimeRegistry::condition(std::string_view name) {
    return {this, std::string(name)};
}

void RuntimeRegistry::state(std::string_view key, std::string_view type) {
    store_.upsertStateKey(key, type);
}

const std::function<Status()>* RuntimeRegistry::findAction(std::string_view name) const {
    auto found = actions_.find(std::string(name));
    return found != actions_.end() ? &found->second : nullptr;
}

const std::function<bool()>* RuntimeRegistry::findCondition(std::string_view name) const {
    auto found = conditions_.find(std::string(name));
    return found != conditions_.end() ? &found->second : nullptr;
}

void RuntimeRegistry::commitAction(const ActionSpec& spec, std::function<Status()> func) {
    store_.upsertAction(spec);
    actions_[spec.name] = std::move(func);
}

void RuntimeRegistry::commitCondition(const ConditionSpec& spec, std::function<bool()> func) {
    store_.upsertCondition(spec);
    conditions_[spec.name] = std::move(func);
}

}  // namespace bt
