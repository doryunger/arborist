# Arborist — Build Plan

A game-engine-agnostic Behavior Tree runtime and management framework.
The engine is reduced to a state provider and command executor.

---

## Status Summary

| Phase | Name | Status |
|---|---|---|
| 1 | Runtime | ✅ Complete |
| 2 | Schema Layer | ✅ Complete |
| 3 | Registry & Spec Layer | ✅ Complete |
| 4 | Mock Engine + Simulator | ✅ Complete |
| 5 | Monitor + Observability | ✅ Complete |
| 6 | Test Harness | ✅ Complete |
| 7 | Logic Analysis + Automated Path Testing | ✅ Complete |
| 8 | Performance | ✅ Complete |
| 9 | Visual Editor | 🔲 Planned |
| 10 | Thread Management | 🔲 Planned |
| 11 | Engine Adapters | 🔲 Planned |

261 tests passing. All phases 1–8 verified by benchmark (200 agents × 3600 ticks).

---

## Phase 1 — Runtime ✅

The execution engine. Defines all types and drives the tick loop.

### What was built

- `Status` enum — `SUCCESS`, `FAILURE`, `RUNNING`
- `Node` base class with non-virtual `tick()` and virtual `doTick()` override point
- `Sequence`, `Selector`, `Parallel` composite nodes; `Action`, `Condition` leaf nodes
- `Parallel` with configurable `Policy` — `ALL`, `ANY`, `THRESHOLD(n)` per node
- `Blackboard` — key-value store, pulls values from registered lambdas before each tick; snapshot is frozen within a tick
- `BehaviorTree` — owns the root node, drives the tick loop, maintains RUNNING resumption state
- `BehaviorBuilder` — fluent API for code-first tree construction
- `TreeAssembler` — assembles a priority-ordered selector tree from registered behaviors
- `DecisionEmitter` — records every tick: behavior name, result, active node path, optional blackboard snapshot
- Validation runs before the first tick; structural errors surface at load time

### Key decisions

`RUNNING` resumption is implemented at the composite level — the tree resumes the active node on the next tick without re-evaluating preconditions from root. Interruption policy is configurable per behavior via `interruptible` flag.

---

## Phase 2 — Schema Layer ✅

The authoring format. Defines how behavior trees are written, loaded, and validated from YAML.

### What was built

- YAML schema specification: node definitions, tree structure, state variable declarations, `schema_version` field
- `SchemaParser` — YAML → `SchemaDoc` (typed intermediate representation)
- `SchemaValidator` — catches malformed trees, missing references, unknown node types before runtime
- `intent:` annotation field per node for human-readable documentation
- Subtree composition — multi-file trees via `import:` declarations with cycle detection
- Topological sort at parse time for correct load order

### Key decisions

Named conditions only — YAML references conditions registered in C++ by name. The schema is a wiring format, not a logic format. `schema_version` field required; major version mismatch is a hard error.

---

## Phase 3 — Registry & Spec Layer ✅

Separates the spec layer (what actions and conditions declare they do) from the logic layer (what they actually do in C++). The name is the bridge between the two.

### What was built

- `ActionSpec`, `ConditionSpec`, `StateKeySpec` data types
- `RegistryStore` — SQLite-backed store; persists declared reads, writes, and intent per item across sessions
- `RuntimeRegistry` — fluent builder API (`action().reads().writes().impl()`) that writes spec to SQLite and keeps the lambda in memory
- `SchemaSerializer` — queries `RegistryStore` → generates a YAML catalog of all declared behaviors for editors and tooling
- `SchemaLoader` extended to accept `RuntimeRegistry` directly

### Architecture

```
LOGIC LAYER  — C++ lambdas in memory (not serializable, lives while process runs)
     ↕ same name key
SPEC LAYER   — SQLite (persisted contracts: reads[], writes[], intent)
     ↕ generated on demand
STRUCTURE    — YAML (tree wiring, generated from SQLite for editors/tooling)
```

The SQLite store is the foundation for the visual editor planned in Phase 9. Because all behaviors, conditions, and blackboard dependencies are declared in structured form here, an editor can read and write the full AI graph without touching source files.

---

## Phase 4 — Mock Engine + Simulator ✅

Drives the runtime without a real game engine. Allows full behavior tree scenarios to run headlessly.

### What was built

- `MockEngine` — configurable state provider and command recorder; state values can be set per-tick from test code
- `Simulator` — drives the tick loop for a fixed number of ticks or until a terminal condition; returns a `SimulatorResult` with full tick history
- Headless execution pipeline — no engine dependency; all conditions and actions are thin stubs

---

## Phase 5 — Monitor + Observability ✅

Surfaces the decision emitter output as a live browser viewer and queryable JSON API.

### What was built

- Embedded HTTP server (`MonitorServer`) with two endpoints: static tree structure and rolling tick history
- Browser-based tree viewer — renders the full node graph, refreshes at 2fps, highlights evaluated nodes green/red/orange per result
- Scrollable tick history sidebar — last N ticks with behavior name and result; clicking a tick shows the active path and blackboard snapshot
- `MonitorQuery` — programmatic API over emitter history; filter by behavior, status, tick range
- Blackboard diff between ticks

---

## Phase 6 — Test Harness ✅

Scenario-based testing. Validates behavior against declared contracts and designer intent without requiring the full engine.

### What was built

- `ScenarioRunner` — executes a behavior tree against a scripted sequence of world state changes; fails immediately on unexpected behavior
- `ContractValidator` — compares declared `reads`/`writes` in `RegistryStore` against observed blackboard accesses at runtime; surfaces drift between spec and implementation
- Regression scenario format — replayable, deterministic; locks in expected behavior sequences as permanent non-regressions

---

## Phase 7 — Logic Analysis + Automated Path Testing ✅

Structural analysis of the live tree and schema-driven automated coverage testing. Both run without an engine.

### What was built

- `ComplexityAnalyzer` — walks the live node tree after load; detects and reports:
  - Empty composites (always fail silently)
  - Single-child composites (structural smell)
  - Parallel nodes with threshold exceeding child count (mathematically unsatisfiable)
  - Priority shadowing (unconditional behavior making all lower behaviors unreachable)
  - Missing fallback behavior (tree returns FAILURE when no condition matches)
  - Depth, width, and node count threshold violations
- `PathExplorer::enumerate()` — exhaustively varies all condition combinations (up to 2²⁰); records which behavior activates under each; identifies unreachable behaviors
- `PathExplorer::fuzz()` — random condition sampling for trees with more than 20 conditions; reports coverage and never-activated behaviors
- `SubtreeScope` — transparent node wrapper; isolates large subtrees for monitoring and testing without changing tick behavior
- Auto-partitioning in `SchemaLoader` — subtrees exceeding `PartitionConfig::maxNodesPerScope` are automatically wrapped in `SubtreeScope` at load time

---

## Phase 8 — Performance ✅

Targeted optimizations validated by a large-scale benchmark. No architectural changes — additive configuration only.

### What was built

- **DecisionEmitter ring buffer** — `explicit DecisionEmitter(std::size_t capacity)` bounds history size; oldest record evicted when full. Eliminates unbounded memory growth (benchmark showed 736MB for 200 agents × 3600 ticks with unbounded history).
- **Opt-in blackboard snapshot capture** — `setCaptureBlackboard(false)` skips the per-tick blackboard copy. Snapshot capture remains on by default for correctness in development; disabled in production builds.
- **`SchemaNode::deepClone()`** — recursive deep copy of the schema tree; prerequisite for lazy instantiation.
- **`LazySubtree`** — defers building a behavior's node tree until the first tick that reaches it. The schema is stored as data; live nodes are materialized on demand. `PartitionConfig::lazyThreshold` wires this into `SchemaLoader`. Benchmark shows lazy partition is measurably faster than eager for agents that don't activate all behaviors.
- **`countSchemaNodes()`** — pre-build node count on the schema tree; used to decide partitioning strategy without building nodes first.
- **Benchmark executable** (`benchmarks/bt_benchmark`) — synthesizes agent populations, ticks them for realistic durations, and reports throughput and memory across 10 configurations.

### Benchmark results (200 agents × 3600 ticks, single-threaded)

| Configuration | Throughput | Memory delta |
|---|---|---|
| Small tree, no emitter | 554K ticks/s | — |
| Medium tree, lazy partition | 305K ticks/s | — |
| Large tree, no emitter | 141K ticks/s | — |
| Large tree, unbounded emitter + full snapshot | 63K ticks/s | **736MB** |
| Large tree, ring buffer cap=20, no snapshot | 53K ticks/s | — |

Best-to-worst spread: 10.7×. Production configuration (ring buffer + snapshot off) sits comfortably above the budget for 500 agents at 10Hz.

---

## Phase 9 — Visual Editor 🔲

A standalone, engine-agnostic graph editor for authoring behavior trees. Backed by the SQLite contract store from Phase 3 and served by the existing embedded HTTP server. No engine dependency.

The editor is the most significant capability gap between the current framework and a tool a designer can use independently. All the underlying data it needs — declared behaviors, conditions, actions, blackboard keys, and tree structure — already exists in structured form in the SQLite store and YAML schema. The editor's job is to provide a visual surface over that data.

### Sub-phase 9A — Editor API

Extend the embedded HTTP server with a REST API for reading and writing the contract store and schema.

- `GET /editor/behaviors` — list all declared behaviors with conditions, priorities, interruptibility
- `GET /editor/actions`, `GET /editor/conditions` — list all declared actions and conditions with intent and declared dependencies
- `GET /editor/blackboard` — list all declared blackboard keys with type and source
- `POST /editor/behavior` — create or update a behavior declaration
- `POST /editor/tree` — save a complete tree schema (overwrites the YAML file for the named tree)
- `GET /editor/analyze` — run `ComplexityAnalyzer` and return the report as JSON

This sub-phase produces no UI. It makes the backend queryable and writeable from any frontend.

### Sub-phase 9B — Graph Renderer

A browser-based read-only graph view of the tree. Renders the node hierarchy as a directed graph. This is a foundation for 9C and 9D — get the rendering right before adding interaction.

- Load tree structure from `GET /editor/behaviors` and existing schema
- Render each behavior as a labeled subgraph; nodes shown with type and name
- Highlight node types visually: composites, conditions, actions distinguishable at a glance
- Show declared intent as a tooltip per node
- Wire into the existing live monitor: active nodes highlighted in real time when a game session is running

### Sub-phase 9C — Authoring Interactions

Turn the read-only graph into an editable surface.

- Drag nodes to reorder children within a composite
- Add and remove nodes from the graph; node type selected from a palette
- Edit behavior metadata: name, condition, priority, interruptibility
- Edit node metadata: name, intent annotation
- Save button writes the result back via `POST /editor/tree` → YAML file on disk
- Undo/redo for all graph mutations

### Sub-phase 9D — Contract Authoring

UI for declaring and editing the contract store — the actions, conditions, and blackboard keys that C++ will implement.

- Create and edit action declarations: name, intent, reads[], writes[]
- Create and edit condition declarations: name, intent, reads[]
- Create and edit blackboard key declarations: name, type, description
- Validate that all names referenced in the tree graph have a matching declaration
- Show warnings inline for undeclared references (designer uses a name the engineer hasn't registered yet)

This sub-phase closes the designer/engineer workflow loop: a designer plans the full AI vocabulary in the editor; an engineer implements only the named items that appear in the contracts.

### Sub-phase 9E — Validation Feedback

Surface `ComplexityAnalyzer` results directly in the graph.

- Errors shown as red overlays on the affected node (empty composite, impossible Parallel threshold)
- Warnings shown as amber overlays (single-child composite, missing fallback)
- Priority shadowing shown as a visual indicator on the shadowed behaviors
- Analyzer runs automatically on every save; results appear without a separate step

---

## Phase 10 — Thread Management 🔲

Make the tick loop safe for use across multiple threads so that engines running AI on worker threads can integrate without per-agent serialization.

The schema, contract store, blackboard source registration, and test harness are already stateless or read-only. Only the tick loop and blackboard snapshot need redesign.

### What gets built

- **Per-tree tick counter** — the tick counter is currently thread-local, which is correct but accidental. Make it an explicit member of `BehaviorTree` so each agent's tick sequence is independent and inspectable.
- **Blackboard thread isolation** — each `BehaviorTree` owns its own `Blackboard` instance. Blackboard sources (the lambdas) must be safe to call from any thread; this is a contract imposed on the integration layer, not enforced by the framework.
- **`TickPool`** — a thin coordinator that distributes a list of agents across a thread pool and ticks each agent on its assigned thread. Agents are never shared between threads within a pool.
- **`DecisionEmitter` thread isolation** — each agent's emitter is owned by that agent's tree and accessed only from its assigned thread. No shared emitter state.
- Benchmark extended to measure multi-threaded throughput and verify linear scaling with thread count.

### What does not change

The schema layer, contract store, path explorer, and scenario runner are all read-only after load. They require no changes and gain thread safety for free.

---

## Phase 11 — Engine Adapters 🔲

Thin integration layers that wire engine state and commands into the framework interfaces. This phase is open-ended — Unity is the first target, but the adapter architecture is designed so that adding Unreal, Godot, or a custom engine later follows the same pattern.

### Adapter contract

An adapter is responsible for exactly three things:
1. Registering blackboard sources — mapping engine state (transform, health, perception) to named lambdas the blackboard pulls from.
2. Registering action and condition implementations — mapping named items in the contract store to engine-side logic.
3. Driving the tick loop — calling `BehaviorTree::tick()` at the correct point in the engine's update cycle, on the correct thread.

Everything above those three points — the tree structure, contracts, analysis, testing, monitoring, editor — is unchanged and engine-agnostic.

### Sub-phase 11A — Unity Adapter

- Native C++ plugin exposing the framework as a Unity NativePlugin
- C# `BehaviorTreeAgent` MonoBehaviour — registers blackboard sources from Unity component state, drives tick from `Update` or a coroutine
- C# fluent API mirroring `RuntimeRegistry` for registering actions and conditions from managed code
- Live monitor integration — the embedded HTTP server runs alongside the Unity Editor; the browser viewer connects to the running game session
- Editor integration — the Phase 9 browser editor runs as a panel inside the Unity Editor window via `WebView`

### Sub-phase 11B — Additional Adapters (open-ended)

Each future adapter follows the same contract as 11A. Planned targets:
- Unreal Engine — C++ plugin, Blueprint-accessible registration API
- Godot — GDExtension plugin
- Custom / headless engine — reference implementation showing the minimal integration surface

---

## Demo Project (separate repository)

A standalone project consuming the framework as a versioned library. Not part of this repository.

Demonstrates: full pipeline end-to-end, multiple agent types sharing one registry, live reload, and the visual editor in use. The demo project is not a correctness test for the framework — that is covered by the 261-test suite in this repository.
