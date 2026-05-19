# Arborist — Behavior Tree Framework

A game-engine-agnostic framework for authoring, executing, monitoring, and automatically testing AI behavior trees. The framework owns the full decision-making pipeline from authoring to analysis; the game engine is reduced to a state provider and command executor.

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

The Blackboard also feeds the decision history. Every tick record includes a full snapshot of the blackboard at the time of decision, so replaying or reviewing AI behavior includes the exact state that drove each choice.

---

## Live Monitoring

Every running tree can be connected to an embedded HTTP server that serves a browser-based viewer. The viewer renders the full tree as a graph and refreshes at two frames per second. After each tick, every node that was evaluated is highlighted in the color corresponding to its result — green for success, red for failure, orange for still running. Nodes not evaluated that tick are grey.

The sidebar shows the last twenty ticks as a scrollable history: tick number, which behavior was active, and the final result. Clicking into a tick record shows the full active path through the tree and the blackboard snapshot at that moment.

The server also exposes two JSON endpoints that external tools can poll: one for the static tree structure and one for the rolling tick history. Any dashboard, logging pipeline, or automated analysis tool can consume these without coupling to the framework's internals.

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

## Auto-Partitioning

When a behavior's subtree grows beyond a configurable node count, Arborist automatically wraps it in a scope boundary at load time. From the outside this is completely transparent — the behavior ticks identically, produces the same results, and appears the same in the YAML. Internally, the scope boundary isolates that subtree for monitoring and testing purposes.

In the live viewer, scope boundaries appear as distinct sections of the graph rather than one undifferentiated mass of nodes. In the path explorer, scoped subtrees can be targeted and tested in isolation. The partition threshold is configurable; the framework applies it silently and reports the partitioning in the analyzer output.

---

## Capabilities

- Author complete multi-priority AI in YAML with no engine coupling
- Trees reload at runtime without restarting the process
- All condition and action implementations live in C++; YAML is pure structure
- Decision history — which behavior ran, which nodes were evaluated, full blackboard snapshot — is recorded for every tick
- Every behavior can be regression-tested without an engine
- Logic errors surface at load time, not during gameplay
- Large trees are automatically partitioned into manageable scopes
- Live browser viewer shows exactly which nodes ran and with what result

---

## Limitations

**Conditions are boolean.** The YAML references named conditions by name; those conditions return true or false. Numeric comparisons, range checks, or multi-variable expressions belong in C++ lambdas. The schema cannot express "fire when health is below 30 and distance is less than 5" directly — that logic must be wrapped in a named condition.

**Static analysis cannot see inside lambdas.** The framework detects structural issues — shadowed branches, empty composites, impossible Parallel policies. It cannot detect a condition whose C++ implementation always returns false, or an action that never returns anything other than Running. Those semantic issues require runtime observation, which is why the fuzz tester exists alongside the static analyzer.

**Exhaustive path exploration scales with condition count.** With twenty or fewer distinct conditions, Arborist can try every possible combination. Beyond twenty conditions, it falls back to random sampling. Very large trees with dozens of distinct conditions may require targeted scenario testing to achieve full behavior coverage.

**Auto-partitioning operates at behavior-subtree granularity.** When a subtree is partitioned, the boundary is placed at the root of that subtree. Deeply nested hot spots within a single behavior's subtree are not recursively subdivided automatically — that is a planned capability.

**RUNNING state belongs to the action.** When an action returns Running across multiple ticks, the framework resumes it correctly, but the action itself is responsible for managing its own in-progress state. The framework does not persist state for actions between ticks — it only preserves the position in the tree.

**No built-in visual editor.** Trees are authored in YAML. A visual graph editor backed by the schema serializer is a planned capability for the engine adapter phase.

---

## Project Structure

```
include/bt/       Public API headers
src/runtime/      Core execution: Node, BehaviorTree, Blackboard, all node types
src/schema/       YAML parsing and tree assembly
src/registry/     SQLite contract store and validator
src/harness/      ScenarioRunner, PathExplorer (automated testing)
src/analysis/     ComplexityAnalyzer, SubtreeScope (logic analysis and partitioning)
src/monitor/      Embedded HTTP server and browser viewer
src/simulator/    MockEngine, headless Simulator
src/builder/      Code-first tree builder API
tests/            Phase test files (phase0 through phase7)
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

Run the included NPC demo and open `http://localhost:8080` to see the live tree viewer:

```bash
./build/examples/bt_demo
```
