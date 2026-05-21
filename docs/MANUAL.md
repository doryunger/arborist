# Arborist — Framework Manual

## Table of Contents

1. [Overview](#1-overview)
2. [Prerequisites and Setup](#2-prerequisites-and-setup)
3. [Core Concepts](#3-core-concepts)
4. [Schema — YAML Reference](#4-schema--yaml-reference)
5. [RuntimeRegistry — Registering Behaviour](#5-runtimeregistry--registering-behaviour)
6. [Blackboard — Shared State](#6-blackboard--shared-state)
7. [Loading and Ticking a Tree](#7-loading-and-ticking-a-tree)
8. [DecisionEmitter — Capturing History](#8-decisionemitter--capturing-history)
9. [MonitorServer — Live Tree Viewer](#9-monitorserver--live-tree-viewer)
10. [EditorServer — Visual Schema Editor](#10-editorserver--visual-schema-editor)
11. [RegistryStore — Contract Persistence](#11-registrystore--contract-persistence)
12. [ContractValidator — Runtime Contract Checks](#12-contractvalidator--runtime-contract-checks)
13. [ComplexityAnalyzer — Structural Analysis](#13-complexityanalyzer--structural-analysis)
14. [ScenarioRunner — Automated Behaviour Testing](#14-scenariorunner--automated-behaviour-testing)
15. [TickPool — Multi-Agent Threading](#15-tickpool--multi-agent-threading)
16. [C API — Engine Integration](#16-c-api--engine-integration)
17. [Quick Reference](#17-quick-reference)

---

## 1. Overview

Arborist is a C++20 behaviour tree framework designed for game AI and simulation. It separates three concerns that are usually tangled together:

- **Schema** — the _structure_ of a behaviour tree, expressed in YAML. Edited by designers.
- **Contracts** — the _declarations_ of what each action and condition reads/writes. Stored in SQLite, editable via browser UI.
- **Implementations** — the _code_ that runs when a node is ticked. Written by programmers in C++ (or via the C API from Unity/Unreal/Godot).

This separation means designers can iterate on tree structure without touching code, and programmers can validate that implementations honour their declared contracts automatically.

### Key properties

- **Priority-based interruption** — behaviours are listed in priority order; a higher-priority condition becoming true mid-tick interrupts and resets the current behaviour.
- **RUNNING resumption** — a node that returns `RUNNING` is resumed on the next tick at the exact node that returned it, not re-evaluated from root.
- **Live viewer** — a MonitorServer streams the active node path and blackboard snapshot to a browser in real time.
- **Visual editor** — an EditorServer serves a full schema editor in the browser, backed by the SQLite contract store.
- **Engine-agnostic C API** — `capi.h` provides an `extern "C"` bridge for Unity NativePlugins, Unreal plugins, and Godot GDExtensions.

---

## 2. Prerequisites and Setup

### Dependencies

| Dependency | Purpose | Install |
|-----------|---------|---------|
| CMake ≥ 3.20 | Build system | `apt install cmake` |
| GCC 12+ or Clang 15+ | C++20 compiler | `apt install g++-12` |
| yaml-cpp | YAML parsing | `apt install libyaml-cpp-dev` |
| SQLite3 | Contract storage | `apt install libsqlite3-dev` |
| Threads | TickPool | included with compiler |

`cpp-httplib` is fetched automatically by CMake via FetchContent — no manual install needed.

### Building

```bash
git clone <repo>
cd bt-framework
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running tests

```bash
ctest --test-dir build --output-on-failure
```

### Turning off clang-tidy (faster iteration builds)

```bash
cmake -S . -B build -DENABLE_CLANG_TIDY=OFF
cmake --build build -j$(nproc)
```

### Entry points

| Script | What it does |
|--------|-------------|
| `./editor/run.sh` | Start the visual schema editor (empty, persistent) |
| `./playground/run.sh` | Start the editor pre-loaded with a demo NPC scenario |
| `./run_demo.sh` | Start the live NPC demo with the real-time tree viewer |

---

## 3. Core Concepts

### 3.1 Status

Every node tick returns one of three values:

```cpp
enum class bt::Status : std::uint8_t {
    SUCCESS,   // node completed successfully
    FAILURE,   // node completed unsuccessfully
    RUNNING,   // node is in progress; tick me again next frame
};
```

The tree propagates these values up through composite nodes according to their type.

### 3.2 Node types

| Type | Class | Behaviour |
|------|-------|-----------|
| Action | `ActionNode` | Leaf — calls your implementation lambda |
| Condition | `ConditionNode` | Leaf — calls your predicate; returns SUCCESS/FAILURE |
| Sequence | `Sequence` | Composite — ticks children left-to-right; stops on first FAILURE |
| Selector | `Selector` | Composite — ticks children left-to-right; stops on first SUCCESS |
| Parallel | `Parallel` | Composite — ticks ALL children every tick; result via Policy |

### 3.3 Sequence

Ticks children in order. Returns FAILURE the moment any child fails. Returns SUCCESS only when all children succeed. Resumes a RUNNING child on the next tick without re-evaluating earlier children.

```
Sequence
├─ move_to_target    → SUCCESS (done, skip next time)
├─ aim_weapon        → RUNNING  ← resumes here next tick
└─ fire              (not reached yet)
```

### 3.4 Selector

Ticks children in order. Returns SUCCESS the moment any child succeeds. Returns FAILURE only when all children fail. Useful for fallback chains.

```
Selector
├─ find_cover        → FAILURE (no cover available)
├─ dodge_sideways    → SUCCESS  ← returns SUCCESS, stops here
└─ take_hit          (not reached)
```

### 3.5 Parallel

Ticks ALL children every tick regardless of their individual results. The aggregated result depends on the Policy:

| Policy | Returns SUCCESS when | Returns FAILURE when |
|--------|---------------------|---------------------|
| `Policy::all()` | every child succeeds | any child fails |
| `Policy::any()` | any child succeeds | every child fails |
| `Policy::threshold(n)` | ≥ n children succeed | too few can possibly succeed |

### 3.6 Priority-based interruption

Behaviours are declared in priority order (highest first). Before each tick, the framework evaluates all `when:` conditions and selects the highest-priority eligible behaviour. If a different behaviour is selected than the one currently running — and the current behaviour is interruptible — the tree is reset and the new behaviour starts from its root.

```yaml
behaviors:
  - name: emergency        # priority 1 — checked first every tick
    when: health_critical
    tree: ...
  - name: combat           # priority 2
    when: enemy_visible
    tree: ...
  - name: patrol           # priority 3 — unconditional fallback
    tree: ...
```

If `health_critical` becomes true while `combat` is running, the combat tree is abandoned and `emergency` starts immediately.

---

## 4. Schema — YAML Reference

The schema defines tree structure. It does _not_ contain logic — all logic lives in your registered C++ implementations.

### Minimal example

```yaml
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
```

### Full behavior fields

```yaml
behaviors:
  - name: combat               # unique identifier (required)
    when: enemy_visible        # gate condition — named condition from registry
    interruptible: true        # default true; set false to run to completion
    intent: "Engage visible enemy"   # human-readable description (optional)
    tree:
      ...
```

### Node types

#### Action

```yaml
type: action
name: shoot_weapon   # must match a registered action name
```

#### Condition

```yaml
type: condition
name: has_ammo       # must match a registered condition name
```

Returns SUCCESS if the condition is true, FAILURE if false.

#### Sequence

```yaml
type: sequence
children:
  - type: action
    name: take_cover
  - type: action
    name: aim_at_target
  - type: action
    name: fire_weapon
```

#### Selector

```yaml
type: selector
children:
  - type: sequence
    children:
      - type: condition
        name: has_ammo
      - type: action
        name: shoot
  - type: action
    name: melee_strike   # fallback if no ammo
```

#### Parallel

```yaml
type: parallel
policy: any       # "all", "any", or "threshold"
threshold: 2      # only used when policy is "threshold"
children:
  - type: action
    name: move_to_cover
  - type: action
    name: report_position
```

### Interruptible flag

```yaml
- name: reload_sequence
  when: low_ammo
  interruptible: false   # will run to completion even if a higher-priority
  tree:                  # behaviour becomes eligible mid-tick
    type: sequence
    children:
      - type: action
        name: eject_magazine
      - type: action
        name: insert_magazine
      - type: action
        name: chamber_round
```

### Multi-behaviour with priority ordering

```yaml
schema_version: "1.0"
behaviors:
  - name: flee
    when: overwhelmed
    tree:
      type: action
      name: run_away

  - name: fight
    when: enemy_close
    tree:
      type: selector
      children:
        - type: sequence
          children:
            - type: condition
              name: has_ammo
            - type: action
              name: shoot
        - type: action
          name: punch

  - name: idle
    tree:
      type: action
      name: stand_around
```

---

## 5. RuntimeRegistry — Registering Behaviour

`RuntimeRegistry` is the integration layer between the schema and your game code. It registers the action lambdas and condition predicates that run when a node is ticked, and simultaneously declares the contracts (intents, reads, writes) that the editor and validator use.

```cpp
#include "bt/RuntimeRegistry.h"

bt::RuntimeRegistry reg;   // in-memory SQLite, no file persistence
// — or —
bt::RuntimeRegistry reg("my_game.db");   // persists contracts across sessions
```

### Registering an action

```cpp
reg.action("shoot_weapon")
    .intent("Fire the currently equipped weapon at the locked target")
    .reads("ammo_count")
    .reads("target_distance")
    .writes("ammo_count")
    .impl([&world] {
        if (world.ammo == 0) { return bt::Status::FAILURE; }
        --world.ammo;
        world.shotsFired++;
        return bt::Status::RUNNING;   // animation still playing
    });
```

The builder chain is:

| Call | Required | Description |
|------|----------|-------------|
| `.intent("...")` | no | Human-readable description shown in the editor |
| `.reads("key")` | no, repeatable | Blackboard keys this action observes |
| `.writes("key")` | no, repeatable | Blackboard keys this action modifies |
| `.impl(lambda)` | **yes** | `std::function<bt::Status()>` — called on every tick |

### Registering a condition

```cpp
reg.condition("enemy_visible")
    .intent("True when an enemy is within line-of-sight range")
    .reads("enemy_distance")
    .reads("los_blocked")
    .impl([&world] {
        return world.enemyDistance < 20.0f && !world.losBlocked;
    });
```

The builder chain:

| Call | Required | Description |
|------|----------|-------------|
| `.intent("...")` | no | Human-readable description |
| `.reads("key")` | no, repeatable | Blackboard keys this condition observes |
| `.impl(lambda)` | **yes** | `std::function<bool()>` |

### Declaring state keys

State keys appear in the editor's blackboard panel and validate that your blackboard sources are declared.

```cpp
reg.state("ammo_count",      "double");
reg.state("enemy_distance",  "double");
reg.state("is_stunned",      "bool");
```

### Accessing the underlying store

```cpp
const bt::RegistryStore& store = reg.store();
auto actions    = store.allActions();
auto conditions = store.allConditions();
```

---

## 6. Blackboard — Shared State

The `Blackboard` is a key-value store that bridges your game state with the behaviour tree. It is refreshed automatically before every tick.

### Registering sources

Sources are lambdas that pull live data from your game each tick:

```cpp
bt::Blackboard board;

board.registerSource<double>("health",  [&player] { return player.health; });
board.registerSource<double>("ammo",    [&weapon] { return weapon.ammo; });
board.registerSource<bool>("is_dead",   [&player] { return player.health <= 0.0; });
```

Any type that `std::any` can hold is supported.

### Reading values

```cpp
double health = board.get<double>("health");
bool isDead   = board.get<bool>("is_dead");
```

`get<T>()` throws `std::out_of_range` if the key does not exist.

### Setting values directly

For values you want to set explicitly (not via a source lambda):

```cpp
board.set<int>("wave_number", 3);
```

### Type safety

Once a key is written — either via `set<T>()` or `registerSource<T>()` — the framework records its type. Any subsequent call for that key with a different type throws `std::runtime_error` immediately:

```cpp
board.set<int>("health", 100);
board.set<float>("health", 100.0f);   // throws: "Blackboard type conflict for key 'health'..."
```

```cpp
board.registerSource<bool>("enemy_near", [&] { return sensor.nearbyEnemy(); });
board.get<int>("enemy_near");          // throws: "Blackboard type mismatch for key 'enemy_near'..."
```

Both error messages include the key name and the conflicting type names, making the source of the bug immediately clear. This replaces the opaque `std::bad_any_cast` that would otherwise appear at an arbitrary point during a live tick.

The same type registered again is always allowed — updating a value or swapping a source lambda of the same type is fine:

```cpp
board.set<int>("health", 100);
board.set<int>("health", 95);   // OK — same type, updated value
```

### Manual refresh

`BehaviorTree::tick()` calls `board.refresh()` automatically before each tick. If you use the `Blackboard` independently, call `refresh()` yourself:

```cpp
board.refresh();   // pulls all registered sources into the value store
```

### Checking for a key

```cpp
if (board.has("health")) { ... }
```

---

## 7. Loading and Ticking a Tree

### Loading from YAML

```cpp
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"

bt::RuntimeRegistry reg;
// ... register actions and conditions ...

// Pass YAML as a string
auto tree = bt::SchemaLoader::load(kYamlString, reg);

// Pass a Blackboard with registered sources
bt::Blackboard board;
board.registerSource<double>("health", [&world] { return world.health; });
auto tree = bt::SchemaLoader::load(kYamlString, reg, std::move(board));
```

`SchemaLoader::load()` throws `bt::SchemaLoadError` if the YAML is malformed or references an unregistered action/condition.

### Loading from a file

```cpp
std::ifstream file("npc_guard.yaml");
std::string yaml((std::istreambuf_iterator<char>(file)),
                  std::istreambuf_iterator<char>());
auto tree = bt::SchemaLoader::load(yaml, reg, std::move(board));
```

### Ticking

```cpp
// Typically called once per game frame or AI update
bt::Status result = tree.tick();

switch (result) {
    case bt::Status::SUCCESS: /* behavior completed */   break;
    case bt::Status::FAILURE: /* behavior failed */      break;
    case bt::Status::RUNNING: /* behavior in progress */ break;
}
```

### Resetting

```cpp
tree.reset();   // clears RUNNING state; next tick starts from behavior root
```

### Hot-swap (live reload)

Replace the entire tree between ticks without losing the tick counter or attached emitter:

```cpp
auto newTree = bt::SchemaLoader::load(updatedYaml, reg, std::move(board));
tree.reload(std::move(newTree));
// next tree.tick() uses the new structure
```

### Inspecting tree state

```cpp
std::size_t ticks = tree.tickCount();
const bt::Node& root = tree.root();
const bt::Blackboard& board = tree.blackboard();
```

---

## 8. DecisionEmitter — Capturing History

`DecisionEmitter` records structured data for every tick: which behaviour ran, what status it returned, which nodes were visited, and a snapshot of the blackboard.

### Basic setup

```cpp
#include "bt/DecisionEmitter.h"

bt::DecisionEmitter emitter;
tree.setEmitter(&emitter);

// ... tick the tree ...

for (const bt::TickRecord& record : emitter.history()) {
    std::cout << "tick " << record.tickNumber
              << "  behavior=" << record.behaviorName
              << "  result=" << bt::toString(record.result) << "\n";
}
```

### TickRecord fields

```cpp
struct TickRecord {
    std::size_t tickNumber;
    std::string behaviorName;
    bt::Status  result;
    std::unordered_map<std::string, std::any> blackboardSnapshot;
    std::vector<bt::ActiveNode> activePath;  // nodes visited, root → leaf
};
```

### Ring buffer mode

For long-running simulations, cap the history to avoid unbounded memory growth:

```cpp
bt::DecisionEmitter emitter(512);   // keeps the last 512 tick records
```

### Disabling blackboard snapshots

In performance-sensitive builds where snapshot data is not needed:

```cpp
emitter.setCaptureBlackboard(false);
```

### Clearing history

```cpp
emitter.clear();
```

---

## 9. MonitorServer — Live Tree Viewer

The `MonitorServer` serves a browser UI that shows the active node path highlighted in the tree, the full tick history, and the blackboard state — updated in real time as the tree ticks.

### Setup

```cpp
#include "bt/MonitorServer.h"
#include "bt/DecisionEmitter.h"

bt::DecisionEmitter emitter;
tree.setEmitter(&emitter);

bt::MonitorServer monitor(tree, emitter);
monitor.start(8080);   // non-blocking; starts a background thread

// ... tick loop ...

monitor.stop();   // stops the background thread and cleans up
```

### Browser access

Open `http://localhost:8080` while the tree is ticking.

### Checking server state

```cpp
if (monitor.running()) { ... }
```

### Querying data without HTTP

```cpp
std::string treeJson    = monitor.getTree();
std::string historyJson = monitor.getHistory();
```

Useful in tests or when embedding the data elsewhere.

### Typical pattern in a game loop

```cpp
bt::DecisionEmitter emitter;
tree.setEmitter(&emitter);
bt::MonitorServer monitor(tree, emitter);
monitor.start(8080);

while (!quit) {
    tree.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

monitor.stop();
```

---

## 10. EditorServer — Visual Schema Editor

The `EditorServer` serves a browser-based editor for authoring and saving the behaviour tree schema. It works entirely from the `RegistryStore` (contract data) and an optional YAML file on disk — no live tree is required.

### Setup

```cpp
#include "bt/EditorServer.h"
#include "bt/RegistryStore.h"

bt::RegistryStore store("my_project.db");
bt::EditorServer editor(store, "npc_guard.yaml");
editor.start(8081);   // non-blocking

// ... keep the process alive (e.g. wait for user input) ...

editor.stop();
```

### Attaching a live tree for hot-reload

When the EditorServer is running alongside a live game loop, call `attachTree()` to wire the schema editor directly to the running tree. Every time `POST /api/schema` succeeds, the framework rebuilds the tree from the new YAML and calls `BehaviorTree::reload()` without interrupting the game loop:

```cpp
bt::RegistryStore store("project.db");
bt::EditorServer editor(store, "npc_guard.yaml");

// Build tree from the same schema
bt::LoaderRegistry loaderReg;
loaderReg.actions["patrol"] = [&world] { world.patrol(); return bt::Status::RUNNING; };
loaderReg.actions["attack"] = [&world] { world.attack(); return bt::Status::RUNNING; };
auto tree = bt::SchemaLoader::load(kYaml, loaderReg);

// Wire the editor to the live tree
editor.attachTree(&tree, loaderReg);

editor.start(8081);

// Saving schema in the browser now hot-reloads 'tree' automatically
```

If the new schema references an action not in `loaderReg`, the file is saved but the live tree is left unchanged — no crash, no interruption.

### Attaching a DecisionEmitter for live tick overlay

When `attachEmitter()` is called, the editor's graph view overlays which nodes ran on the most recent tick, color-coded by status. The overlay updates every 500 ms via `GET /api/tickstate`:

```cpp
bt::DecisionEmitter emitter(512);   // ring buffer of 512 records
tree.setEmitter(&emitter);

editor.attachEmitter(&emitter);
editor.start(8081);
// Open http://localhost:8081 — active nodes now glow in the graph
```

Both `attachTree()` and `attachEmitter()` accept `nullptr` to detach. Pointers must remain valid for the lifetime of the `EditorServer`.

### REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Browser editor UI |
| `GET` | `/api/actions` | All registered actions (JSON) |
| `GET` | `/api/conditions` | All registered conditions (JSON) |
| `GET` | `/api/blackboard` | All declared blackboard keys (JSON) |
| `GET` | `/api/schema` | Current schema YAML wrapped in JSON |
| `POST` | `/api/schema` | Save schema YAML; hot-reloads attached tree |
| `GET` | `/api/tree` | Full tree as structured JSON with node IDs and paths |
| `GET` | `/api/analyze` | Run `ComplexityAnalyzer`; returns issues + metrics |
| `GET` | `/api/tickstate` | Latest tick record — tick number, behavior, active path |
| `PUT` | `/api/actions/:name` | Upsert an action contract |
| `DELETE` | `/api/actions/:name` | Remove an action contract |
| `PUT` | `/api/conditions/:name` | Upsert a condition contract |
| `DELETE` | `/api/conditions/:name` | Remove a condition contract |
| `PUT` | `/api/blackboard/:key` | Upsert a blackboard key declaration |
| `DELETE` | `/api/blackboard/:key` | Remove a blackboard key declaration |

### Programmatic contract authoring

You can modify the registry through the editor API directly:

```cpp
editor.putAction("new_action",
    "Move the agent to the target location",
    {"target_position", "agent_position"},   // reads
    {"agent_position"});                      // writes

editor.putCondition("at_destination",
    "True when the agent has reached the target",
    {"agent_position", "target_position"});

editor.putStateKey("agent_position", "double");
```

### Saving schema from code

```cpp
bool ok = editor.saveSchema(yamlString);
```

---

## 11. RegistryStore — Contract Persistence

`RegistryStore` is an SQLite-backed database of action and condition contracts. It is the source of truth for the editor, the validator, and the complexity analyzer.

### Creating a store

```cpp
bt::RegistryStore store(":memory:");      // in-memory; for tests or runtime-only use
bt::RegistryStore store("project.db");    // file-backed; persists across sessions
```

### Upserting contracts

```cpp
bt::ActionSpec actionSpec;
actionSpec.name   = "shoot_weapon";
actionSpec.intent = "Fire the currently equipped weapon";
actionSpec.reads  = {"ammo_count", "target_distance"};
actionSpec.writes = {"ammo_count"};
store.upsertAction(actionSpec);

bt::ConditionSpec condSpec;
condSpec.name   = "has_ammo";
condSpec.intent = "True when the weapon has ammunition remaining";
condSpec.reads  = {"ammo_count"};
store.upsertCondition(condSpec);

store.upsertStateKey("ammo_count", "double");
```

### Querying

```cpp
auto actions    = store.allActions();    // std::vector<ActionSpec>
auto conditions = store.allConditions(); // std::vector<ConditionSpec>
auto keys       = store.allStateKeys();  // std::vector<StateKeySpec>

auto action = store.findAction("shoot_weapon");  // std::optional<ActionSpec>
```

### Removing entries

```cpp
store.removeAction("old_action");
store.removeCondition("old_condition");
store.removeStateKey("old_key");
```

---

## 12. ContractValidator — Runtime Contract Checks

`ContractValidator` checks that your implementations actually honour the contracts you declared in the registry. Run it after a `ScenarioRunner` exercise to find discrepancies.

### What it checks

| Check | Violation type | Description |
|-------|---------------|-------------|
| Declared read present | `READ_NOT_SATISFIED` | A key in `reads[]` was never in the blackboard while the behaviour ran |
| Declared write observed | `WRITE_NOT_OBSERVED` | A key in `writes[]` never changed while the behaviour ran |
| Undeclared write | `UNDECLARED_WRITE` | A key changed but is not in `writes[]` |

### Usage

```cpp
#include "bt/ContractValidator.h"
#include "bt/ScenarioRunner.h"

bt::ScenarioRunner runner(std::move(tree));
runner.expect(1, "patrol");
runner.expect(3, "combat");
auto result = runner.run(10);

bt::ContractValidator validator(store);
auto violations = validator.validate(result);

for (const auto& violation : violations) {
    std::cout << violation.message << "\n";
}
```

### Behavior-to-action name mapping

When the behaviour name differs from the action name in the registry, supply an explicit map:

```cpp
bt::ContractValidator validator(store, {
    {"combat_behavior", "shoot_weapon"},
    {"retreat_behavior", "run_to_cover"},
});
```

---

## 13. ComplexityAnalyzer — Structural Analysis

`ComplexityAnalyzer` walks the live node tree after loading and flags structural issues before the first tick.

### Running the analyzer

```cpp
#include "bt/ComplexityAnalyzer.h"

auto report = bt::ComplexityAnalyzer::analyze(tree);

std::cout << report.summary() << "\n";
// "5 nodes, depth 3, width 2, 0 errors, 1 warning"

for (const auto& issue : report.issues) {
    std::cout << (issue.isError() ? "ERROR" : "WARN")
              << "  " << issue.nodePath
              << "  " << issue.message << "\n";
}
```

### Issue codes

| Code | Severity | Meaning |
|------|----------|---------|
| `EMPTY_COMPOSITE` | ERROR | Sequence/Selector/Parallel has no children |
| `SINGLE_CHILD_COMPOSITE` | WARNING | Composite node with one child — can be simplified |
| `PARALLEL_THRESHOLD_UNREACHABLE` | ERROR | Parallel threshold exceeds child count |
| `NO_FALLBACK_BEHAVIOR` | WARNING | No unconditional behaviour — tree may always return FAILURE |
| `PRIORITY_SHADOW` | WARNING | A behaviour can never be reached (unconditional above it) |
| `DEPTH_EXCEEDED` | WARNING | Tree deeper than configured threshold (default: 8) |
| `WIDTH_EXCEEDED` | WARNING | Node has more children than threshold (default: 6) |
| `NODE_COUNT_EXCEEDED` | WARNING | Total nodes exceed threshold (default: 100) |

### Custom thresholds

```cpp
bt::ComplexityAnalyzer::Thresholds limits;
limits.maxDepth      = 12;
limits.maxWidth      = 8;
limits.maxTotalNodes = 200;

auto report = bt::ComplexityAnalyzer::analyze(tree, limits);
```

### Report fields

```cpp
report.totalNodes;          // total node count
report.maxDepth;            // deepest node level
report.maxWidth;            // widest node (most children)
report.avgBranchingFactor;  // average children per composite node
report.hasErrors();         // true if any ERROR-severity issue exists
report.clean();             // true if no issues at all
```

---

## 14. ScenarioRunner — Automated Behaviour Testing

`ScenarioRunner` drives a deterministic tick loop with per-tick hooks and expectations. Use it in unit tests or for automated QA verification.

### Basic usage

```cpp
#include "bt/ScenarioRunner.h"

bt::ScenarioRunner runner(std::move(tree));

// Fire state changes before specific ticks
runner.atTick(3, [&world] { world.enemyVisible = true; });
runner.atTick(7, [&world] { world.health = 10; });   // trigger critical

// Assert which behaviour runs at specific ticks
runner.expect(1, "patrol");     // ticks 1-2: patrolling
runner.expect(2, "patrol");
runner.expect(3, "combat");     // tick 3+: enemy visible → combat
runner.expect(7, "emergency");  // tick 7+: health critical → emergency

auto result = runner.run(10);

for (const auto& step : result.stepResults) {
    std::cout << (step.passed ? "PASS" : "FAIL")
              << "  tick " << step.tick
              << "  expected=" << step.expectedBehavior
              << "  actual="   << step.actualBehavior << "\n";
}

ASSERT_TRUE(result.allPassed);
```

### Chaining hooks and expectations

Both `.atTick()` and `.expect()` return `*this`, so you can chain:

```cpp
runner
    .atTick(1, [&] { world.reset(); })
    .expect(1, "patrol")
    .atTick(5, [&] { world.enemyVisible = true; })
    .expect(5, "combat")
    .expect(6, "combat");
```

### ScenarioResult

```cpp
struct ScenarioResult {
    bool allPassed;
    std::vector<StepResult> stepResults;
    std::vector<TickRecord> tickHistory;   // full emitter history
};
```

---

## 15. TickPool — Multi-Agent Threading

`TickPool` runs multiple independent behaviour trees concurrently — one thread per CPU core, each tree pinned to a single thread.

### Setup

```cpp
#include "bt/TickPool.h"

// Create one tree per NPC
auto tree1 = bt::SchemaLoader::load(yaml, reg, board1);
auto tree2 = bt::SchemaLoader::load(yaml, reg, board2);
auto tree3 = bt::SchemaLoader::load(yaml, reg, board3);

bt::TickPool pool;              // uses hardware_concurrency() threads
// bt::TickPool pool(4);        // explicit thread count

pool.addAgent(tree1);           // assigned to worker 0
pool.addAgent(tree2);           // assigned to worker 1
pool.addAgent(tree3);           // assigned to worker 2 (or wraps round-robin)
```

### Ticking

```cpp
// Ticks ALL agents concurrently, blocks until all complete
std::size_t agentCount = pool.tickAll();
```

### Inspecting the pool

```cpp
pool.size();          // number of registered agents
pool.threadCount();   // number of worker threads
```

### Exception isolation

If a tree's action or condition lambda throws, the exception is caught per agent and stored. Other trees in the pool continue ticking normally:

```cpp
// After tickAll(), check for errors
const auto& errors = pool.lastErrors();
for (const auto& err : errors) {
    try {
        std::rethrow_exception(err.error);
    } catch (const std::exception& exc) {
        std::cerr << "agent error: " << exc.what() << "\n";
        // re-register, reset, or remove the offending tree
    }
}
```

`lastErrors()` returns `const std::vector<TickPool::AgentError>&`. Each entry holds:
- `tree` — pointer to the tree that threw
- `error` — `std::exception_ptr` you can rethrow and inspect

Errors are cleared at the start of every `tickAll()`, so the vector always reflects only the most recent tick round.

### Thread-safety requirements

- Call `addAgent()` for all trees before the first `tickAll()`.
- Do not call `addAgent()` while `tickAll()` is running.
- Blackboard source lambdas are called from worker threads — ensure they are safe to call concurrently (read-only access to game state is typically fine; writes need synchronisation).

---

## 16. C API — Engine Integration

The C API in `include/bt/capi.h` provides an `extern "C"` bridge for game engines that cannot use C++ directly. It exposes the full framework through opaque `bt_handle_t` handles.

### Include

Unity, Unreal, and Godot plugins need only this single header:

```c
#include "bt/capi.h"
```

### Status type

```c
typedef enum {
    BT_SUCCESS = 0,
    BT_FAILURE = 1,
    BT_RUNNING  = 2,
} BtCStatus;
```

### Full workflow

#### 1. Create a registry

```c
bt_handle_t reg = bt_registry_create();
```

#### 2. Register implementations

```c
// Action: receives your context pointer, returns status
bt_registry_add_action(reg, "patrol",
    [](void* ctx) -> BtCStatus {
        MyNPC* npc = (MyNPC*)ctx;
        npc->walkWaypoint();
        return BT_RUNNING;
    },
    myNpcPtr);

// Condition: returns 1 = true, 0 = false
bt_registry_add_condition(reg, "enemy_visible",
    [](void* ctx) -> int {
        MyNPC* npc = (MyNPC*)ctx;
        return npc->canSeeEnemy() ? 1 : 0;
    },
    myNpcPtr);

// Blackboard sources
bt_registry_add_double_source(reg, "health",
    [](void* ctx) -> double {
        return ((MyNPC*)ctx)->health;
    },
    myNpcPtr);

bt_registry_add_bool_source(reg, "is_alert",
    [](void* ctx) -> int {
        return ((MyNPC*)ctx)->isAlert ? 1 : 0;
    },
    myNpcPtr);

// int32 source (e.g. ammo count, wave number)
bt_registry_add_int32_source(reg, "ammo_count",
    [](void* ctx) -> int32_t {
        return ((MyNPC*)ctx)->ammo;
    },
    myNpcPtr);

// String source (e.g. current target tag)
bt_registry_add_string_source(reg, "target_tag",
    [](void* ctx) -> const char* {
        return ((MyNPC*)ctx)->targetTag.c_str();
    },
    myNpcPtr);
```

#### 3. Load the tree

```c
const char* yaml = "..."; // YAML schema string
bt_handle_t tree = bt_tree_load(reg, yaml);

if (tree == NULL) {
    printf("Load failed: %s\n", bt_last_error());
}
```

#### 4. (Optional) Start the live viewer

```c
BtCStatus status = bt_monitor_start(tree, 8080);
// Open http://localhost:8080 in a browser while the game runs
```

#### 5. Tick every frame

```c
// In Update() / per-frame callback
BtCStatus result = bt_tree_tick(tree);
```

#### 6. Read blackboard values

```c
double      health    = bt_tree_get_double(tree, "health");
int         alert     = bt_tree_get_bool(tree,   "is_alert");
int32_t     ammo      = bt_tree_get_int32(tree,  "ammo_count");
const char* tag       = bt_tree_get_string(tree, "target_tag");  // valid until next C API call on this thread
```

#### 7. Shut down

```c
bt_monitor_stop(tree);       // optional — bt_tree_destroy also stops it
bt_tree_destroy(tree);
bt_registry_destroy(reg);
```

### Error handling

Functions that can fail return `NULL` / `BT_FAILURE` and store a description retrievable via `bt_last_error()`. The string is valid until the next C API call on the same thread.

```c
bt_handle_t tree = bt_tree_load(reg, yaml);
if (tree == NULL) {
    fprintf(stderr, "bt_tree_load failed: %s\n", bt_last_error());
}
```

### Unity C# example

```csharp
[DllImport("bt_framework")]
static extern IntPtr bt_registry_create();

[DllImport("bt_framework")]
static extern void bt_registry_add_action(
    IntPtr reg, string name, ActionCallback fn, IntPtr ctx);

[DllImport("bt_framework")]
static extern IntPtr bt_tree_load(IntPtr reg, string yaml);

[DllImport("bt_framework")]
static extern BtCStatus bt_monitor_start(IntPtr tree, int port);

[DllImport("bt_framework")]
static extern BtCStatus bt_tree_tick(IntPtr tree);

[DllImport("bt_framework")]
static extern void bt_tree_destroy(IntPtr tree);

[DllImport("bt_framework")]
static extern void bt_registry_destroy(IntPtr reg);

// In Awake():
IntPtr reg  = bt_registry_create();
bt_registry_add_action(reg, "patrol", OnPatrol, IntPtr.Zero);
IntPtr tree = bt_tree_load(reg, schemaYaml);
bt_monitor_start(tree, 8080);

// In Update():
bt_tree_tick(tree);

// In OnDestroy():
bt_tree_destroy(tree);
bt_registry_destroy(reg);
```

---

## 17. Quick Reference

### Class summary

| Class | Header | Purpose |
|-------|--------|---------|
| `bt::Status` | `Status.h` | Three-value tick result |
| `bt::BehaviorTree` | `BehaviorTree.h` | Owns root, drives tick loop |
| `bt::Blackboard` | `Blackboard.h` | Key-value state store |
| `bt::RuntimeRegistry` | `RuntimeRegistry.h` | Registers actions/conditions with impls + contracts |
| `bt::RegistryStore` | `RegistryStore.h` | SQLite-backed contract persistence |
| `bt::SchemaLoader` | `SchemaLoader.h` | Parses YAML and builds `BehaviorTree` |
| `bt::DecisionEmitter` | `DecisionEmitter.h` | Records structured tick history |
| `bt::MonitorServer` | `MonitorServer.h` | HTTP live tree viewer |
| `bt::EditorServer` | `EditorServer.h` | HTTP visual schema editor |
| `bt::ContractValidator` | `ContractValidator.h` | Validates impls against declared contracts |
| `bt::ComplexityAnalyzer` | `ComplexityAnalyzer.h` | Structural tree analysis |
| `bt::ScenarioRunner` | `ScenarioRunner.h` | Deterministic tick loop for testing |
| `bt::TickPool` | `TickPool.h` | Multi-agent concurrent ticking |
| `bt::Policy` | `Policy.h` | Parallel aggregation policy (ALL/ANY/THRESHOLD) |
| C API | `capi.h` | `extern "C"` bridge for Unity/Unreal/Godot |

### Minimal complete example

```cpp
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"
#include "bt/DecisionEmitter.h"
#include "bt/MonitorServer.h"

static const char* kYaml = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    when: enemy_close
    tree:
      type: action
      name: shoot
  - name: patrol
    tree:
      type: action
      name: wander
)";

int main() {
    bool enemyClose = false;

    bt::RuntimeRegistry reg;

    reg.condition("enemy_close")
        .intent("True when an enemy is within 10 m")
        .impl([&] { return enemyClose; });

    reg.action("shoot")
        .intent("Fire at the nearby enemy")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("wander")
        .intent("Roam the patrol area")
        .impl([] { return bt::Status::RUNNING; });

    auto tree = bt::SchemaLoader::load(kYaml, reg);

    bt::DecisionEmitter emitter;
    tree.setEmitter(&emitter);

    bt::MonitorServer monitor(tree, emitter);
    monitor.start(8080);

    // Game loop
    for (int tick = 0; tick < 20; ++tick) {
        if (tick == 10) { enemyClose = true; }
        tree.tick();
    }

    monitor.stop();
}
```
