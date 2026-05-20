#include "bt/TickPool.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace bt {

namespace {

struct WorkerSlot {
    std::vector<BehaviorTree*> agents;
    std::mutex mu;
    std::condition_variable cv;
    bool shouldTick{false};
    bool tickDone{false};
    bool stop{false};
    std::thread thread;
};

void runWorker(WorkerSlot* slot) {
    while (true) {
        std::unique_lock<std::mutex> lock(slot->mu);
        slot->cv.wait(lock, [slot] { return slot->shouldTick || slot->stop; });
        if (slot->stop) { return; }
        slot->shouldTick = false;
        lock.unlock();

        for (auto* tree : slot->agents) {
            tree->tick();
        }

        {
            std::lock_guard<std::mutex> guard(slot->mu);
            slot->tickDone = true;
        }
        slot->cv.notify_one();
    }
}

}  // namespace

struct TickPool::Impl {
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    std::vector<std::unique_ptr<WorkerSlot>> workers;
    std::size_t agentCount{0};
    std::size_t nextWorkerIdx{0};

    explicit Impl(std::size_t threadCount) {
        std::size_t count = threadCount;
        if (count == 0) {
            count = std::thread::hardware_concurrency();
        }
        if (count == 0) {
            count = 1;
        }
        workers.reserve(count);
        for (std::size_t slotIdx = 0; slotIdx < count; ++slotIdx) {
            auto slot = std::make_unique<WorkerSlot>();
            slot->thread = std::thread(runWorker, slot.get());
            workers.push_back(std::move(slot));
        }
    }

    ~Impl() {
        for (auto& worker : workers) {
            {
                std::lock_guard<std::mutex> guard(worker->mu);
                worker->stop = true;
            }
            worker->cv.notify_one();
            worker->thread.join();
        }
    }
};

TickPool::TickPool(std::size_t threadCount)
    : impl_(std::make_unique<Impl>(threadCount)) {}

TickPool::~TickPool() = default;

void TickPool::addAgent(BehaviorTree& tree) {
    auto& slot = *impl_->workers[impl_->nextWorkerIdx % impl_->workers.size()];
    slot.agents.push_back(&tree);
    ++impl_->nextWorkerIdx;
    ++impl_->agentCount;
}

std::size_t TickPool::tickAll() {
    for (auto& worker : impl_->workers) {
        if (worker->agents.empty()) { continue; }
        {
            std::lock_guard<std::mutex> guard(worker->mu);
            worker->shouldTick = true;
            worker->tickDone = false;
        }
        worker->cv.notify_one();
    }

    for (auto& worker : impl_->workers) {
        if (worker->agents.empty()) { continue; }
        std::unique_lock<std::mutex> lock(worker->mu);
        worker->cv.wait(lock, [&worker] { return worker->tickDone; });
    }

    return impl_->agentCount;
}

std::size_t TickPool::size() const noexcept { return impl_->agentCount; }

std::size_t TickPool::threadCount() const noexcept { return impl_->workers.size(); }

}  // namespace bt
