#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "bt/BehaviorTree.h"
#include "bt/DecisionEmitter.h"
#include "bt/SchemaLoader.h"
#include "bt/Status.h"
#include "bt/TickPool.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 10 — Thread Management: TickPool
// ═══════════════════════════════════════════════════════════════════════════════

static_assert(!std::is_copy_constructible_v<bt::TickPool>);
static_assert(!std::is_copy_assignable_v<bt::TickPool>);
static_assert(!std::is_move_constructible_v<bt::TickPool>);
static_assert(!std::is_move_assignable_v<bt::TickPool>);

namespace {

bt::BehaviorTree makeTree(const std::string& behaviorName,
                           std::function<bt::Status()> actionImpl) {
    bt::LoaderRegistry reg;
    reg.actions["act"] = std::move(actionImpl);
    std::string yaml =
        "schema_version: \"1.0\"\n"
        "behaviors:\n"
        "  - name: " + behaviorName + "\n"
        "    tree:\n"
        "      type: action\n"
        "      name: act\n";
    return bt::SchemaLoader::load(yaml, reg);
}

}  // namespace

// ───────────────────────────────────────────────────────────────────────────────
// Basic — construction, size, tickAll return value
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase10_Basic, ZeroAgentsTickAllReturnsZero) {
    bt::TickPool pool{1};
    EXPECT_EQ(pool.tickAll(), 0U);
    EXPECT_EQ(pool.size(), 0U);
}

TEST(Phase10_Basic, SizeReflectsAddedAgents) {
    auto tree = makeTree("beh", [] { return bt::Status::SUCCESS; });
    bt::TickPool pool{1};
    pool.addAgent(tree);
    EXPECT_EQ(pool.size(), 1U);
}

TEST(Phase10_Basic, TickAllReturnsAgentCount) {
    auto treeA = makeTree("behA", [] { return bt::Status::SUCCESS; });
    auto treeB = makeTree("behB", [] { return bt::Status::SUCCESS; });
    bt::TickPool pool{2};
    pool.addAgent(treeA);
    pool.addAgent(treeB);
    EXPECT_EQ(pool.tickAll(), 2U);
}

TEST(Phase10_Basic, EachAgentTickedOncePerTickAll) {
    auto tree = makeTree("beh", [] { return bt::Status::SUCCESS; });
    bt::TickPool pool{1};
    pool.addAgent(tree);
    std::ignore = pool.tickAll();
    EXPECT_EQ(tree.tickCount(), 1U);
}

TEST(Phase10_Basic, MultipleRoundsAccumulateTicks) {
    constexpr int kRounds = 7;
    auto tree = makeTree("beh", [] { return bt::Status::SUCCESS; });
    bt::TickPool pool{1};
    pool.addAgent(tree);
    for (int round = 0; round < kRounds; ++round) {
        std::ignore = pool.tickAll();
    }
    EXPECT_EQ(tree.tickCount(), static_cast<std::size_t>(kRounds));
}

// ───────────────────────────────────────────────────────────────────────────────
// Thread count
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase10_ThreadCount, DefaultPoolHasAtLeastOneThread) {
    bt::TickPool pool;
    EXPECT_GE(pool.threadCount(), 1U);
}

TEST(Phase10_ThreadCount, ExplicitThreadCountIsHonoured) {
    bt::TickPool pool{3};
    EXPECT_EQ(pool.threadCount(), 3U);
}

TEST(Phase10_ThreadCount, SingleThreadPool) {
    constexpr int kAgents = 5;
    std::vector<bt::BehaviorTree> trees;
    trees.reserve(kAgents);
    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        trees.push_back(makeTree("beh", [] { return bt::Status::SUCCESS; }));
    }
    {
        bt::TickPool pool{1};
        for (auto& tree : trees) {
            pool.addAgent(tree);
        }
        std::ignore = pool.tickAll();
        std::ignore = pool.tickAll();
    }
    for (const auto& tree : trees) {
        EXPECT_EQ(tree.tickCount(), 2U);
    }
}

// ───────────────────────────────────────────────────────────────────────────────
// Thread affinity — same agent always ticks on the same thread
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase10_ThreadAffinity, SameAgentAlwaysOnSameThread) {
    constexpr int kAgents = 4;
    constexpr int kRounds = 5;
    constexpr int kThreads = 4;

    std::vector<std::vector<std::thread::id>> seenIds(kAgents);

    std::vector<bt::BehaviorTree> trees;
    trees.reserve(kAgents);
    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        trees.push_back(makeTree("beh", [&seenIds, agentIdx] {
            seenIds[agentIdx].push_back(std::this_thread::get_id());
            return bt::Status::SUCCESS;
        }));
    }

    {
        bt::TickPool pool{kThreads};
        for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
            pool.addAgent(trees[agentIdx]);
        }
        for (int round = 0; round < kRounds; ++round) {
            std::ignore = pool.tickAll();
        }
    }

    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        ASSERT_EQ(seenIds[agentIdx].size(), static_cast<std::size_t>(kRounds));
        for (int round = 1; round < kRounds; ++round) {
            EXPECT_EQ(seenIds[agentIdx][round], seenIds[agentIdx][0])
                << "agent " << agentIdx << " changed threads at round " << round;
        }
    }
}

// ───────────────────────────────────────────────────────────────────────────────
// Correctness — tree results are not corrupted by concurrent execution
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase10_Correctness, SuccessAndFailureResultsCorrect) {
    bt::DecisionEmitter emitterA;
    bt::DecisionEmitter emitterB;

    auto treeA = makeTree("success_beh", [] { return bt::Status::SUCCESS; });
    auto treeB = makeTree("failure_beh", [] { return bt::Status::FAILURE; });
    treeA.setEmitter(&emitterA);
    treeB.setEmitter(&emitterB);

    {
        bt::TickPool pool{2};
        pool.addAgent(treeA);
        pool.addAgent(treeB);
        std::ignore = pool.tickAll();
    }

    ASSERT_EQ(emitterA.history().size(), 1U);
    EXPECT_EQ(emitterA.history()[0].result, bt::Status::SUCCESS);

    ASSERT_EQ(emitterB.history().size(), 1U);
    EXPECT_EQ(emitterB.history()[0].result, bt::Status::FAILURE);
}

TEST(Phase10_Correctness, ManyAgentsManyRoundsNoCorruption) {
    constexpr int kAgents = 8;
    constexpr int kRounds = 20;

    std::vector<bt::BehaviorTree> trees;
    trees.reserve(kAgents);
    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        trees.push_back(makeTree("beh", [] { return bt::Status::SUCCESS; }));
    }

    {
        bt::TickPool pool{4};
        for (auto& tree : trees) {
            pool.addAgent(tree);
        }
        for (int round = 0; round < kRounds; ++round) {
            std::ignore = pool.tickAll();
        }
    }

    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        EXPECT_EQ(trees[agentIdx].tickCount(), static_cast<std::size_t>(kRounds))
            << "agent " << agentIdx << " has wrong tick count";
    }
}

// ───────────────────────────────────────────────────────────────────────────────
// Emitter isolation — each agent's emitter records only its own ticks
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase10_Emitter, IsolationPerAgent) {
    constexpr int kAgents = 3;
    constexpr int kRounds = 4;

    std::vector<bt::DecisionEmitter> emitters(kAgents);
    std::vector<bt::BehaviorTree> trees;
    trees.reserve(kAgents);

    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        std::string behName = "beh" + std::to_string(agentIdx);
        trees.push_back(makeTree(behName, [] { return bt::Status::SUCCESS; }));
        trees.back().setEmitter(&emitters[agentIdx]);
    }

    {
        bt::TickPool pool{2};
        for (auto& tree : trees) {
            pool.addAgent(tree);
        }
        for (int round = 0; round < kRounds; ++round) {
            std::ignore = pool.tickAll();
        }
    }

    for (int agentIdx = 0; agentIdx < kAgents; ++agentIdx) {
        const auto& hist = emitters[agentIdx].history();
        ASSERT_EQ(hist.size(), static_cast<std::size_t>(kRounds))
            << "emitter " << agentIdx << " has wrong record count";
        const std::string expected = "beh" + std::to_string(agentIdx);
        for (const auto& rec : hist) {
            EXPECT_EQ(rec.behaviorName, expected);
        }
    }
}
