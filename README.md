# Arborist

C++20 behavior tree framework. Author AI in YAML, implement actions/conditions in C++, tick each frame. No engine coupling.

---

## Build

```bash
cmake -B build
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Requires CMake ≥ 3.20 and a C++20 compiler. Dependencies are fetched automatically.

---

## Quick start

**1. Register actions and conditions**

```cpp
#include "bt/LoaderRegistry.h"

bt::LoaderRegistry reg;

reg.registerAction("patrol", [](bt::Blackboard&) {
    // move to next waypoint
    return bt::Status::RUNNING;
});

reg.registerCondition("enemy_visible", [](const bt::Blackboard& bb) {
    return bb.get<bool>("enemy_in_sight");
});
```

**2. Write a schema**

```yaml
behaviors:
  - name: combat
    priority: 1
    condition: enemy_visible
    root:
      type: sequence
      children:
        - type: action
          name: aim
        - type: action
          name: shoot

  - name: patrol
    priority: 2
    root:
      type: action
      name: patrol
```

**3. Load and tick**

```cpp
#include "bt/SchemaLoader.h"

auto tree = bt::SchemaLoader::load("npc.yaml", reg);

// Each frame:
tree.tick();
```

---

## Blackboard

The blackboard is the framework's view of world state. Register sources before ticking; values are snapshotted once per tick so all nodes read the same state within a frame.

```cpp
bt::Blackboard bb;

bb.registerSource<bool>("enemy_in_sight", [&engine] {
    return engine.hasLineOfSight();
});

bb.registerSource<float>("health", [&player] {
    return player.health;
});
```

Types are enforced at registration. Registering the same key with a different type throws `std::runtime_error` immediately, with the key name in the message.

---

## Live tree viewer

```cpp
#include "bt/MonitorServer.h"

bt::MonitorServer monitor(8080);
tree.setEmitter(&monitor.emitter());
monitor.start();
// open http://localhost:8080
```

Shows which nodes ran each tick, with color-coded results. Exposes tick history as JSON at `/api/history`.

---

## Visual editor

```bash
cd editor && ./run.sh
# open http://localhost:8081
```

Browser-based editor for authoring tree schemas and declaring action/condition contracts. Attach a live tree for hot-reload and tick overlay:

```cpp
#include "bt/EditorServer.h"

bt::EditorServer editor(reg, "npc.yaml");
editor.attachTree(&tree, reg);      // save → reload
editor.attachEmitter(&emitter);     // live tick overlay
editor.start();
```

REST API: `GET/POST /api/schema`, `GET /api/analyze`, `GET /api/tickstate`, full CRUD on `/api/actions`, `/api/conditions`, `/api/blackboard`.

---

## Multi-agent

```cpp
#include "bt/TickPool.h"

bt::TickPool pool(4); // 4 threads

for (auto& agent : agents) {
    pool.addAgent(&agent.tree);
}

// Each frame:
pool.tickAll();

for (const auto& err : pool.lastErrors()) {
    log(err.message); // per-agent exception isolation
}
```

Agents are pinned to threads (cache affinity). One throwing agent does not affect others.

---

## C API

For engines without C++ interop (Unity, Godot, etc.):

```c
#include "bt/capi.h"

bt_registry* reg = bt_registry_create();
bt_registry_add_action(reg, "patrol", my_patrol_fn);
bt_registry_add_bool_source(reg, "enemy_visible", my_condition_fn);

bt_tree* tree = bt_tree_load(reg, "npc.yaml");
bt_tree_tick(tree);

bt_tree_destroy(tree);
bt_registry_destroy(reg);
```

Supported blackboard types: `bool`, `float`, `int32`, `string`.

---

## Hot-reload

```cpp
// Swap the running tree between ticks — preserves tick count and emitter
auto next = bt::SchemaLoader::load("npc_v2.yaml", reg);
tree.reload(std::move(next));
```

---

## Logic analysis

```cpp
#include "bt/ComplexityAnalyzer.h"

auto report = bt::ComplexityAnalyzer::analyze(tree);
for (const auto& issue : report.issues) {
    // severity, path, description
    // e.g. PRIORITY_SHADOW, IMPOSSIBLE_PARALLEL_POLICY, EMPTY_COMPOSITE
}
```

Runs automatically at load time in debug builds. Call on demand for CI.

---

## Automated testing

```cpp
#include "bt/PathExplorer.h"
#include "bt/ScenarioRunner.h"

// Enumerate every reachable behavior (exhaustive up to 20 conditions, then fuzz)
auto coverage = bt::PathExplorer::explore(schema, reg);

// Scripted regression scenario
bt::ScenarioRunner runner(tree);
runner.step({{"enemy_in_sight", true}}, bt::Status::RUNNING); // expect combat
runner.step({{"enemy_in_sight", false}}, bt::Status::RUNNING); // expect patrol
runner.run();
```

No engine required — runs entirely against the mock simulator.

---

## Performance

200 agents × 3600 ticks (single-threaded):

| Configuration | Throughput | Per-tick |
|---|---|---|
| Small tree, no emitter | 554K ticks/s | 1.8 µs |
| Medium tree, lazy subtrees | 305K ticks/s | 3.3 µs |
| Large tree, ring-buffer emitter | 53K ticks/s | 18.9 µs |
| Large tree, unbounded emitter + full snapshots | 63K ticks/s | 15.7 µs / **736MB** |

Use `DecisionEmitter` with a ring buffer capacity and opt-in snapshots. Leaving history unbounded with full snapshots enabled will exhaust memory in long sessions.

---

## Project structure

```
include/bt/       Public headers
src/runtime/      BehaviorTree, Blackboard, all node types
src/schema/       YAML parser and tree assembler
src/registry/     SQLite contract store
src/harness/      ScenarioRunner, PathExplorer
src/analysis/     ComplexityAnalyzer, LazySubtree
src/monitor/      MonitorServer (8080), EditorServer (8081)
src/adapter/      C API bridge
tests/            Phase test files (369 tests)
benchmarks/       Large-scale throughput benchmark
examples/         NPC demo
docs/             MANUAL.md, TECHNICAL.md
```

---

## Further reading

- [Manual](docs/MANUAL.md) — full API reference and usage guide
- [Technical reference](docs/TECHNICAL.md) — internals and design decisions
- [Editor README](editor/README.md)
- [Playground README](playground/README.md)
