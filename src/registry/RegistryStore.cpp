#include "bt/RegistryStore.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace bt {

namespace {

struct StmtDeleter {
    void operator()(sqlite3_stmt* stmt) const noexcept { sqlite3_finalize(stmt); }
};
using UniqueStmt = std::unique_ptr<sqlite3_stmt, StmtDeleter>;

std::string columnText(sqlite3_stmt* stmt, int col) {
    const auto* raw = sqlite3_column_text(stmt, col);
    if (raw == nullptr) {
        return {};
    }
    return {reinterpret_cast<const char*>(raw)};
}

UniqueStmt prepare(sqlite3* database, std::string_view sql) {
    sqlite3_stmt* rawStmt = nullptr;
    int result = sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()),
                                     &rawStmt, nullptr);
    if (result != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite3_prepare_v2 failed: ") +
                                  sqlite3_errmsg(database));
    }
    return UniqueStmt(rawStmt);
}

void bindText(sqlite3_stmt* stmt, int col, const std::string& value) {
    sqlite3_bind_text(stmt, col, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::vector<std::string> queryStringList(sqlite3* database, std::string_view sql,
                                          const std::string& param) {
    auto stmt = prepare(database, sql);
    bindText(stmt.get(), 1, param);
    std::vector<std::string> results;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        results.push_back(columnText(stmt.get(), 0));
    }
    return results;
}

}  // namespace

RegistryStore::RegistryStore(std::string_view dbPath) {
    sqlite3* rawDb = nullptr;
    int result = sqlite3_open(std::string(dbPath).c_str(), &rawDb);
    db_ = rawDb;
    if (result != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite3_open failed: ") + sqlite3_errmsg(rawDb));
    }
    createSchema();
}

RegistryStore::~RegistryStore() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

void RegistryStore::exec(std::string_view sql) const {
    char* errMsg = nullptr;
    int result = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &errMsg);
    if (result != SQLITE_OK) {
        std::string msg(errMsg != nullptr ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        throw std::runtime_error("sqlite3_exec failed: " + msg);
    }
}

void RegistryStore::createSchema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS state_keys (
            key  TEXT PRIMARY KEY,
            type TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS actions (
            name   TEXT PRIMARY KEY,
            intent TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS conditions (
            name   TEXT PRIMARY KEY,
            intent TEXT NOT NULL DEFAULT ''
        );
        CREATE TABLE IF NOT EXISTS action_reads (
            action_name TEXT NOT NULL,
            state_key   TEXT NOT NULL,
            PRIMARY KEY (action_name, state_key)
        );
        CREATE TABLE IF NOT EXISTS action_writes (
            action_name TEXT NOT NULL,
            state_key   TEXT NOT NULL,
            PRIMARY KEY (action_name, state_key)
        );
        CREATE TABLE IF NOT EXISTS condition_reads (
            condition_name TEXT NOT NULL,
            state_key      TEXT NOT NULL,
            PRIMARY KEY (condition_name, state_key)
        );
    )");
}

void RegistryStore::upsertAction(const ActionSpec& spec) {
    auto stmt = prepare(db_, "INSERT OR REPLACE INTO actions (name, intent) VALUES (?, ?)");
    bindText(stmt.get(), 1, spec.name);
    bindText(stmt.get(), 2, spec.intent);
    sqlite3_step(stmt.get());

    exec("DELETE FROM action_reads  WHERE action_name = '" + spec.name + "'");
    exec("DELETE FROM action_writes WHERE action_name = '" + spec.name + "'");

    for (const auto& key : spec.reads) {
        auto ins =
            prepare(db_, "INSERT OR IGNORE INTO action_reads (action_name, state_key) VALUES (?, ?)");
        bindText(ins.get(), 1, spec.name);
        bindText(ins.get(), 2, key);
        sqlite3_step(ins.get());
    }
    for (const auto& key : spec.writes) {
        auto ins = prepare(
            db_, "INSERT OR IGNORE INTO action_writes (action_name, state_key) VALUES (?, ?)");
        bindText(ins.get(), 1, spec.name);
        bindText(ins.get(), 2, key);
        sqlite3_step(ins.get());
    }
}

void RegistryStore::upsertCondition(const ConditionSpec& spec) {
    auto stmt = prepare(db_, "INSERT OR REPLACE INTO conditions (name, intent) VALUES (?, ?)");
    bindText(stmt.get(), 1, spec.name);
    bindText(stmt.get(), 2, spec.intent);
    sqlite3_step(stmt.get());

    exec("DELETE FROM condition_reads WHERE condition_name = '" + spec.name + "'");

    for (const auto& key : spec.reads) {
        auto ins = prepare(
            db_,
            "INSERT OR IGNORE INTO condition_reads (condition_name, state_key) VALUES (?, ?)");
        bindText(ins.get(), 1, spec.name);
        bindText(ins.get(), 2, key);
        sqlite3_step(ins.get());
    }
}

void RegistryStore::upsertStateKey(std::string_view key, std::string_view type) {
    auto stmt = prepare(db_, "INSERT OR REPLACE INTO state_keys (key, type) VALUES (?, ?)");
    bindText(stmt.get(), 1, std::string(key));
    bindText(stmt.get(), 2, std::string(type));
    sqlite3_step(stmt.get());
}

std::optional<ActionSpec> RegistryStore::findAction(std::string_view name) const {
    auto stmt = prepare(db_, "SELECT name, intent FROM actions WHERE name = ?");
    bindText(stmt.get(), 1, std::string(name));
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    ActionSpec spec;
    spec.name = columnText(stmt.get(), 0);
    spec.intent = columnText(stmt.get(), 1);
    spec.reads = queryStringList(db_,
                                  "SELECT state_key FROM action_reads WHERE action_name = ?",
                                  spec.name);
    spec.writes = queryStringList(db_,
                                   "SELECT state_key FROM action_writes WHERE action_name = ?",
                                   spec.name);
    return spec;
}

std::optional<ConditionSpec> RegistryStore::findCondition(std::string_view name) const {
    auto stmt = prepare(db_, "SELECT name, intent FROM conditions WHERE name = ?");
    bindText(stmt.get(), 1, std::string(name));
    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }
    ConditionSpec spec;
    spec.name = columnText(stmt.get(), 0);
    spec.intent = columnText(stmt.get(), 1);
    spec.reads = queryStringList(db_,
                                  "SELECT state_key FROM condition_reads WHERE condition_name = ?",
                                  spec.name);
    return spec;
}

std::vector<ActionSpec> RegistryStore::allActions() const {
    auto stmt = prepare(db_, "SELECT name, intent FROM actions");
    std::vector<ActionSpec> results;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ActionSpec spec;
        spec.name = columnText(stmt.get(), 0);
        spec.intent = columnText(stmt.get(), 1);
        spec.reads = queryStringList(db_,
                                      "SELECT state_key FROM action_reads WHERE action_name = ?",
                                      spec.name);
        spec.writes = queryStringList(
            db_, "SELECT state_key FROM action_writes WHERE action_name = ?", spec.name);
        results.push_back(std::move(spec));
    }
    return results;
}

std::vector<ConditionSpec> RegistryStore::allConditions() const {
    auto stmt = prepare(db_, "SELECT name, intent FROM conditions");
    std::vector<ConditionSpec> results;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        ConditionSpec spec;
        spec.name = columnText(stmt.get(), 0);
        spec.intent = columnText(stmt.get(), 1);
        spec.reads = queryStringList(
            db_, "SELECT state_key FROM condition_reads WHERE condition_name = ?", spec.name);
        results.push_back(std::move(spec));
    }
    return results;
}

std::vector<StateKeySpec> RegistryStore::allStateKeys() const {
    auto stmt = prepare(db_, "SELECT key, type FROM state_keys");
    std::vector<StateKeySpec> results;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        results.push_back({columnText(stmt.get(), 0), columnText(stmt.get(), 1)});
    }
    return results;
}

}  // namespace bt
