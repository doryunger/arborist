# Arborist — Build Plan

A game-engine-agnostic Behavior Tree runtime and management framework.
The engine is reduced to a state provider and command executor.

---

## Phase 1 — Runtime (C++) ✅

The execution engine. Defines all types and drives the tick loop.

### What gets built

**Runtime core**
- `Status` enum — `SUCCESS`, `FAILURE`, `RUNNING`
- `Node` base class — all node types inherit from this
- All standard composite, decorator, and leaf node implementations
- `Blackboard` — key-value store, pulls values from registered lambdas before each tick
- `BehaviorTree` — owns the root node, drives the tick loop

**Builder layer**
- `BehaviorBuilder` — fluent API (`behavior().when().onEnter().onTick().onExit()`)
- `StateRegistry` — stores all declared state lambdas
- `PriorityResolver` — given a priority list and current blackboard, determines which behavior is valid
- `TreeAssembler` — takes registered behaviors and builds the internal node tree automatically

**Validation layer**
- Runs at registration time, before the first tick
- Detects unreachable behaviors, missing defaults, conflicting conditions, dead references
- Outputs structured warnings with enough context for the developer to fix the issue

**Decision emitter**
- Records every tick: which behavior was evaluated, which was selected, full blackboard snapshot

### What to get right

`RUNNING` resumption is the most critical correctness requirement. When a behavior returns
`RUNNING`, the framework must resume that behavior on the next tick — not re-evaluate the
full tree from the top.

`Parallel` node aggregation policy is configurable per node via a `Policy` enum:
- `ALL` — all children must succeed
- `ANY` — one success is enough
- `THRESHOLD(n)` — n out of total children must succeed

**Interruption policy** is configurable per behavior via an `interruptible()` flag.
Default is `true`. The runtime checks this flag before switching behaviors — if the current
behavior is non-interruptible it runs to completion before re-evaluating priority.

The validation layer must run before the tick loop starts. A warning that fires on tick 500
is useless. A warning that fires at startup is actionable.

### Gaps (resolved)

- Semantic validation (conditions that can never be true) is deferred to Phase 2 — structural
  checks only in Phase 1.

---

## Phase 2 — Schema Layer ✅

The authoring format. Defines how BTs are written, loaded, and validated.

### What gets built

- YAML schema specification — node definitions, tree structure, state variable declarations
- Schema parser — YAML → internal runtime tree
- Schema validator — catches malformed trees, missing references, type mismatches before runtime
- `intent:` annotation field — human-readable description per node (used later by the test harness)
- Subtree composition — ability to reference and reuse subtrees across definitions

### Gaps (resolved)

**Subtree load order** — Single manifest file, topological sort at parse time.
Trees reference each other via explicit `import:` declarations. Cycles are a hard error.

**Versioning strategy** — Every schema file carries a `schema_version:` field (e.g. `1.0`).
- Major version mismatch → hard error
- Schema minor > runtime minor → warn but load
- Runtime minor > schema minor → silent (backwards compatible)

**Condition expressiveness** — Named conditions only (Option A, industry standard).
YAML references conditions registered in C++ by name (`when: low_health`).
The schema is a wiring format, not a logic format. Complex conditions belong in C++.

---

## Phase 3 — Registry & Spec Layer ✅

Separates the spec layer (what actions/conditions declare they do) from the logic layer
(what they actually do in C++). The name is the bridge between the two.

### Architecture

```
LOGIC LAYER  — C++ lambdas in memory (not serializable, lives while process runs)
     ↕ same name key
SPEC LAYER   — SQLite (persisted contracts: reads[], writes[], intent)
     ↕ generated on demand
STRUCTURE    — YAML (tree wiring, generated from SQLite for editors/tooling)
```

### What gets built

- `RegistrySpec` — `ActionSpec`, `ConditionSpec`, `StateKeySpec` data types
- `RegistryStore` — SQLite-backed store; persists declared reads/writes/intent per item
- `RuntimeRegistry` — fluent builder API (`action().reads().writes().impl()`) that writes
  spec to SQLite on `.impl()` and keeps the lambda in memory
- `SchemaSerializer` — queries `RegistryStore` → generates a YAML registry catalog
  (actions, conditions, state keys with declared dependencies) for editors and tooling
- `SchemaLoader` extended to accept `RuntimeRegistry` directly

### Design decisions

**Contracts are declarations, not proofs.** The developer declares what an action reads and
writes. The framework cannot verify this matches the implementation — that gap is closed by
the test harness in Phase 6. Wrong contracts surface when the observed runtime behavior
diverges from the declared spec.

**SQLite is the source of truth for specs.** YAML is generated from SQLite for tooling use.
The runtime only needs the in-memory lambdas; SQLite is consulted only by tooling.

---

## Phase 4 — Mock Engine + Simulator

Drives the runtime without a real game engine. Allows full BT scenarios to run headlessly.

The `RuntimeRegistry` and `RegistryStore` from Phase 3 make this significantly cleaner:
mock implementations register via the same fluent API as real engine code, and their
declared contracts are stored in SQLite alongside real registrations.

### What gets built

- `MockEngine` — configurable state provider and command recorder
- `Simulator` — drives the tick loop for a fixed number of ticks or until a terminal condition
- Scenario definition format — declarative description of initial state + expected outcomes
- Headless execution pipeline — no engine dependency required

---

## Phase 5 — Monitor + Observability

Surfaces the decision emitter output. Live tree state, tick history, blackboard snapshots.

### What gets built

- Query API over `DecisionEmitter` history — filter by behavior, status, tick range
- Tree state snapshot — which node is currently active, current child index per composite
- Blackboard diff — what changed between two ticks
- Export formats — JSON, YAML for external tooling

---

## Phase 6 — Test Harness

Scenario-based testing using `intent:` annotations and declared contracts from the spec layer.
Validates behavior against designer intent without requiring the full engine.

### What gets built

- Scenario runner — executes a behavior tree against a scripted state sequence
- Intent validator — checks that the selected behavior matches the `intent:` annotation
  for the current state
- **Contract validator** — compares declared `reads`/`writes` in `RegistryStore` against
  observed blackboard accesses at runtime; surfaces drift between spec and implementation
- Regression suite format — replayable scenarios that lock in expected behavior sequences

### Key guarantee

A wrong contract (declared reads/writes that don't match the lambda's actual behavior)
is caught here, not at load time. This is where the spec layer pays off.

---

## Phase 7 — Unity Adapter

Thin adapter that wires Unity state and commands into the framework interfaces.
This completes the framework. The demo game is a separate project (see below).

### What gets built

- Unity `MonoBehaviour` wrappers that register engine state as blackboard sources
- `RuntimeRegistry` integration — register Unity actions and conditions from C# via P/Invoke
  or a native plugin bridge
- Visual editor — Unity Editor window using `GraphView` for drag-and-drop tree authoring,
  backed by `SchemaSerializer` output and saving to YAML

---

## Demo Game (separate repository)

A standalone Unity project that consumes the framework as a library.
Not part of this repository — lives on its own timeline.

### What it demonstrates

- Full pipeline end-to-end: YAML schema → RuntimeRegistry → BehaviorTree → ticking agents
- Multiple agent types each with their own tree, all sharing one RuntimeRegistry
- Live reload — tree structure changes at runtime without restarting
- Visual editor in use — designer edits YAML graph, game reflects changes immediately

### Relationship to this repo

The demo game depends on the framework as a versioned release.
Framework changes do not require the demo game to update immediately.
The demo game is not a correctness test for the framework — that is Phase 6.
