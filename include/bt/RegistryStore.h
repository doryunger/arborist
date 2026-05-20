#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bt/RegistrySpec.h"

struct sqlite3;

namespace bt {

// SQLite-backed store for action/condition/state specs.
// Use ":memory:" as dbPath for in-process use (tests, runtime-only).
// Use a file path to persist the registry across sessions (tooling).
class RegistryStore {
public:
    explicit RegistryStore(std::string_view dbPath);
    ~RegistryStore();

    RegistryStore(const RegistryStore&) = delete;
    RegistryStore& operator=(const RegistryStore&) = delete;
    RegistryStore(RegistryStore&&) = delete;
    RegistryStore& operator=(RegistryStore&&) = delete;

    void upsertAction(const ActionSpec& spec);
    void upsertCondition(const ConditionSpec& spec);
    void upsertStateKey(std::string_view key, std::string_view type);

    void removeAction(std::string_view name);
    void removeCondition(std::string_view name);
    void removeStateKey(std::string_view key);

    [[nodiscard]] std::optional<ActionSpec> findAction(std::string_view name) const;
    [[nodiscard]] std::optional<ConditionSpec> findCondition(std::string_view name) const;
    [[nodiscard]] std::vector<ActionSpec> allActions() const;
    [[nodiscard]] std::vector<ConditionSpec> allConditions() const;
    [[nodiscard]] std::vector<StateKeySpec> allStateKeys() const;

private:
    sqlite3* db_{nullptr};

    void createSchema();
    void exec(std::string_view sql) const;
};

}  // namespace bt
