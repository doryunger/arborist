# Arborist — Build Plan

A game-engine-agnostic Behavior Tree runtime and management framework.
The engine is reduced to a state provider and command executor.

---

## Phase 1 — Runtime (C++)

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

### Implementation steps

| Step | What | Key guarantee |
|------|------|---------------|
| 0 | Build pipeline, GTest smoke test | cmake + ctest green |
| 1 | `Status` enum | All three values, `toString()`, stable underlying ints |
| 2 | `Node` base class | Abstract, polymorphic, non-copyable, `reset()` for interruption |
| 3 | `CompositeNode` + `Sequence` + `Selector` + `treeToString()` | RUNNING resumption, ASCII tree visualization, integration test |
| 4 | `Parallel` node + `Policy` enum | ALL / ANY / THRESHOLD aggregation |
| 5 | Leaf nodes — `Action`, `Condition` | Named callbacks, condition references |
| 6 | `Blackboard` | Key-value store, lambda-backed values |
| 7 | `BehaviorTree` + tick loop | Interruption policy, RUNNING resumption end-to-end |
| 8 | Builder layer | Fluent API, `StateRegistry`, `PriorityResolver`, `TreeAssembler` |
| 9 | Validation layer | Structural checks at registration time |
| 10 | Decision emitter | Per-tick record: behavior evaluated, selected, blackboard snapshot |

### Step 3 detail — Composites + Visualization

**What gets built:**
- `typeName()` pure virtual added to `Node` — each node type names itself
- `children()` virtual accessor on `Node` — returns empty span for leaves, overridden by composites
- `CompositeNode` — owns children via `vector<unique_ptr<Node>>`, tracks `currentChildIndex_` for RUNNING resumption, recursive `reset()`
- `Sequence` — ticks children in order; FAILURE short-circuits and resets; SUCCESS after all children resets
- `Selector` — ticks children in order; SUCCESS short-circuits and resets; FAILURE after all children resets
- `treeToString()` — free function producing ASCII tree visualization

**RUNNING resumption in composites:**
When a child returns `RUNNING`, the composite saves `currentChildIndex_` and returns `RUNNING`.
On the next tick it resumes from that index — not from child 0.
This is verified explicitly: a counting node upstream of the RUNNING node must not be re-ticked on resumption.

**Integration test:**
Build this tree programmatically → verify ASCII output → tick four scenarios:
```
[Selector] root
├── [Sequence] attack_sequence
│   ├── [TestLeaf] enemy_visible
│   └── [TestLeaf] attack
└── [TestLeaf] patrol
```
Scenarios: enemy not visible (patrol runs), enemy visible + attack RUNNING (resumption), attack succeeds, attack fails (patrol fallback).

---

## Phase 2 — Schema Layer

The authoring format. Defines how BTs are written, loaded, and validated.
Evolves alongside Phase 1 until the full pipeline works.

### What gets built

- YAML schema specification — node definitions, tree structure, state variable declarations
- Schema parser — YAML → internal runtime tree
- Schema validator — catches malformed trees, missing references, type mismatches before runtime
- `intent:` annotation field — human-readable description per node (used later by the test harness)
- Subtree composition — ability to reference and reuse subtrees across definitions

### What to get right

Schema design is iterative. Build something that works, run it against real scenarios,
then revise before committing.

The `intent:` annotation is what separates this from a plain serialization format — it captures
what the designer meant, not just what the code does.

### Gaps (resolved)

**Subtree load order** — Single manifest file, topological sort at parse time.
Trees reference each other via explicit `import:` declarations. Cycles are a hard error.
Leaf trees load first. Good error messages identify the cycle.

**Versioning strategy** — Every schema file carries a `schema_version:` field (e.g. `1.0`).
- Major version mismatch → hard error
- Schema minor > runtime minor → warn but load
- Runtime minor > schema minor → silent (backwards compatible)

No migration layer in Phase 2.

**Condition expressiveness** — Named conditions only (Option A, industry standard).
YAML references conditions registered in C++ by name (`when: low_health`).
The schema is a wiring format, not a logic format. Complex conditions belong in C++.
This matches how BehaviorTree.CPP, Unreal Engine, and Unity BT frameworks all work.

---

## Phase 3 — Mock Engine + Simulator

Drives the runtime without a real game engine. Allows full BT scenarios to run headlessly.

---

## Phase 4 — Monitor + Observability

Surfaces the decision emitter output. Live tree state, tick history, blackboard snapshots.

---

## Phase 5 — Test Harness

Scenario-based testing using `intent:` annotations. Validates behavior against designer intent
without requiring the full engine.

---

## Phase 6 — Unity Adapter + Demo Game

Thin adapter that wires Unity state and commands into the framework interfaces.
