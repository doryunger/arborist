#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "bt/BehaviorTree.h"
#include "bt/RegistryStore.h"
#include "bt/SchemaLoader.h"

namespace bt {

// Standalone HTTP server for the behavior tree visual editor.
// Independent of any live BehaviorTree or DecisionEmitter — it works
// entirely from the RegistryStore (contract data) and an optional YAML
// schema file on disk.
//
// Default port: 8081  (monitor viewer stays on 8080).
//
// REST API:
//   GET  /               — editor UI (HTML)
//   GET  /api/actions    — all declared actions as JSON
//   GET  /api/conditions — all declared conditions as JSON
//   GET  /api/blackboard — all declared blackboard keys as JSON
//   GET  /api/schema     — current schema YAML wrapped in JSON
//   POST /api/schema     — save updated schema YAML to disk; reloads
//                          the attached tree if one was set via attachTree()
//   GET  /api/analyze    — run ComplexityAnalyzer; returns issues + metrics
class EditorServer {
public:
    // schemaPath: path to the YAML file the editor loads and saves.
    // Pass an empty string to run without file persistence (read-only mode).
    explicit EditorServer(RegistryStore& store,
                          std::string_view schemaPath = "");
    ~EditorServer();

    EditorServer(const EditorServer&) = delete;
    EditorServer& operator=(const EditorServer&) = delete;
    EditorServer(EditorServer&&) = delete;
    EditorServer& operator=(EditorServer&&) = delete;

    void start(int port = 8081);
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return running_; }

    // Wire a live BehaviorTree to the editor so that every successful schema
    // save also hot-reloads the running tree. The registry is needed to
    // rebuild the tree from the new YAML. Both pointers must outlive the
    // EditorServer. Pass nullptr to detach.
    // If the new schema fails to load (e.g. unknown action name), the save
    // still succeeds but the running tree is left unchanged.
    void attachTree(BehaviorTree* tree, const LoaderRegistry& reg) noexcept;

    // Wire a DecisionEmitter so that GET /api/tickstate returns the latest
    // tick record (active behavior + active-path nodes with statuses).
    // Pointer must outlive the EditorServer. Pass nullptr to detach.
    void attachEmitter(DecisionEmitter* emitter) noexcept;

    // Direct data accessors — return the same JSON the HTTP endpoints serve.
    // Useful for tests without starting the HTTP server.
    [[nodiscard]] std::string getActionsJson()    const;
    [[nodiscard]] std::string getConditionsJson() const;
    [[nodiscard]] std::string getBlackboardJson() const;
    [[nodiscard]] std::string getSchemaJson()     const;
    [[nodiscard]] std::string getAnalyzeJson()    const;
    [[nodiscard]] std::string getTreeJson()       const;
    // Returns the most recent TickRecord as JSON, or an empty-state object
    // if no emitter is attached or no ticks have been recorded yet.
    [[nodiscard]] std::string getTickStateJson()  const;

    // Save schema YAML to the configured file path. If a tree is attached via
    // attachTree(), also rebuilds and hot-swaps it from the new YAML.
    // Returns true on success, false if no path is configured or write fails.
    [[nodiscard]] bool saveSchema(std::string_view yaml);

    // Contract authoring — mutate the RegistryStore directly.
    void putAction(std::string_view name, std::string_view intent,
                   const std::vector<std::string>& reads,
                   const std::vector<std::string>& writes);
    void putCondition(std::string_view name, std::string_view intent,
                      const std::vector<std::string>& reads);
    void putStateKey(std::string_view key, std::string_view type);
    void removeAction(std::string_view name);
    void removeCondition(std::string_view name);
    void removeStateKey(std::string_view key);

private:
    RegistryStore*        store_;
    std::string           schemaPath_;
    bool                  running_{false};
    BehaviorTree*         attachedTree_{nullptr};
    const LoaderRegistry* attachedReg_{nullptr};
    DecisionEmitter*      attachedEmitter_{nullptr};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt
