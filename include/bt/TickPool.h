#pragma once

#include <cstddef>
#include <memory>

#include "bt/BehaviorTree.h"

namespace bt {

// Distributes a set of BehaviorTree agents across a fixed thread pool and
// ticks all of them concurrently each round.
//
// Each agent is pinned to one worker thread for the pool's lifetime.
// This makes the thread_local tick-ID stamp in the node layer safe — only
// one tree ever ticks on a given thread at a time.
//
// Thread-safety contract on the caller:
//   - addAgent() must complete before the first tickAll() call.
//   - addAgent() calls must not overlap each other or with tickAll().
//   - Blackboard source lambdas registered on each tree must be safe to call
//     from any thread; this is the integration layer's responsibility.
class TickPool {
public:
    // threadCount == 0 uses std::thread::hardware_concurrency() (minimum 1).
    explicit TickPool(std::size_t threadCount = 0);
    ~TickPool();

    TickPool(const TickPool&) = delete;
    TickPool& operator=(const TickPool&) = delete;
    TickPool(TickPool&&) = delete;
    TickPool& operator=(TickPool&&) = delete;

    // Register a tree with the pool. Assigned to a worker thread round-robin;
    // never reassigned. The tree must remain valid for the pool's lifetime.
    void addAgent(BehaviorTree& tree);

    // Tick every registered agent on its assigned worker thread.
    // Blocks until all agents have completed one tick.
    // Returns the number of agents ticked (== size()).
    [[nodiscard]] std::size_t tickAll();

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t threadCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt
