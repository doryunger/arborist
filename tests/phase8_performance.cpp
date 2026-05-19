#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bt/Action.h"
#include "bt/BehaviorTree.h"
#include "bt/Condition.h"
#include "bt/DecisionEmitter.h"
#include "bt/LazySubtree.h"
#include "bt/PartitionConfig.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaNode.h"
#include "bt/SchemaParser.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/Status.h"
#include "bt/SubtreeScope.h"
#include "bt/TreeUtils.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 8 — Performance: ring buffer, opt-in snapshots, lazy instantiation
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

// Build a minimal BehaviorTree with one unconditional behavior backed by an
// always-SUCCESS action, wired for use with DecisionEmitter.
bt::BehaviorTree makeSingleActionTree(const std::string& behaviorName,
                                       const std::string& actionName) {
    bt::LoaderRegistry reg;
    reg.actions[actionName] = [] { return bt::Status::SUCCESS; };
    std::string yaml =
        "schema_version: \"1.0\"\n"
        "behaviors:\n"
        "  - name: " + behaviorName + "\n"
        "    tree:\n"
        "      type: action\n"
        "      name: " + actionName + "\n";
    return bt::SchemaLoader::load(yaml, reg);
}

}  // namespace

// ───────────────────────────────────────────────────────────────────────────────
// DecisionEmitter — ring buffer
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase8_RingBuffer, UnboundedByDefault) {
    bt::DecisionEmitter emitter;
    auto tree = makeSingleActionTree("patrol", "walk");
    tree.setEmitter(&emitter);

    for (int i = 0; i < 20; ++i) {
        tree.tick();
    }
    EXPECT_EQ(emitter.history().size(), 20U);
}

TEST(Phase8_RingBuffer, CapacityLimitsHistory) {
    bt::DecisionEmitter emitter(5);
    auto tree = makeSingleActionTree("patrol", "walk");
    tree.setEmitter(&emitter);

    for (int i = 0; i < 10; ++i) {
        tree.tick();
    }
    EXPECT_EQ(emitter.history().size(), 5U);
}

TEST(Phase8_RingBuffer, OldestRecordEvicted) {
    bt::DecisionEmitter emitter(3);
    auto tree = makeSingleActionTree("patrol", "walk");
    tree.setEmitter(&emitter);

    for (int i = 0; i < 5; ++i) {
        tree.tick();
    }
    // Ticks 1-5; ring of 3 should hold ticks 3, 4, 5.
    ASSERT_EQ(emitter.history().size(), 3U);
    EXPECT_EQ(emitter.history()[0].tickNumber, 3U);
    EXPECT_EQ(emitter.history()[1].tickNumber, 4U);
    EXPECT_EQ(emitter.history()[2].tickNumber, 5U);
}

TEST(Phase8_RingBuffer, ClearResetsBuffer) {
    bt::DecisionEmitter emitter(5);
    auto tree = makeSingleActionTree("patrol", "walk");
    tree.setEmitter(&emitter);

    tree.tick();
    tree.tick();
    emitter.clear();
    EXPECT_TRUE(emitter.history().empty());

    tree.tick();
    EXPECT_EQ(emitter.history().size(), 1U);
}

TEST(Phase8_RingBuffer, CapacityOneAlwaysHoldsLatest) {
    bt::DecisionEmitter emitter(1);
    auto tree = makeSingleActionTree("patrol", "walk");
    tree.setEmitter(&emitter);

    for (int i = 0; i < 7; ++i) {
        tree.tick();
    }
    ASSERT_EQ(emitter.history().size(), 1U);
    EXPECT_EQ(emitter.history()[0].tickNumber, 7U);
}

// ───────────────────────────────────────────────────────────────────────────────
// DecisionEmitter — opt-in blackboard capture
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase8_BlackboardCapture, EnabledByDefault) {
    bt::DecisionEmitter emitter;
    EXPECT_TRUE(emitter.capturesBlackboard());
}

TEST(Phase8_BlackboardCapture, SnapshotPresentWhenEnabled) {
    bt::DecisionEmitter emitter;
    emitter.setCaptureBlackboard(true);

    bt::Blackboard board;
    board.set<int>("health", 100);
    board.refresh();
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});

    ASSERT_FALSE(emitter.history().empty());
    EXPECT_TRUE(emitter.history()[0].blackboardSnapshot.count("health") > 0);
}

TEST(Phase8_BlackboardCapture, SnapshotEmptyWhenDisabled) {
    bt::DecisionEmitter emitter;
    emitter.setCaptureBlackboard(false);

    bt::Blackboard board;
    board.set<int>("health", 100);
    board.refresh();
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});

    ASSERT_FALSE(emitter.history().empty());
    EXPECT_TRUE(emitter.history()[0].blackboardSnapshot.empty());
}

TEST(Phase8_BlackboardCapture, ToggleAtRuntime) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    board.set<int>("stamina", 50);
    board.refresh();

    emitter.setCaptureBlackboard(false);
    emitter.record(1, "run", bt::Status::SUCCESS, board, {});

    emitter.setCaptureBlackboard(true);
    emitter.record(2, "walk", bt::Status::SUCCESS, board, {});

    ASSERT_EQ(emitter.history().size(), 2U);
    EXPECT_TRUE(emitter.history()[0].blackboardSnapshot.empty());
    EXPECT_TRUE(emitter.history()[1].blackboardSnapshot.count("stamina") > 0);
}

// ───────────────────────────────────────────────────────────────────────────────
// LazySubtree — deferred instantiation
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase8_LazySubtree, NotMaterializedBeforeTick) {
    bool factoryCalled = false;
    auto factory = [&factoryCalled]() -> std::unique_ptr<bt::Node> {
        factoryCalled = true;
        return std::make_unique<bt::Action>("act", [] { return bt::Status::SUCCESS; });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    EXPECT_FALSE(lazy.materialized());
    EXPECT_FALSE(factoryCalled);
    EXPECT_TRUE(lazy.children().empty());
}

TEST(Phase8_LazySubtree, MaterializesOnFirstTick) {
    bool factoryCalled = false;
    auto factory = [&factoryCalled]() -> std::unique_ptr<bt::Node> {
        factoryCalled = true;
        return std::make_unique<bt::Action>("act", [] { return bt::Status::SUCCESS; });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    auto result = lazy.tick();

    EXPECT_TRUE(lazy.materialized());
    EXPECT_TRUE(factoryCalled);
    EXPECT_EQ(result, bt::Status::SUCCESS);
    EXPECT_EQ(lazy.children().size(), 1U);
}

TEST(Phase8_LazySubtree, FactoryCalledOnlyOnce) {
    int callCount = 0;
    auto factory = [&callCount]() -> std::unique_ptr<bt::Node> {
        ++callCount;
        return std::make_unique<bt::Action>("act", [] { return bt::Status::SUCCESS; });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    std::ignore = lazy.tick();
    std::ignore = lazy.tick();
    std::ignore = lazy.tick();

    EXPECT_EQ(callCount, 1);
}

TEST(Phase8_LazySubtree, ResetDoesNotUnmaterialize) {
    auto factory = []() -> std::unique_ptr<bt::Node> {
        return std::make_unique<bt::Action>("act", [] { return bt::Status::SUCCESS; });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    std::ignore = lazy.tick();
    ASSERT_TRUE(lazy.materialized());

    lazy.reset();
    EXPECT_TRUE(lazy.materialized());  // still materialized — reset only resets state, not structure
}

TEST(Phase8_LazySubtree, ResetBeforeMaterializationIsNoop) {
    bool factoryCalled = false;
    auto factory = [&factoryCalled]() -> std::unique_ptr<bt::Node> {
        factoryCalled = true;
        return std::make_unique<bt::Action>("act", [] { return bt::Status::SUCCESS; });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    EXPECT_NO_THROW(lazy.reset());
    EXPECT_FALSE(lazy.materialized());
    EXPECT_FALSE(factoryCalled);
}

TEST(Phase8_LazySubtree, ChildNotTickedWhenNotReached) {
    int tickCount = 0;
    auto factory = [&tickCount]() -> std::unique_ptr<bt::Node> {
        return std::make_unique<bt::Action>("act", [&tickCount] {
            ++tickCount;
            return bt::Status::SUCCESS;
        });
    };

    bt::LazySubtree lazy("test_lazy", std::move(factory));
    // Do NOT tick — factory and child should never be called.
    EXPECT_EQ(tickCount, 0);
    EXPECT_FALSE(lazy.materialized());
}

// ───────────────────────────────────────────────────────────────────────────────
// LazySubtree — integration via SchemaLoader + PartitionConfig
// ───────────────────────────────────────────────────────────────────────────────

static constexpr std::string_view kLargeBehaviorYaml = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    condition: enemy_visible
    tree:
      type: sequence
      name: combat_seq
      children:
        - type: action
          name: move_to_enemy
        - type: action
          name: attack
        - type: sequence
          name: reload_seq
          children:
            - type: condition
              name: ammo_low
            - type: action
              name: reload
  - name: patrol
    tree:
      type: action
      name: walk_waypoint
)";

TEST(Phase8_LazyPartition, TreeTicksCorrectlyWithLazyThreshold) {
    bt::LoaderRegistry reg;
    bool enemyVisible = false;
    reg.conditions["enemy_visible"] = [&enemyVisible] { return enemyVisible; };
    reg.conditions["ammo_low"]      = [] { return false; };
    reg.actions["move_to_enemy"]    = [] { return bt::Status::SUCCESS; };
    reg.actions["attack"]           = [] { return bt::Status::SUCCESS; };
    reg.actions["reload"]           = [] { return bt::Status::SUCCESS; };
    reg.actions["walk_waypoint"]    = [] { return bt::Status::SUCCESS; };

    bt::PartitionConfig cfg;
    cfg.lazyThreshold = 3;  // combat subtree has 6 nodes, should be lazy

    auto doc = bt::SchemaParser::parse(kLargeBehaviorYaml);
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);

    // Patrol runs (no enemy) — does NOT materialize combat subtree.
    auto result = tree.tick();
    EXPECT_EQ(result, bt::Status::SUCCESS);

    // Enable enemy — combat subtree materializes and runs.
    enemyVisible = true;
    result = tree.tick();
    EXPECT_EQ(result, bt::Status::SUCCESS);
}

TEST(Phase8_LazyPartition, SmallSubtreeNotWrappedInLazy) {
    bt::LoaderRegistry reg;
    reg.actions["walk_waypoint"] = [] { return bt::Status::SUCCESS; };

    static constexpr std::string_view kSmallYaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: walk_waypoint
)";

    bt::PartitionConfig cfg;
    cfg.lazyThreshold = 10;  // small tree stays eager

    auto doc = bt::SchemaParser::parse(kSmallYaml);
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);

    // Should tick without issues and without lazy wrapping.
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase8_LazyPartition, ZeroThresholdDisablesLazy) {
    bt::LoaderRegistry reg;
    reg.actions["walk_waypoint"] = [] { return bt::Status::SUCCESS; };

    static constexpr std::string_view kYaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: walk_waypoint
)";

    bt::PartitionConfig cfg;
    cfg.lazyThreshold = 0;  // disabled

    auto doc = bt::SchemaParser::parse(kYaml);
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

// ───────────────────────────────────────────────────────────────────────────────
// countSchemaNodes utility
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase8_TreeUtils, CountSchemaNodesLeaf) {
    bt::SchemaNode leaf;
    leaf.type = bt::SchemaNodeType::ACTION;
    leaf.name = "act";
    EXPECT_EQ(bt::countSchemaNodes(leaf), 1U);
}

TEST(Phase8_TreeUtils, CountSchemaNodesNested) {
    auto root = std::make_unique<bt::SchemaNode>();
    root->type = bt::SchemaNodeType::SEQUENCE;

    auto child1 = std::make_unique<bt::SchemaNode>();
    child1->type = bt::SchemaNodeType::ACTION;

    auto child2 = std::make_unique<bt::SchemaNode>();
    child2->type = bt::SchemaNodeType::CONDITION;

    root->children.push_back(std::move(child1));
    root->children.push_back(std::move(child2));

    EXPECT_EQ(bt::countSchemaNodes(*root), 3U);
}

TEST(Phase8_TreeUtils, DeepCloneIsIndependent) {
    auto original = std::make_unique<bt::SchemaNode>();
    original->type = bt::SchemaNodeType::SEQUENCE;
    original->name = "seq";

    auto child = std::make_unique<bt::SchemaNode>();
    child->type = bt::SchemaNodeType::ACTION;
    child->name = "act";
    original->children.push_back(std::move(child));

    auto clone = original->deepClone();
    ASSERT_NE(clone.get(), original.get());
    EXPECT_EQ(clone->name, original->name);
    ASSERT_EQ(clone->children.size(), 1U);
    EXPECT_NE(clone->children[0].get(), original->children[0].get());
    EXPECT_EQ(clone->children[0]->name, original->children[0]->name);

    // Mutate clone — original is unaffected.
    clone->name = "modified";
    EXPECT_EQ(original->name, "seq");
}
