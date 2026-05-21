# Arborist — Technical Reference

This document explains the core framework internals: the design decisions behind each component, the data structures, and why the code is shaped the way it is. It focuses on the runtime and loading pipeline. Tooling (editor, monitor, validator, analyzer) is outside scope.

---

## Table of Contents

1. [Status — the three-state contract](#1-status--the-three-state-contract)
2. [Node — the tick protocol](#2-node--the-tick-protocol)
3. [CompositeNode — shared resumption state](#3-compositenode--shared-resumption-state)
4. [Sequence — AND with memory](#4-sequence--and-with-memory)
5. [Selector — OR with memory](#5-selector--or-with-memory)
6. [Parallel — concurrent evaluation](#6-parallel--concurrent-evaluation)
7. [Policy — aggregation logic](#7-policy--aggregation-logic)
8. [Blackboard — consistent world snapshot](#8-blackboard--consistent-world-snapshot)
9. [BehaviorTree — the tick loop](#9-behaviortree--the-tick-loop)
10. [Priority interruption](#10-priority-interruption)
11. [SchemaParser — YAML to data](#11-schemaparser--yaml-to-data)
12. [SchemaLoader — data to node tree](#12-schemaloader--data-to-node-tree)
13. [The assembled tree shape](#13-the-assembled-tree-shape)
14. [LazySubtree — deferred materialization](#14-lazysubtree--deferred-materialization)
15. [TickPool — multi-agent threading](#15-tickpool--multi-agent-threading)

---

## 1. Status — the three-state contract

```cpp
enum class Status : std::uint8_t {
    SUCCESS = 0,
    FAILURE = 1,
    RUNNING = 2,
};
```

Every node tick returns exactly one of these three values. The choice of three states (rather than two) is the foundational design decision of the framework.

**Why not just success/failure?**
A boolean return forces every action to complete within a single tick. Real game actions — animations, pathfinding, projectile travel — take multiple frames. Without RUNNING, the tree would have to poll actions itself, or actions would have to be split across multiple calls with external state tracking. Neither is clean.

RUNNING means: "I am in progress; call me again next frame exactly where you left off." The composite nodes use this to implement *resumption* — skipping nodes that already completed and going directly to the one that was in progress. This is what separates a behaviour tree from a decision tree.

**Why `uint8_t`?**
Three states fit in two bits. `uint8_t` avoids the default `int` storage, keeps the enum tight in vectors and structs, and is unambiguous about size. `enum class` prevents accidental integer promotion or comparison with raw numbers.

---

## 2. Node — the tick protocol

```cpp
// Node.h
class Node {
public:
    [[nodiscard]] Status tick();          // public, non-virtual
protected:
    [[nodiscard]] virtual Status doTick() = 0;  // override point
private:
    std::uint64_t lastTickId_{0};
    Status        lastStatus_{Status::RUNNING};
};

// Node.cpp
thread_local std::uint64_t gCurrentTickId{0};

Status Node::tick() {
    lastTickId_ = gCurrentTickId;
    lastStatus_ = doTick();
    return lastStatus_;
}
```

### The NVI pattern (non-virtual interface)

`tick()` is public and non-virtual. `doTick()` is protected and pure virtual. This is the NVI (Non-Virtual Interface) pattern.

The reason: `tick()` has responsibilities that must happen for *every* node regardless of type — stamping `lastTickId_` with the current tick counter. If `tick()` were virtual, every subclass would have to remember to call `Node::tick()` via `super`, which is fragile. Making it non-virtual enforces the contract: the stamp always happens, then `doTick()` is delegated to the subclass.

### The tick-ID stamp

`lastTickId_` records which tick this node was last visited. After `BehaviorTree::tick()` returns, the framework can walk the full tree and identify exactly which nodes participated in this tick by comparing each node's `lastTickId_` against the current `tickCount_`. Nodes with a stale ID were skipped (inside a branch not reached).

This is how the active path is collected for the monitor viewer — it walks the tree post-tick and gathers only nodes with a matching ID.

### Thread-local tick ID

`gCurrentTickId` is `thread_local`. `BehaviorTree::tick()` calls `Node::setCurrentTickId(tickCount_)` before ticking the root, setting this value on the calling thread. Every node reached in that recursive tick walk reads the same value.

A global variable would require a mutex for `TickPool` (multiple trees ticking concurrently). A thread-local requires no synchronisation and is correct because `TickPool` guarantees exactly one tree ticks on a given thread at a time — so the ID set before a tick is unambiguous for the duration of that tick's call stack.

### Non-copyable, non-movable

```cpp
Node(const Node&) = delete;
Node& operator=(const Node&) = delete;
Node(Node&&) = delete;
Node& operator=(Node&&) = delete;
```

Nodes are owned exclusively by their parent via `unique_ptr`. Copying would duplicate mutable state (`lastTickId_`, `lastStatus_`, child indices) and produce two nodes with shared logical identity. Moving would leave the original in an indeterminate state that a parent might still reference. Neither operation has a valid use case; both are deleted to make misuse a compile error.

---

## 3. CompositeNode — shared resumption state

```cpp
class CompositeNode : public Node {
protected:
    std::size_t currentChildIndex() const noexcept { return currentChildIndex_; }
    void advanceChildIndex() noexcept { ++currentChildIndex_; }
private:
    std::size_t currentChildIndex_{0};
    std::vector<std::unique_ptr<Node>> children_;
};

void CompositeNode::reset() {
    currentChildIndex_ = 0;
    for (const auto& child : children_) { child->reset(); }
}
```

`CompositeNode` is the shared base for `Sequence`, `Selector`, and `Parallel`. It owns children via `unique_ptr<Node>` in a `vector` and maintains `currentChildIndex_` — the index of the child that should be ticked on the next call.

### Why currentChildIndex_ lives in CompositeNode and not subclasses

Both `Sequence` and `Selector` need the same resumption mechanism — skip already-completed children and resume at the child that returned RUNNING. Putting the index in `CompositeNode` means the reset logic (`= 0`, recursive child reset) is written once. Subclasses call `currentChildIndex()` and `advanceChildIndex()` without knowing the storage details.

### reset() is recursive

When `reset()` is called on a composite, it resets its own index and calls `reset()` on every child. Children that are themselves composites propagate the reset further. This guarantees the entire subtree below a reset node is returned to its initial state, regardless of depth.

This matters for interruption: when a higher-priority behaviour becomes eligible, `BehaviorTree::tick()` calls `root_->reset()`, which cascades to every node in the tree. The tree is fully clean for the new behaviour.

---

## 4. Sequence — AND with memory

```cpp
Status Sequence::doTick() {
    while (currentChildIndex() < children().size()) {
        Status status = children()[currentChildIndex()]->tick();
        if (status == Status::RUNNING) { return Status::RUNNING; }
        if (status == Status::FAILURE) { reset(); return Status::FAILURE; }
        advanceChildIndex();
    }
    reset();
    return Status::SUCCESS;
}
```

A Sequence succeeds only when all children succeed, in order. It is the AND operator of a behaviour tree — "do A, then B, then C; if any fails, the whole sequence fails."

### RUNNING resumption

`currentChildIndex_` is the key. When child `N` returns RUNNING:

1. The `while` loop exits immediately, returning RUNNING up the tree.
2. `currentChildIndex_` still holds `N`.
3. On the next tick, the `while` loop starts at `N` again — children 0 through N-1 are skipped entirely.

This is correct BT semantics. Those earlier children already succeeded; re-evaluating them would be wrong. An action that completed should not run a second time just because a sibling is still in progress.

### Why reset() on both terminal outcomes

When the Sequence reaches SUCCESS (all children done) or FAILURE (a child failed):

```cpp
reset();  // before return in both branches
```

`reset()` sets `currentChildIndex_` back to 0 and propagates to children. Without this, the *next* call to this Sequence would start partway through, as though it was in the middle of a previous run. The Sequence must be in a clean state every time it is entered.

### Why failure resets immediately

On FAILURE, the Sequence does not continue ticking remaining children. There is no point — if the precondition for step 3 is that steps 1 and 2 succeeded, and step 2 failed, step 3's outcome is irrelevant. Immediate reset also ensures no child is left in an unexpected RUNNING state after the parent has declared FAILURE.

---

## 5. Selector — OR with memory

```cpp
Status Selector::doTick() {
    while (currentChildIndex() < children().size()) {
        Status status = children()[currentChildIndex()]->tick();
        if (status == Status::RUNNING) { return Status::RUNNING; }
        if (status == Status::SUCCESS) { reset(); return Status::SUCCESS; }
        advanceChildIndex();
    }
    reset();
    return Status::FAILURE;
}
```

Selector is the mirror of Sequence: it is the OR operator. It succeeds as soon as one child succeeds, and fails only when all children fail. The resumption and reset logic are identical — only the terminal conditions are inverted.

The same `currentChildIndex_` mechanism provides RUNNING resumption. If child `N` is in the middle of a RUNNING operation, the Selector resumes at child `N` on the next tick, skipping children 0 through N-1 (which already failed).

Selector is naturally the building block for fallback chains: "try the preferred option; if it fails, fall back to the next; if that fails, fall back further." Each child represents a different strategy with decreasing preference.

---

## 6. Parallel — concurrent evaluation

```cpp
Status Parallel::doTick() {
    std::size_t successCount = 0;
    std::size_t failureCount = 0;
    const std::size_t total = children().size();

    for (const auto& child : children()) {
        Status childStatus = child->tick();
        if (childStatus == Status::SUCCESS) { ++successCount; }
        else if (childStatus == Status::FAILURE) { ++failureCount; }
    }

    if (policy_.satisfied(successCount, failureCount, total)) {
        reset(); return Status::SUCCESS;
    }
    if (policy_.failed(successCount, failureCount, total)) {
        reset(); return Status::FAILURE;
    }
    return Status::RUNNING;
}
```

### Why Parallel does not use currentChildIndex_

Sequence and Selector use `currentChildIndex_` to skip already-completed children and resume the one that was RUNNING. Parallel intentionally ticks *all* children every tick, regardless of their individual status.

The reason is semantics: Parallel models concurrent activities — "move to cover AND report position simultaneously." Both activities should make progress every tick. If Parallel skipped a child that returned SUCCESS last tick, that child would stop receiving ticks while still logically "in progress" from the behaviour's perspective. Ticking all children every frame is the correct model for concurrent execution.

### RUNNING resumption in Parallel children

Individual children of a Parallel can still return RUNNING and benefit from their own internal resumption (if they are themselves Sequences or Selectors). Parallel gives each child a tick every frame; what that child does with the tick is the child's concern.

### Reset semantics

When Parallel reaches a terminal result (via Policy), it calls `reset()`. Unlike Sequence and Selector, where reset is visible in the loop body, Parallel's reset is through the CompositeNode base which resets all children. This is appropriate: all concurrent activities should be aborted together when the Parallel node concludes.

---

## 7. Policy — aggregation logic

```cpp
bool Policy::satisfied(std::size_t successes, std::size_t failures,
                        std::size_t total) const noexcept {
    switch (type_) {
        case Type::ALL:       return successes == total;
        case Type::ANY:       return successes >= 1;
        case Type::THRESHOLD: return successes >= threshold_;
    }
}

bool Policy::failed(std::size_t successes, std::size_t failures,
                     std::size_t total) const noexcept {
    switch (type_) {
        case Type::ALL:       return failures >= 1;
        case Type::ANY:       return failures == total;
        case Type::THRESHOLD: return (total - failures) < threshold_;
    }
}
```

Policy is asked two separate questions each tick: "has the success condition been met?" and "has the failure condition been met?" Both can return false simultaneously — that means the Parallel returns RUNNING.

### Why two separate queries instead of a three-way result

A single function returning SUCCESS/FAILURE/RUNNING would work, but two boolean functions are more composable and testable. Each query answers one clear question. More importantly, the failure condition for THRESHOLD is non-trivial:

```cpp
case Type::THRESHOLD: return (total - failures) < threshold_;
```

"We have failed if the number of children that *could still succeed* is less than the threshold." This is a forward-looking check — it fires as soon as success is mathematically impossible, without waiting for all children to resolve. A merged three-way function would obscure this logic.

### Factory methods instead of a constructor

```cpp
Policy::all()              // clearer than Policy(Type::ALL, 0)
Policy::any()
Policy::threshold(3)
```

The constructor is private. Named factory methods make the intent explicit at the call site and prevent constructing a THRESHOLD policy without a threshold value (which would silently produce threshold=0, meaning success is always reached immediately).

---

## 8. Blackboard — consistent world snapshot

```cpp
template <typename T>
void Blackboard::registerSource(const std::string& key, std::function<T()> source) {
    sources_[key] = [src = std::move(source)]() -> std::any { return src(); };
}

void Blackboard::refresh() {
    for (const auto& [key, source] : sources_) {
        values_[key] = source();
    }
}
```

### Refresh before every tick

`BehaviorTree::tick()` calls `blackboard_.refresh()` as its first action, before any node is ticked. This pulls all registered source lambdas into `values_`.

The consequence: every node that reads from the blackboard during a single tick sees the same snapshot of the world. A node early in the tree and a node deep in a nested subtree both read the health value as it was at the start of this tick, not a changing live value.

Without this, a RUNNING sequence containing two action nodes could observe different health values in the same tick — the first node sees health=50, the second sees health=48 because a damage event fired between ticks. This produces subtle, hard-to-debug behaviour.

### std::any for type erasure

The `sources_` map stores `std::function<std::any()>` — the source lambda is wrapped to return `std::any`. The `values_` map stores `std::any`. This avoids template specialisation throughout the framework; the Blackboard does not need to know what types are stored.

### typeRegistry_ — type enforcement at write time

```cpp
std::unordered_map<std::string, std::type_index> typeRegistry_;

template <typename T>
void enforceType(const std::string& key) {
    const std::type_index tid(typeid(T));
    auto [it, inserted] = typeRegistry_.emplace(key, tid);
    if (!inserted && it->second != tid) {
        throw std::runtime_error(
            "Blackboard type conflict for key '" + key + "'...");
    }
}
```

`set<T>()` and `registerSource<T>()` both call `enforceType<T>()` before writing. The first call for a key registers the type; every subsequent call checks it. This converts a silent runtime failure (a `std::bad_any_cast` from a deep tick call stack with no context) into an immediate, named error at the point of misuse.

`get<T>()` consults `typeRegistry_` before calling `std::any_cast`:

```cpp
const auto regIt = typeRegistry_.find(key);
if (regIt != typeRegistry_.end() &&
    regIt->second != std::type_index(typeid(T))) {
    throw std::runtime_error(
        "Blackboard type mismatch for key '" + key + "'...");
}
```

The check is a map lookup (O(1)) and a `type_index` comparison, both extremely cheap relative to the surrounding tick work. If the key has no registered type (possible if it was populated via a path that pre-dates the type registry, or from external data), the cast proceeds normally.

---

## 9. BehaviorTree — the tick loop

```cpp
Status BehaviorTree::tick() {
    blackboard_.refresh();                           // 1. snapshot world state

    if (!behaviors_.empty()) {
        auto highest = highestPriorityValid();
        if (highest != currentBehaviorIndex_) {
            bool shouldInterrupt = !currentBehaviorIndex_.has_value() ||
                                   behaviors_[*currentBehaviorIndex_].interruptible;
            if (shouldInterrupt) {
                root_->reset();                      // 2. abort current behaviour
                currentBehaviorIndex_ = highest;
                currentBehaviorName_  = ...;
            }
        }
    }

    ++tickCount_;
    Node::setCurrentTickId(tickCount_);
    Status result = root_->tick();                   // 3. tick the tree

    if (emitter_ != nullptr) { ... }                 // 4. record history

    if (result != Status::RUNNING) {
        currentBehaviorIndex_.reset();               // 5. clear on terminal
        currentBehaviorName_.clear();
    }

    return result;
}
```

Every tick follows five steps in this exact order:

1. **Refresh blackboard** — pull all source lambdas into the value store. All nodes see the same snapshot.
2. **Priority check** — evaluate which behaviour should run. If it differs from the current one and the current one is interruptible, reset the tree and switch.
3. **Tick the root** — the recursive tick walk descends the node tree.
4. **Record** — if an emitter is attached, collect the active path and snapshot for the history.
5. **Clear terminal state** — if the tree returned SUCCESS or FAILURE, clear the current behaviour index so the next tick starts a fresh priority evaluation.

### tickCount_ as a monotonically increasing ID

`tickCount_` increments by one each tick and is set as the current tick ID before ticking the root. It serves double duty: the emitter uses it as the record number, and the active path collector uses it to identify which nodes were visited this tick (by comparing `node.lastTickId() == tickCount_`).

Using `tickCount_` directly as the tick ID means there is no separate ID counter to keep in sync, and the ID is always identical to "how many times has this tree been ticked?"

---

## 10. Priority interruption

```cpp
std::optional<std::size_t> BehaviorTree::highestPriorityValid() const {
    for (std::size_t idx = 0; idx < behaviors_.size(); ++idx) {
        const auto& meta = behaviors_[idx];
        if (!meta.condition || meta.condition()) {
            return idx;
        }
    }
    return std::nullopt;
}
```

### Linear scan

`behaviors_` is a `std::vector<BehaviorMeta>` ordered by priority (highest first). `highestPriorityValid()` scans from index 0 and returns the first eligible behaviour. Because:

- The number of top-level behaviours is typically small (4–20).
- Conditions are cheap boolean lambdas reading from the already-refreshed blackboard.
- The scan is O(N) with very low constant factor — it stops at the first match.

A priority queue or sorted structure would add complexity and overhead with no practical benefit.

### Null condition means unconditional

```cpp
if (!meta.condition || meta.condition()) { return idx; }
```

A null `condition` means the behaviour has no gate (`when:` omitted in YAML). It is always eligible. Because the scan stops at the first match, an unconditional behaviour makes all lower-priority behaviours unreachable — which is exactly what the `PRIORITY_SHADOW` analyzer warning detects.

### currentBehaviorIndex_ as std::optional

```cpp
std::optional<std::size_t> currentBehaviorIndex_;
```

`std::optional` cleanly represents "no behaviour is currently running" without a sentinel value like -1 or SIZE_MAX. Checking `!currentBehaviorIndex_.has_value()` is unambiguous. The optional is reset on every terminal tick result so the next tick re-evaluates from scratch.

### The interruptible flag

```cpp
bool shouldInterrupt = !currentBehaviorIndex_.has_value() ||
                       behaviors_[*currentBehaviorIndex_].interruptible;
```

If the currently running behaviour has `interruptible: false`, a higher-priority condition becoming true does not abort it. The tree keeps running the current behaviour to completion. This is needed for atomic sequences — a reload animation or a pickup sequence that would be broken if interrupted mid-way.

The guard `!currentBehaviorIndex_.has_value()` handles the case where no behaviour is currently running (first tick, or previous tick was terminal) — in that case there is nothing to protect, so an interrupt always happens.

---

## 11. SchemaParser — YAML to data

```cpp
SchemaDoc SchemaParser::parse(std::string_view yaml);
```

`SchemaParser` converts a YAML string into a `SchemaDoc` — a plain C++ data structure with no lambdas, no node pointers, and no framework types beyond enums and strings.

```cpp
struct SchemaDoc {
    std::string schemaVersion;
    std::string subtreeName;
    std::vector<std::string> imports;
    std::vector<StateDeclaration> stateDeclarations;
    std::vector<BehaviorSchema> behaviors;
};

struct BehaviorSchema {
    std::string name;
    std::string condition;  // name of gate condition, or ""
    bool interruptible{true};
    std::unique_ptr<SchemaNode> tree;
};

struct SchemaNode {
    SchemaNodeType type;    // ACTION, CONDITION, SEQUENCE, SELECTOR, PARALLEL
    std::string    name;
    SchemaPolicy   policy;
    std::size_t    threshold{1};
    std::vector<std::unique_ptr<SchemaNode>> children;
};
```

### Why two separate phases (parse then load)?

Separating parsing from loading has three benefits:

**1. Testability.** SchemaDoc is inspectable, printable, and constructible without any lambdas. Tests can verify the parsed structure before asking the loader to instantiate nodes.

**2. Reuse.** `PathExplorer` builds multiple trees from the same `SchemaDoc` without re-parsing the YAML. `LazySubtree` captures a deep clone of a `SchemaNode` and builds it later from within a lambda — it cannot hold a `RuntimeRegistry` or live node references, but it can hold a `SchemaNode`.

**3. Decoupling.** `SchemaParser` depends only on `yaml-cpp`. `SchemaLoader` depends on the node classes, `RuntimeRegistry`, and `Blackboard`. Neither depends on the other's dependencies.

### Recursive parseNode

The tree structure in YAML is recursive, so the parser is recursive:

```cpp
std::unique_ptr<SchemaNode> parseNode(const YAML::Node& node) {
    auto typeStr = node["type"].as<std::string>();
    if (typeStr == "action")    { return parseLeaf(ACTION, node); }
    if (typeStr == "sequence")  { /* ... parseChildren ... */ }
    // ...
}

void parseChildren(const YAML::Node& node, SchemaNode& out) {
    for (const auto& child : node["children"]) {
        out.children.push_back(parseNode(child));   // recursive
    }
}
```

Each `SchemaNode` owns its children via `unique_ptr`, forming a tree with automatic lifetime. The root `BehaviorSchema::tree` owns the entire node subtree; destroying the `SchemaDoc` destroys the whole parsed structure.

---

## 12. SchemaLoader — data to node tree

```cpp
// Internal to SchemaLoader.cpp
BehaviorTree buildTree(const SchemaDoc& doc, const LoaderRegistry& reg,
                       Blackboard blackboard, const PartitionConfig& partition) {
    auto root = std::make_unique<Selector>("root");
    std::vector<BehaviorMeta> metas;

    for (const auto& behavior : doc.behaviors) {
        auto condition = resolveCondition(behavior, reg);
        auto seq = std::make_unique<Sequence>(behavior.name);
        if (condition) {
            seq->addChild(std::make_unique<Condition>(behavior.name + "_condition", condition));
        }
        seq->addChild(buildNode(*behavior.tree, reg));   // recursive build
        root->addChild(std::move(seq));
        metas.push_back({behavior.name, condition, behavior.interruptible});
    }

    return BehaviorTree(std::move(root), std::move(blackboard), std::move(metas));
}
```

### LoaderRegistry — the thin interface between loader and registry

```cpp
struct LoaderRegistry {
    std::unordered_map<std::string, std::function<Status()>> actions;
    std::unordered_map<std::string, std::function<bool()>>   conditions;
};
```

`SchemaLoader` only needs lambdas — it does not care about intents, reads, writes, or SQLite. `LoaderRegistry` is a plain struct of two maps. When the caller has a `RuntimeRegistry`, `SchemaLoader` extracts only the lambdas:

```cpp
for (const auto& action : reg.store().allActions()) {
    const auto* func = reg.findAction(action.name);
    if (func != nullptr) { loaderReg.actions[action.name] = *func; }
}
```

This indirection decouples `SchemaLoader` from `RuntimeRegistry`. The C API builds a `LoaderRegistry` directly from function pointers; it does not need to construct a `RuntimeRegistry`. Tests can construct a `LoaderRegistry` inline with toy lambdas without a SQLite database.

### buildNode — recursive node construction

```cpp
std::unique_ptr<Node> buildNode(const SchemaNode& schema, const LoaderRegistry& reg) {
    switch (schema.type) {
        case SchemaNodeType::ACTION:    return buildAction(schema, reg);
        case SchemaNodeType::CONDITION: return buildCondition(schema, reg);
        case SchemaNodeType::SEQUENCE: {
            auto seq = std::make_unique<Sequence>(...);
            for (const auto& child : schema.children) {
                seq->addChild(buildNode(*child, reg));  // recursive
            }
            return seq;
        }
        // ...
    }
}
```

`buildNode` mirrors `parseNode` — where `parseNode` descends YAML nodes producing `SchemaNode` objects, `buildNode` descends `SchemaNode` objects producing live `Node` objects. The recursion terminates at leaves (`ACTION` and `CONDITION`), which have no children.

Unknown action or condition names throw `SchemaLoadError` immediately, so the error is surfaced at load time (startup) rather than silently at the tick where the node is first reached.

---

## 13. The assembled tree shape

Given a schema:

```yaml
behaviors:
  - name: combat
    when: enemy_visible
    tree:
      type: action
      name: shoot
  - name: patrol
    tree:
      type: action
      name: wander
```

`buildTree` produces:

```
Selector("root")
├─ Sequence("combat")
│   ├─ Condition("combat_condition")   ← gate: enemy_visible
│   └─ Action("shoot")
└─ Sequence("patrol")
    └─ Action("wander")                ← no gate: always eligible
```

### Why this shape?

Priority selection is encoded using standard BT combinators — no special `PriorityNode` class needed. The root `Selector` tries children in order, stopping at the first success. Each `Sequence` wraps the gate condition and the behaviour subtree:

- If the gate condition fails → `Condition` returns FAILURE → `Sequence` fails → `Selector` tries next child.
- If the gate condition passes → `Condition` returns SUCCESS → `Sequence` ticks the subtree → result propagates up.
- If no gate → `Sequence` ticks the subtree directly → unconditional fallback.

This is an elegant reduction: priority selection is just a Selector over Sequences, and gating is just prepending a Condition to a Sequence. No new node type is needed. The same mechanism that handles "try A then B" (Selector) handles "run the highest-priority eligible behaviour."

### BehaviorMeta runs in parallel with the tree

The `BehaviorMeta` list (condition lambdas + interruptible flags) is a separate data structure from the node tree. `highestPriorityValid()` evaluates conditions from `BehaviorMeta` without touching the node tree.

This duplication is intentional. The gate condition is stored twice: once as a `Condition` node in the tree (so the node's execution is visible in the active path and the monitor), and once as a raw lambda in `BehaviorMeta` (so interruption can be evaluated before ticking the root, without a partial tick). Without this, detecting that a higher-priority behaviour became eligible would require ticking the tree and then rolling back if an interrupt was needed — which would corrupt RUNNING state.

---

## 14. LazySubtree — deferred materialization

```cpp
class LazySubtree : public Node {
    std::function<std::unique_ptr<Node>()> factory_;
    std::vector<std::unique_ptr<Node>>     children_;

    Status doTick() override {
        if (children_.empty()) {
            children_.push_back(factory_());   // build on first tick
            factory_ = nullptr;                // release the schema clone
        }
        return children_[0]->tick();
    }
};
```

### The problem it solves

A schema with 20 behaviours each containing a 30-node subtree builds 600 nodes at load time. If only 2 behaviours ever run in a given session, 540 nodes are allocated and never ticked.

`LazySubtree` defers building a behaviour's subtree until the first tick that reaches it. The factory lambda captures a deep clone of the `SchemaNode` and a copy of the `LoaderRegistry`:

```cpp
auto cloned = std::shared_ptr<SchemaNode>(behavior.tree->deepClone().release());
subtree = std::make_unique<LazySubtree>(
    behavior.name + "_lazy",
    [schema = std::move(cloned), regCopy = reg]() {
        return buildNode(*schema, regCopy);
    });
```

### Why shared_ptr for the schema clone?

`std::function` requires its captured callable to be copyable. `unique_ptr` is not copyable. `shared_ptr` is, so the lambda can be stored in `std::function`. At materialisation time, the `shared_ptr` is released (by setting `factory_ = nullptr`), dropping the last reference to the schema clone and freeing it.

### Controlled by PartitionConfig

`LazySubtree` is not unconditional. `SchemaLoader` checks `partition.lazyThreshold` and only wraps subtrees that exceed the node-count threshold:

```cpp
if (partition.lazyThreshold > 0 && subtreeNodeCount > partition.lazyThreshold) {
    // wrap in LazySubtree
} else {
    // build eagerly
}
```

This keeps simple schemas fully eager (no lazy overhead) while large schemas benefit from deferred allocation.

---

## 15. TickPool — multi-agent threading

```cpp
struct WorkerSlot {
    std::vector<BehaviorTree*> agents;
    std::mutex              mu;
    std::condition_variable cv;
    bool shouldTick{false};
    bool tickDone{false};
    bool stop{false};
    std::thread thread;
};
```

### One slot per thread, multiple agents per slot

Each `WorkerSlot` owns one `std::thread` and a list of `BehaviorTree*` pointers. Agents are assigned round-robin at `addAgent()` time and never reassigned. A slot with 8 agents ticks all 8 sequentially in its worker loop.

This is thread *affinity* by design. Each agent's data — its blackboard values, node states, tick count — is always accessed from the same thread, keeping it in that CPU's L1/L2 cache across frames.

### The worker loop

```cpp
void runWorker(WorkerSlot* slot) {
    while (true) {
        std::unique_lock<std::mutex> lock(slot->mu);
        slot->cv.wait(lock, [slot] { return slot->shouldTick || slot->stop; });
        if (slot->stop) { return; }
        slot->shouldTick = false;
        lock.unlock();

        for (auto* tree : slot->agents) { tree->tick(); }

        { std::lock_guard guard(slot->mu); slot->tickDone = true; }
        slot->cv.notify_one();
    }
}
```

The worker sleeps on its condition variable until `shouldTick` or `stop` is set. After receiving a signal it immediately releases the lock before ticking its agents — the lock is not held during the tick, so the main thread can check `tickDone` concurrently without contention.

### tickAll — signal all, then wait all

```cpp
std::size_t TickPool::tickAll() {
    // Phase 1: signal all workers
    for (auto& worker : impl_->workers) {
        if (worker->agents.empty()) { continue; }
        { std::lock_guard guard(worker->mu); worker->shouldTick = true; worker->tickDone = false; }
        worker->cv.notify_one();
    }
    // Phase 2: wait for all workers
    for (auto& worker : impl_->workers) {
        if (worker->agents.empty()) { continue; }
        std::unique_lock lock(worker->mu);
        worker->cv.wait(lock, [&worker] { return worker->tickDone; });
    }
    return impl_->agentCount;
}
```

Signalling all workers before waiting for any is a deliberate two-phase approach. If `tickAll` signalled worker 0 and immediately waited for it to finish before signalling worker 1, workers 1..N would be idle while worker 0 ran — no parallelism. The two-phase approach fires all workers simultaneously, then the main thread blocks until the last one finishes.

### Why the thread-local tick ID is safe

`gCurrentTickId` is `thread_local`. Each `WorkerSlot` thread has its own copy. When worker thread 0 calls `tree0->tick()`, it sets its copy of `gCurrentTickId` to `tree0->tickCount_` before descending. Worker thread 1 simultaneously sets its own copy to `tree1->tickCount_`. There is no shared mutable global; each thread's ID is independent.

This is why the affinity guarantee matters: if a tree could hop between threads, a single tick walk might stamp some nodes with thread A's ID and other nodes with thread B's ID. Pinning each tree to one thread eliminates this entirely.

### Shutdown

When `TickPool::~TickPool()` runs (via `Impl`'s destructor), it sets `stop = true` on each slot and notifies the condition variable. The worker loop's predicate wakes up, sees `stop`, and returns, ending the thread. Then `thread.join()` waits for clean exit. This guarantees no worker thread accesses a destroyed `WorkerSlot` after the destructor completes.

### Exception isolation

Without isolation, a single throwing behavior in one agent would unwind the worker thread's stack, skip all remaining agents in the slot, and leave `tickDone` unset — causing `tickAll()` to hang forever waiting on the condition variable.

The worker loop wraps each tree tick in a per-agent try-catch:

```cpp
for (auto* tree : slot->agents) {
    try {
        tree->tick();
    } catch (const std::exception& e) {
        std::lock_guard guard(impl_->errorMu);
        impl_->lastErrors.push_back({ tree, e.what() });
    } catch (...) {
        std::lock_guard guard(impl_->errorMu);
        impl_->lastErrors.push_back({ tree, "unknown exception" });
    }
}
```

Each exception is caught, wrapped in an `AgentError` struct (`{BehaviorTree* agent; std::string message}`), and appended to `lastErrors_` under a mutex. The worker continues to the next agent. At the end of the frame, `tickAll()` returns normally and the caller can inspect errors:

```cpp
pool.tickAll();
for (const auto& err : pool.lastErrors()) {
    log("agent error: " + err.message);
}
```

`lastErrors()` is cleared at the start of each `tickAll()` call, so it always reflects only the most recent frame's failures. An agent that throws once is ticked normally on subsequent frames — the error is reported, not permanent.
