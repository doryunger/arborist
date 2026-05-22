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

**Appendix**
- [A. Ownership and Memory](#a-ownership-and-memory) — `unique_ptr`, `shared_ptr`, move semantics
- [B. Polymorphism and Dispatch](#b-polymorphism-and-dispatch) — `virtual`, pure virtual, NVI, `dynamic_cast`, `override`
- [C. Type Erasure](#c-type-erasure) — `std::any`, `std::function`, `type_index`
- [D. Concurrency Primitives](#d-concurrency-primitives) — `thread_local`, `mutex`, `condition_variable`
- [E. Language Features and Qualifiers](#e-language-features-and-qualifiers) — `[[nodiscard]]`, `noexcept`, `= delete`, `enum class`, `constexpr`
- [F. Standard Types](#f-standard-types) — `optional`, sized integers, `string_view`
- [G. Templates](#g-templates) — `template <typename T>`, instantiation

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

---

## Appendix — C++ Reference

This appendix explains the C++ language features and standard library types used throughout the framework. Each entry describes the concept, how it works, and where it appears in the codebase.

---

### A. Ownership and Memory

#### `std::unique_ptr<T>`

A smart pointer that holds exclusive ownership of a heap-allocated object. When the `unique_ptr` goes out of scope or is destroyed, it automatically deletes the object it owns — no manual `delete` required. This is RAII (Resource Acquisition Is Initialization): the lifetime of the resource is tied to the lifetime of the owning object.

`unique_ptr` cannot be copied — copying would produce two owners for the same object, which would cause a double-free. It can only be *moved*, transferring ownership to a new pointer and leaving the original empty (`nullptr`).

```cpp
auto node = std::make_unique<Sequence>("root");  // allocates, owns
// node is destroyed here — Sequence is deleted automatically
```

In this framework, `unique_ptr` is the exclusive ownership mechanism for all nodes. `CompositeNode` owns its children via `std::vector<std::unique_ptr<Node>>`. The entire node tree is a chain of `unique_ptr` links — destroying the root destroys the whole tree.

#### `std::shared_ptr<T>`

A smart pointer that shares ownership of an object among multiple holders. It maintains a reference count: each copy of the `shared_ptr` increments the count; each destruction decrements it. When the count reaches zero, the object is deleted.

Unlike `unique_ptr`, `shared_ptr` is copyable — the copy shares ownership and increments the count.

```cpp
auto a = std::make_shared<SchemaNode>(...);
auto b = a;  // both a and b own the same SchemaNode; count = 2
// when both go out of scope, count → 0 → SchemaNode is deleted
```

`shared_ptr` is used in `LazySubtree` specifically because `std::function` requires its captured values to be copyable, and `unique_ptr` is not. The schema clone is wrapped in `shared_ptr` so the lambda can be stored in `std::function`. Once materialized, `factory_` is set to `nullptr`, dropping the last reference and freeing the schema.

#### Move semantics and `std::move`

Move semantics allow a resource to be *transferred* from one object to another without copying it. A move leaves the source in a valid but empty state (typically `nullptr` for pointers or an empty container). Moving is cheap — it avoids allocating new memory and copying data.

`std::move` is a cast: it tells the compiler to treat the value as an *rvalue* (a temporary or a value you are done with), enabling the move constructor or move assignment operator.

```cpp
auto root = std::make_unique<Selector>("root");
BehaviorTree bt(std::move(root), ...);
// root is now nullptr; bt owns the Selector
```

Throughout the loader, `std::move` transfers `unique_ptr` ownership into the tree being built. `std::move` on a `Blackboard` or `LoaderRegistry` avoids copying the entire map structure when passing to `BehaviorTree`.

---

### B. Polymorphism and Dispatch

#### `virtual` functions and the vtable

When a function is declared `virtual` in a base class, C++ uses *dynamic dispatch* to call the correct override at runtime. The mechanism is a *vtable* (virtual dispatch table): each class with virtual functions has a hidden table of function pointers. Every object of that class stores a pointer to its class's vtable. When a virtual function is called through a base pointer, the vtable is consulted and the right function is called — regardless of what the compile-time type of the pointer is.

```cpp
Node* n = new Sequence("s");
n->doTick();  // calls Sequence::doTick(), not Node::doTick()
              // decided at runtime via vtable lookup
```

This is what allows the framework to hold all nodes as `Node*` or `unique_ptr<Node>` and call `tick()` without knowing the concrete type. Every `Sequence`, `Selector`, `Parallel`, `Action`, and `Condition` has its own vtable entry for `doTick()`.

The cost of a virtual call is one extra pointer dereference (vtable lookup). For a node tree ticked every frame this is negligible, but it is non-zero — which is why the NVI pattern is used rather than making `tick()` itself virtual.

#### Pure virtual (`= 0`) and abstract classes

A function declared `virtual Status doTick() = 0` is *pure virtual*. The class that declares it becomes *abstract* — it cannot be instantiated directly. Any concrete subclass must provide an implementation, or it is also abstract.

```cpp
class Node {
    virtual Status doTick() = 0;  // pure virtual — Node is abstract
};

class Sequence : public Node {
    Status doTick() override { ... }  // provides the implementation
};
```

`Node` is abstract. You cannot write `Node n;` — the compiler rejects it. You can only instantiate `Sequence`, `Selector`, etc. The pure virtual declaration acts as an interface contract: every node type must implement `doTick()`.

#### The `override` keyword

`override` on a method declaration tells the compiler that this method is intended to override a virtual function from a base class. If the signature does not match any virtual function in the base, it is a compile error — catching mistakes like typos or signature drift.

```cpp
Status doTick() override;   // compile error if Node has no matching virtual
```

Without `override`, a mismatched signature silently creates a *new* function that hides the base version, rather than overriding it — a common source of subtle bugs.

#### Non-Virtual Interface (NVI)

NVI is a design pattern where the public function is non-virtual and the override point is a protected virtual function. The public function calls the virtual one after performing fixed pre/post work.

```cpp
// public, non-virtual — callers use this
Status Node::tick() {
    lastTickId_ = gCurrentTickId;   // always happens
    lastStatus_ = doTick();         // delegates to subclass
    return lastStatus_;
}

// protected, pure virtual — subclasses override this
virtual Status doTick() = 0;
```

If `tick()` were virtual, subclasses would have to remember to call `Node::tick()` at the start of every override to ensure the stamp happens. Forgetting would silently break the tick-ID mechanism. NVI makes the invariant impossible to violate: the stamp is in non-virtual code that the subclass cannot bypass.

#### `dynamic_cast`

`dynamic_cast` performs a safe base-to-derived cast at runtime. It uses the vtable to verify the actual type of the object. If the cast is invalid, it returns `nullptr` (for pointer casts) or throws `std::bad_cast` (for reference casts).

```cpp
Node* n = ...;
Sequence* seq = dynamic_cast<Sequence*>(n);  // nullptr if n is not a Sequence
```

This is the only safe way to cast from a base class pointer to a derived class pointer in C++. `static_cast` performs the same cast without the runtime check — it is undefined behavior if the object is not actually of the target type. This framework bans `static_cast` for base-to-derived casts precisely because a silent miscast is harder to debug than a `nullptr` check.

---

### C. Type Erasure

#### `std::any`

`std::any` is a type-safe container that can hold a value of any copyable type. The stored type is remembered internally; reading it back requires knowing the correct type and using `std::any_cast<T>`. Casting to the wrong type throws `std::bad_any_cast`.

```cpp
std::any val = 42;
int x = std::any_cast<int>(val);   // ok
float y = std::any_cast<float>(val); // throws bad_any_cast
```

The Blackboard uses `std::any` as the value type in its `values_` map. This allows a single `unordered_map<string, any>` to hold integers, floats, booleans, and custom types without the Blackboard needing to know what types will be stored. The tradeoff: type errors are runtime failures, not compile-time failures. The `typeRegistry_` exists to catch mismatches early and produce useful error messages rather than cryptic `bad_any_cast` exceptions.

#### `std::function<F>`

`std::function<F>` is a type-erased callable. It can hold any callable that matches the signature `F`: a free function, a lambda, a method bound with `std::bind`, or a functor. The concrete type of the callable is hidden behind the `std::function` interface.

```cpp
std::function<Status()> action;
action = []() { return Status::SUCCESS; };    // lambda
action = &MyClass::myAction;                  // free function
action();  // calls whatever was stored
```

In the framework, action and condition lambdas are stored as `std::function<Status()>` and `std::function<bool()>`. The `LoaderRegistry` maps names to `std::function` values, allowing the loader to call actions by name without knowing their concrete implementation type.

The key constraint: `std::function` requires the callable to be *copyable*. This is why `LazySubtree` must use `shared_ptr` rather than `unique_ptr` in its captured lambda — `unique_ptr` is not copyable.

#### `std::type_index` and `typeid`

`typeid(T)` returns a `std::type_info` object representing the type `T` at runtime. `std::type_index` wraps `type_info` to make it usable as a map key (it supports `==` and hashing).

```cpp
std::type_index tid(typeid(int));    // represents int
std::type_index tid2(typeid(float)); // represents float
tid == tid2;  // false
```

The Blackboard's `typeRegistry_` maps key names to `type_index` values. The first time a key is written with type `T`, `typeid(T)` is stored. On subsequent writes, the stored `type_index` is compared to `typeid(T)`. If they differ, the access is rejected. This provides runtime type safety without templates in the storage layer.

---

### D. Concurrency Primitives

#### `thread_local`

`thread_local` declares a variable that has a separate instance per thread. Every thread that accesses a `thread_local` variable sees its own copy; writes in one thread are invisible to other threads.

```cpp
thread_local std::uint64_t gCurrentTickId{0};
```

Each `WorkerSlot` thread has its own `gCurrentTickId`. When thread 0 sets its copy to `100` before ticking its agents, thread 1's copy is unaffected. Without `thread_local`, a global would require a mutex, serializing all tick ID updates and becoming a bottleneck.

#### `std::mutex` and `std::lock_guard` / `std::unique_lock`

A `mutex` (mutual exclusion) is a synchronization primitive that only one thread can hold at a time. A thread that tries to acquire a locked mutex blocks until it is released.

`lock_guard` is a RAII wrapper: it locks the mutex on construction and unlocks it on destruction. The lock is always released when the `lock_guard` goes out of scope, even if an exception is thrown.

`unique_lock` is a more flexible RAII wrapper that supports deferred locking, manual unlock, and use with condition variables.

```cpp
{
    std::lock_guard guard(slot->mu);   // locked here
    slot->tickDone = true;
}                                       // unlocked here (guard destroyed)
```

In `TickPool`, mutexes protect the `shouldTick`, `tickDone`, and `stop` flags shared between the main thread and worker threads. `lock_guard` is used for simple flag writes; `unique_lock` is used when waiting on a condition variable (which requires the ability to temporarily release the lock).

#### `std::condition_variable`

A condition variable allows one thread to wait until another thread signals that a condition is true. Waiting releases the associated mutex (allowing other threads to acquire it) and re-acquires it when the thread is woken.

```cpp
// waiting thread
std::unique_lock<std::mutex> lock(slot->mu);
slot->cv.wait(lock, [slot] { return slot->shouldTick || slot->stop; });

// signalling thread
{ std::lock_guard guard(slot->mu); slot->shouldTick = true; }
slot->cv.notify_one();
```

`cv.wait(lock, predicate)` is a *spurious wakeup*-safe form: even if the thread wakes for no reason, it re-evaluates the predicate and goes back to sleep if it is false. In `TickPool`, worker threads sleep on their condition variable until the main thread signals a tick, then signal back when the tick is complete.

---

### E. Language Features and Qualifiers

#### `[[nodiscard]]`

An attribute that causes a compiler warning if the return value of a function is discarded (the call result is not assigned or used).

```cpp
[[nodiscard]] Status tick();
```

`tick()` is `[[nodiscard]]` because ignoring its return value is almost always a bug — the caller likely needs to know whether the tree returned `RUNNING`, `SUCCESS`, or `FAILURE` to decide what to do next. The attribute converts a silent logic error into a visible compiler warning.

#### `noexcept`

A specifier that declares a function will not throw exceptions. The compiler can use this to generate more efficient code (no exception unwinding tables for the function). Calling a `noexcept` function through a `noexcept` call chain guarantees no exceptions propagate.

```cpp
void advanceChildIndex() noexcept { ++currentChildIndex_; }
```

Functions that only increment an integer or compare enum values have no plausible throw path. Marking them `noexcept` documents the intent and allows the compiler to optimize.

#### `= delete`

Explicitly deletes a special member function. Any code that tries to use the deleted function gets a compile error with a clear message, rather than a confusing link error or silent undefined behavior.

```cpp
Node(const Node&) = delete;             // copy constructor deleted
Node& operator=(const Node&) = delete;  // copy assignment deleted
Node(Node&&) = delete;                  // move constructor deleted
Node& operator=(Node&&) = delete;       // move assignment deleted
```

By default, C++ will attempt to synthesize copy and move operations for any class. For `Node`, both are semantically wrong (see section 2). `= delete` makes the intent explicit and surfaces misuse at compile time.

#### `enum class`

A *scoped enumeration*. Enumerators are scoped to the enum type name and do not implicitly convert to integers.

```cpp
enum class Status : std::uint8_t { SUCCESS = 0, FAILURE = 1, RUNNING = 2 };

Status s = Status::SUCCESS;  // must qualify with Status::
int x = s;                   // compile error — no implicit conversion
```

Compare with plain `enum`: `enum Status { SUCCESS, FAILURE, RUNNING }` allows `int x = SUCCESS;` and `if (s == 0)` — both error-prone. `enum class` eliminates accidental integer promotion and comparison with raw values. The `: std::uint8_t` base specifies the underlying storage type explicitly.

#### `constexpr`

Declares that a variable or function can be evaluated at compile time. For variables, it replaces `#define` constants with typed, scoped values. For functions, the compiler may evaluate the function during compilation if given constant arguments.

```cpp
constexpr std::size_t kDefaultLazyThreshold = 10;
```

`#define kDefaultLazyThreshold 10` is a text substitution with no type, no scope, and no debugger visibility. `constexpr` values have a type, obey scope rules, and appear in the debugger. They cannot cause macro-expansion surprises.

---

### F. Standard Types

#### `std::optional<T>`

Holds either a value of type `T`, or nothing (`std::nullopt`). It avoids sentinel values (like `-1`, `SIZE_MAX`, or a `nullptr` pointer) to represent "no value."

```cpp
std::optional<std::size_t> currentBehaviorIndex_;

if (currentBehaviorIndex_.has_value()) {
    std::size_t idx = *currentBehaviorIndex_;  // dereference to get the value
}

currentBehaviorIndex_.reset();  // clear it — back to "no value"
```

`BehaviorTree` uses `optional<size_t>` for `currentBehaviorIndex_` because "no behaviour is currently running" is a valid and meaningful state. A sentinel like `SIZE_MAX` would work, but it requires documentation and defensive checks everywhere. `optional` makes the absence of a value self-documenting and impossible to accidentally interpret as a real index.

#### Sized integer types

`<cstdint>` provides integers with guaranteed sizes: `std::uint8_t` (8-bit unsigned), `std::int32_t` (32-bit signed), `std::uint64_t` (64-bit unsigned), etc.

Plain `int` is implementation-defined in size (typically 32-bit, but not guaranteed). On different platforms or compilers, `int` may be 16 or 64 bits. `uint8_t` is always exactly 8 bits.

`Status` uses `uint8_t` because three states fit in 2 bits — any larger integer type wastes space when Status values are stored in arrays or structs. `lastTickId_` uses `uint64_t` because tick IDs must never wrap around in a long-running session.

#### `std::size_t`

The unsigned integer type returned by `sizeof` and used for sizes and indices into containers. On 64-bit platforms it is typically 64 bits wide. Using `size_t` for loop indices and counts that index into `std::vector` avoids signed/unsigned comparison warnings and matches the type of `vector::size()`.

#### `std::string_view`

A non-owning reference to a sequence of characters. It holds a pointer and a length but owns no memory. It can refer to a `std::string`, a string literal, or any contiguous char sequence without copying.

```cpp
void Node::setName(std::string_view name);  // accepts string, literal, view — no copy
```

Passing `const std::string&` requires the argument to already be a `std::string`. Passing a string literal forces a temporary `std::string` to be constructed. `string_view` accepts both with zero allocation. The caveat: the pointed-to data must outlive the `string_view` — it cannot be stored past the caller's scope without copying into a `std::string`.

---

### G. Templates

#### `template <typename T>`

Templates let a function or class be parameterized by a type. The compiler generates a concrete version for each type used with the template. The template itself is not compiled — only the instantiations are.

```cpp
template <typename T>
void Blackboard::registerSource(const std::string& key, std::function<T()> source) {
    sources_[key] = [src = std::move(source)]() -> std::any { return src(); };
}
```

Calling `registerSource<float>("health", ...)` causes the compiler to generate a version of the function with `T = float`. The lambda inside captures a `std::function<float()>` and wraps it to return `std::any`. The same template called with `T = bool` generates a separate version that wraps a `std::function<bool()>`.

Templates are resolved entirely at compile time. There is no runtime overhead for the dispatch — the correct version of the function is baked in during compilation. The cost is that each instantiation produces separate compiled code, which can increase binary size when many types are used.

In the Blackboard, templates are used at the boundary (registration and retrieval) while the interior storage is type-erased via `std::any`. This gives the caller a typed, safe API without requiring the map itself to be templated.
