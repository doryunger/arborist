# Arborist — Behavior Tree Framework

Arborist is a game-engine-agnostic C++ framework for building, running, and maintaining AI behavior trees. It covers the full pipeline: behavior authoring, a contract registry that ties designer intent to C++ implementations, a validated runtime with correct RUNNING resumption and configurable interruption, a live browser-based tree viewer, a browser-based visual editor for authoring trees and contracts without touching source files, structural logic analysis that surfaces unreachable branches and impossible policies before the first tick, automated path exploration and scenario testing that requires no engine, and auto-partitioning with lazy subtree instantiation for large trees.

The engine is reduced to a state provider and command executor. Everything above that boundary — what the character does, why, and when — is owned by the framework.

---

## The Problem

Game AI tends to grow in the wrong direction. It starts as a few conditions in a character controller, then expands into nested if-else chains, then into hand-crafted state machines, and eventually into logic so entangled that changing one behavior risks breaking three others. Nobody can tell at a glance which behavior is active, why it was selected, or whether the one underneath it can ever be reached.

Arborist solves this by owning the decision layer entirely. The engine tells the framework what is true about the world. The framework decides what the character does. The engine executes it. That boundary is hard, explicit, and enforced.

---

## How Behavior Trees Work

A Behavior Tree is a tree of nodes. Every tick — once per game frame — the tree is evaluated from the root downward. Each node reports one of three outcomes: **Success** (the goal was achieved), **Failure** (the goal could not be achieved), or **Running** (the goal is in progress, come back next tick).

The tree's structure determines the logic. Three composite node types cover nearly every decision pattern:

**Sequence** — represents "do A, then B, then C." It ticks its children left to right and stops at the first failure. If all children succeed, the Sequence succeeds. Used for anything that requires a series of steps to all go right.

**Selector** — represents "try A, if that fails try B, if that fails try C." It ticks children left to right and stops at the first success. If all children fail, the Selector fails. Used for fallback chains and priority ordering.

**Parallel** — ticks all its children on the same frame regardless of their individual results. The success or failure policy is configurable: require all children to succeed, require any one child to succeed, or require at least N out of the total.

Leaf nodes are either **Actions** (do something, may take multiple ticks) or **Conditions** (check something, return immediately without side effects).

### A Decision in Practice

Consider an NPC guard with three behaviors in priority order: combat, search, and patrol.

Each tick, the framework scans the behavior list from the top. Combat has a condition: is the enemy visible? If yes, the combat subtree runs — perhaps trying a ranged attack first, falling back to melee if ammo is empty. If the enemy is not visible, combat is skipped. Search has a condition: was a noise heard? If yes, the search subtree runs. If neither condition is true, patrol runs unconditionally because it has no condition — it is the fallback.

If combat is running and the enemy disappears, the framework checks on the next tick whether a higher-priority behavior should take over. If combat was marked as interruptible, search or patrol can reclaim control immediately. If combat was marked non-interruptible, it runs to completion first. This entire policy is declared in the schema — no code change required.

### RUNNING and Resumption

When a node returns Running, the framework remembers exactly where it left off. On the next tick it resumes from that node rather than re-evaluating the whole tree from the root. This means a long-running action like "walk to position" does not re-trigger its pre-conditions every frame — the framework simply picks up the walk where it paused.

This is one of the most important correctness guarantees in the framework and is implemented at the composite node level rather than in application code.

---

## Authoring Behaviors

Behaviors are authored in YAML. The YAML is a wiring format — it describes structure and connects names to logic. It does not contain logic itself.

A behavior definition declares its name, its priority among other behaviors, whether it can be interrupted, which condition must be true for it to be eligible, and the subtree of nodes that carry out its goal. A subtree can be as simple as a single action or as deep as a multi-level nested selector with conditions at each branch.

All condition and action names referenced in the YAML must have a registered C++ implementation. If a name appears in the YAML but has no registered implementation, the framework throws at load time — the mismatch is caught before the first tick, not during gameplay.

The schema is validated independently of the runtime. Before loading, Arborist checks for structural problems: missing required fields, unknown node types, and import cycles in multi-file trees. These are authoring errors that surface immediately, not runtime surprises.

---

## The Registry and Contracts

The framework maintains a SQLite database of declared behaviors. Every action and condition is declared with its intended purpose (as a short text description), which blackboard keys it reads, and which it writes. This declaration is the contract between the designer (who writes YAML) and the engineer (who writes C++).

When the runtime starts, a contract validator cross-references these declarations against the registered lambdas. If an action was declared but has no implementation, startup fails loudly. If a condition reads a blackboard key that was never registered as a source, that is reported. The database persists across sessions, so tooling — editors, CI pipelines, code reviewers — can query what behaviors exist and what they are supposed to do without running the game.

The separation between declaration (what this behavior does) and implementation (how it does it) means a designer can plan an AI's full behavior set, including its data dependencies, before a single line of C++ is written.

---

## The Blackboard

The Blackboard is the framework's read-only view of world state. Before each tick, the framework pulls fresh values from all registered sources — these are thin lambdas that read engine state. Every node that runs within the same tick reads the same snapshot; values do not change mid-tick.

This design eliminates an entire class of bugs common in hand-crafted AI: the condition that was true at the start of a frame but false by the time the action reads it. Within a single tick, the world is frozen from the framework's perspective.

The Blackboard also feeds the decision history. Every tick record optionally includes a full snapshot of the blackboard at the time of decision, so replaying or reviewing AI behavior includes the exact state that drove each choice. Snapshot capture is opt-in; in production builds it is disabled to avoid the memory cost described in the performance section below.

The Blackboard enforces type safety at registration time. Once a key is written via `set<T>()` or `registerSource<T>()`, the type is recorded. Any later call for that key with a different type throws `std::runtime_error` immediately, with the key name in the message. `get<T>()` performs the same check before casting, replacing the opaque `std::bad_any_cast` with a clear, actionable diagnostic.

---

## Live Monitoring

Every running tree can be connected to an embedded HTTP server that serves a browser-based viewer. The viewer renders the full tree as a graph and refreshes at two frames per second. After each tick, every node that was evaluated is highlighted in the color corresponding to its result — green for success, red for failure, orange for still running. Nodes not evaluated that tick are grey.

The sidebar shows the last twenty ticks as a scrollable history: tick number, which behavior was active, and the final result. Clicking into a tick record shows the full active path through the tree and the blackboard snapshot at that moment.

The server also exposes two JSON endpoints that external tools can poll: one for the static tree structure and one for the rolling tick history. Any dashboard, logging pipeline, or automated analysis tool can consume these without coupling to the framework's internals.

---

## Visual Editor

A standalone browser-based editor runs on a separate HTTP server (default port 8081) backed entirely by the SQLite contract store. It works independently of any live tree or game runtime — purely from declared contracts and the YAML schema on disk.

**Tree authoring.** The editor renders the behavior tree as an interactive graph. Clicking a node opens an edit panel where its type, name, and intent can be changed. New child nodes can be appended to any composite, nodes can be reordered within their parent, and any node can be deleted. All edits are held in memory until explicitly saved.

**Contract authoring.** The editor provides forms for creating, editing, and deleting actions, conditions, and blackboard keys directly in the SQLite store. Adds and removals are reflected immediately in the editor and in the REST API. A designer can build out the full action/condition vocabulary before any C++ is written.

**Inline validation.** When the tree is loaded, the editor fetches the current analysis report and overlays issue indicators directly on the graph. Nodes with warnings or errors show a colored glow and a badge counting how many issues they carry. Behaviors with issues display a colored dot in the behavior list. This makes structural problems visible without leaving the authoring tool.

**Save to file.** The editor serializes the in-memory tree back to YAML and writes it to the schema file on disk via `POST /api/schema`. The serializer reconstructs valid, properly-indented YAML from the graph state — the round-trip is lossless for all node types the framework supports.

**Hot-reload.** When `EditorServer::attachTree(tree, registry)` is called, every successful schema save automatically rebuilds and hot-swaps the running tree via `BehaviorTree::reload()`. If the new schema references an action not in the registry, the file is saved but the live tree is left unchanged — no crash, no interruption.

**Live tick overlay.** When `EditorServer::attachEmitter(emitter)` is called, the editor graph overlays which nodes ran on the most recent tick: green border for SUCCESS, orange for RUNNING, red for FAILURE. The overlay updates at 500 ms intervals via `GET /api/tickstate`. The re-render is skipped while the user is actively editing to avoid disrupting interaction.

**REST API.** All editor data is accessible as JSON endpoints for external tooling:

| Endpoint | Purpose |
|---|---|
| `GET /api/actions` | All declared actions |
| `GET /api/conditions` | All declared conditions |
| `GET /api/blackboard` | All declared blackboard keys |
| `GET /api/schema` | Current schema YAML |
| `POST /api/schema` | Save updated schema YAML; hot-reloads attached tree |
| `GET /api/tree` | Full tree as structured JSON (node types, IDs, paths, intent) |
| `GET /api/analyze` | Complexity analysis — issues and metrics |
| `GET /api/tickstate` | Latest tick record — tick number, behavior, active node path |
| `PUT /api/actions/:name` | Upsert an action contract |
| `DELETE /api/actions/:name` | Remove an action contract |
| `PUT /api/conditions/:name` | Upsert a condition contract |
| `DELETE /api/conditions/:name` | Remove a condition contract |
| `PUT /api/blackboard/:key` | Upsert a blackboard key declaration |
| `DELETE /api/blackboard/:key` | Remove a blackboard key declaration |

---

## Logic Analysis

As a tree grows it develops problems that are invisible to casual inspection. Arborist includes a structural analyzer that walks the live tree after it is loaded and surfaces these problems before the first tick.

**What it detects:**

A Sequence or Selector with no children will always fail silently on every tick, yet no error will appear at runtime without the analyzer. A Parallel node configured to require five successes but given only three children can never succeed regardless of what the children return — the policy is mathematically impossible to satisfy.

Priority shadowing is the most common silent bug in larger trees. If a behavior with no condition — one that is always eligible — sits at the top of the priority list, every behavior below it is permanently unreachable. The shadowing behavior will fire every tick and the others will never execute. The analyzer detects this and names the shadowed behaviors explicitly.

If all behaviors have conditions and none of them are true at a given moment, the tree returns Failure without executing anything. This is often a design mistake — a tree should have a fallback behavior with no condition as a safety net. The analyzer warns when this safety net is absent.

Beyond correctness checks, the analyzer reports metrics: total node count, maximum depth from root to leaf, and maximum branching factor at any node. These numbers are compared against configurable thresholds and flagged when exceeded, giving teams a concrete signal that a tree has grown to the point where it needs to be split.

The analyzer runs automatically at startup in debug builds and can be triggered on demand at any time. It produces a structured report with severity levels (warning vs. error), the exact path to each affected node, and a plain-language description of each issue.

---

## Automated Testing

Testing AI behavior is normally expensive. A human tester navigates scenarios manually, or an engineer writes brittle scripts that depend on exact implementation details. Arborist replaces both with a framework that generates and runs tests from the schema itself.

### Path Exploration

Given a behavior tree schema, Arborist can enumerate every reachable behavior automatically. It builds a fully mocked version of the tree — all conditions are under the framework's control, all actions return success — then exhaustively varies every condition combination and records which behavior activates under each. The result is a coverage map: a list of behaviors alongside the exact condition state that reaches them.

If a behavior cannot be reached by any combination of conditions, it appears in the unreachable list. This is the same information the logic analyzer produces through static inspection, but derived dynamically — useful for catching cases where C++ condition logic makes a behavior unreachable in ways the static analyzer cannot see.

For large trees with many conditions, exhaustive enumeration is replaced by fuzz testing: the framework randomly samples condition space over thousands of ticks and records coverage. After the run it reports which behaviors were activated, which were never seen despite many attempts, and how many ticks produced no activation at all.

The entire process requires no engine, no test harness setup, and no manually scripted scenarios. The schema and declared contracts are sufficient.

### Scripted Scenarios

For regression testing against specific sequences of events, `ScenarioRunner` drives the tick loop with scripted world state changes. A test describes what world state exists at each tick and what behavior is expected. The runner executes the tree and fails immediately if the expectation is not met.

This is how specific bugs are locked in as non-regressions: reproduce the scenario that triggered the bug, write it as a scripted scenario, confirm the fix, and the scenario will catch any future regression automatically.

---

## Auto-Partitioning and Lazy Loading

When a behavior's subtree grows beyond a configurable node count, Arborist automatically wraps it in a scope boundary at load time. From the outside this is completely transparent — the behavior ticks identically, produces the same results, and appears the same in the YAML. Internally, the scope boundary isolates that subtree for monitoring and testing purposes.

Beyond partitioning, Arborist supports lazy subtree instantiation. When the lazy threshold is configured, large subtrees are not built at load time at all — the schema is stored as data and the live node objects are constructed only the first time evaluation reaches that subtree. This is analogous to demand paging: the full tree exists as a description at all times, but only the parts the current execution path actually reaches are materialized in memory. The benchmark shows this is measurably faster than eager construction in scenarios where many behaviors are never activated in a given session.

---

## Performance

The framework includes a large-scale benchmark that synthesizes agent populations, ticks them for realistic durations, and measures throughput across configurations. Results below are from a single-threaded run of 200 agents × 3600 ticks (720,000 total ticks).

| Scenario | Throughput | Per-tick latency | Memory delta |
|---|---|---|---|
| Small tree, no emitter | 554K ticks/s | 1.8 µs | — |
| Small tree, ring-buffer emitter, no snapshot | 115K ticks/s | 8.6 µs | ~2MB |
| Medium tree, no emitter | 255K ticks/s | 3.9 µs | — |
| Medium tree, lazy partition, no emitter | 305K ticks/s | 3.3 µs | — |
| Large tree, no emitter | 141K ticks/s | 7.1 µs | — |
| Large tree, unbounded emitter, full snapshot | 63K ticks/s | 15.7 µs | **736MB** |
| Large tree, ring-buffer emitter (cap 20), no snapshot | 53K ticks/s | 18.9 µs | — |

**What these numbers mean in practice.** A game with 500 agents ticking at 10Hz needs 5,000 ticks per second. Even the slowest configuration above (53K ticks/s) provides more than 10× headroom for that workload. AI is also never uniform across agents — most games stagger AI updates so that not every agent ticks on every frame, which multiplies the effective budget further.

**The memory trap.** The row that stands out is the unbounded emitter with full blackboard snapshots: 736MB of heap growth over the 20-minute simulation. This is not a theoretical risk — it is what happens in a naive integration if history is left unbounded and snapshot capture is not disabled in production builds. The ring buffer and opt-in snapshot flags exist specifically to prevent this; using them brings memory growth to near zero.

**Where the ceiling is.** At AAA open-world scale — thousands of streaming agents, 60fps, multi-threaded job systems — this framework would hit limits. Each node dispatch goes through a `std::function` call (~5–15ns), the blackboard uses `std::any` for type erasure, and the entire tick loop is single-threaded. These are not accidental constraints; they are design tradeoffs that prioritize correctness and observability. Eliminating them would require a data-oriented rewrite and is tracked as a future direction.

---

## Capabilities

- Author complete multi-priority AI in YAML with no engine coupling
- Trees reload at runtime without restarting the process
- All condition and action implementations live in C++; YAML is pure structure
- Decision history — which behavior ran, which nodes were evaluated, full blackboard snapshot — is recorded for every tick
- Ring-buffer history with configurable capacity prevents unbounded memory growth
- Blackboard snapshot capture is opt-in; disabled in production to eliminate allocation overhead
- Blackboard type safety — key type is fixed at first registration; mismatches throw descriptive errors immediately
- Lazy subtree instantiation defers building node trees until first use
- Every behavior can be regression-tested without an engine
- Logic errors surface at load time, not during gameplay
- Large trees are automatically partitioned into manageable scopes
- Live browser viewer shows exactly which nodes ran and with what result
- Browser-based visual editor for authoring trees and contracts without touching source files
- Visual editor hot-reloads the running tree on every schema save
- Live tick overlay in the visual editor highlights active nodes on each tick
- TickPool per-agent exception isolation — one bad tree cannot crash or stall the pool
- C API supports bool, float, int32, and string blackboard types for Unity/Unreal/Godot integration

---

## Limitations

**Single-threaded.** The tick loop is not thread-safe. In engines that run AI across worker threads, each thread must own a separate tree instance. Agent-to-thread partitioning is the responsibility of the integration layer, not the framework.

**Conditions are boolean.** The YAML references named conditions by name; those conditions return true or false. Numeric comparisons, range checks, or multi-variable expressions belong in C++ lambdas. The schema cannot express "fire when health is below 30 and distance is less than 5" directly — that logic must be wrapped in a named condition.

**Static analysis cannot see inside lambdas.** The framework detects structural issues — shadowed branches, empty composites, impossible Parallel policies. It cannot detect a condition whose C++ implementation always returns false, or an action that never returns anything other than Running. Those semantic issues require runtime observation, which is why the fuzz tester exists alongside the static analyzer.

**Exhaustive path exploration scales with condition count.** With twenty or fewer distinct conditions, Arborist can try every possible combination. Beyond twenty conditions, it falls back to random sampling. Very large trees with dozens of distinct conditions may require targeted scenario testing to achieve full behavior coverage.

**Auto-partitioning operates at behavior-subtree granularity.** When a subtree is partitioned, the boundary is placed at the root of that subtree. Deeply nested hot spots within a single behavior's subtree are not recursively subdivided automatically.

**RUNNING state belongs to the action.** When an action returns Running across multiple ticks, the framework resumes it correctly, but the action itself is responsible for managing its own in-progress state. The framework does not persist state for actions between ticks — it only preserves the position in the tree.

---

## Project Structure

```
include/bt/       Public API headers
src/runtime/      Core execution: Node, BehaviorTree, Blackboard, all node types
src/schema/       YAML parsing, tree assembly, SchemaNode deep clone
src/registry/     SQLite contract store and validator
src/harness/      ScenarioRunner, PathExplorer (automated testing)
src/analysis/     ComplexityAnalyzer, SubtreeScope, LazySubtree
src/monitor/      Live viewer server (port 8080) and visual editor server (port 8081)
src/simulator/    MockEngine, headless Simulator
src/builder/      Code-first tree builder API
tests/            Phase test files (phase0 through phase9)
benchmarks/       Large-scale throughput benchmark (200 agents × 3600 ticks)
examples/         NPC guard demo, pipeline smoke test
docs/             Full design plan and resolved decisions
```

---

## Building

Requires CMake 3.20 or later and a C++20 compiler. Dependencies are fetched automatically.

```bash
cmake -B build
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Run the large-scale benchmark:

```bash
./build/benchmarks/bt_benchmark
# Optional: ./build/benchmarks/bt_benchmark 500 7200  (500 agents, 7200 ticks each)
```

Run the included NPC demo and open `http://localhost:8080` to see the live tree viewer:

```bash
./build/examples/bt_demo
```

To launch the visual editor standalone, instantiate `bt::EditorServer` with a `RegistryStore` and a path to your schema file, then call `start()`. The editor is available at `http://localhost:8081`.
