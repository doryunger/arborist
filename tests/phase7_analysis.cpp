#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "bt/Action.h"
#include "bt/BehaviorTree.h"
#include "bt/ComplexityAnalyzer.h"
#include "bt/Condition.h"
#include "bt/Parallel.h"
#include "bt/PathExplorer.h"
#include "bt/Policy.h"
#include "bt/SchemaParser.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/SubtreeScope.h"
#include "bt/SchemaLoader.h"
#include "bt/Status.h"

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

bt::BehaviorTree makeTree(std::unique_ptr<bt::Node> root,
                          std::vector<bt::BehaviorMeta> metas = {}) {
    return bt::BehaviorTree(std::move(root), {}, std::move(metas));
}

bool hasIssueCode(const bt::ComplexityAnalyzer::Report& report,
                  bt::ComplexityAnalyzer::Issue::Code code) {
    for (const auto& issue : report.issues) {
        if (issue.code == code) { return true; }
    }
    return false;
}

bool hasNodeOfType(const bt::Node& root, std::string_view typeName) {
    if (root.typeName() == typeName) { return true; }
    for (const auto& child : root.children()) {
        if (hasNodeOfType(*child, typeName)) { return true; }
    }
    return false;
}

struct ResetTracker : bt::Node {
    int resetCount{0};
    explicit ResetTracker() : Node("tracker") {}
    [[nodiscard]] std::string_view typeName() const noexcept override { return "ResetTracker"; }
    void reset() override { ++resetCount; }
protected:
    [[nodiscard]] bt::Status doTick() override { return bt::Status::SUCCESS; }
};

// ── Phase7_Complexity ─────────────────────────────────────────────────────────

TEST(Phase7_Complexity, MetricsOnFlatTree) {
    // root(Selector) → 3 Action leaves
    auto root = std::make_unique<bt::Selector>("root");
    root->addChild(std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; }));
    root->addChild(std::make_unique<bt::Action>("b", [] { return bt::Status::SUCCESS; }));
    root->addChild(std::make_unique<bt::Action>("c", [] { return bt::Status::SUCCESS; }));
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_EQ(report.totalNodes, 4);  // selector + 3 actions
    EXPECT_EQ(report.maxDepth, 1);    // root at 0, leaves at 1
    EXPECT_EQ(report.maxWidth, 3);
}

TEST(Phase7_Complexity, MetricsOnDeepTree) {
    // chain: Sequence → Sequence → Sequence → Action (depth 3)
    auto inner = std::make_unique<bt::Sequence>("s3");
    inner->addChild(std::make_unique<bt::Action>("leaf", [] { return bt::Status::SUCCESS; }));

    auto mid = std::make_unique<bt::Sequence>("s2");
    mid->addChild(std::move(inner));

    auto root = std::make_unique<bt::Sequence>("s1");
    root->addChild(std::move(mid));
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_EQ(report.maxDepth, 3);
    EXPECT_EQ(report.totalNodes, 4);
}

TEST(Phase7_Complexity, DetectsEmptyComposite) {
    auto root = std::make_unique<bt::Sequence>("empty_seq");
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::EMPTY_COMPOSITE));
    EXPECT_FALSE(report.clean());
    EXPECT_TRUE(report.hasErrors());
}

TEST(Phase7_Complexity, DetectsSingleChildComposite) {
    auto root = std::make_unique<bt::Selector>("sel");
    root->addChild(std::make_unique<bt::Action>("only", [] { return bt::Status::SUCCESS; }));
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::SINGLE_CHILD_COMPOSITE));
}

TEST(Phase7_Complexity, DetectsParallelThresholdUnreachable) {
    // THRESHOLD(5) but only 2 children — can never succeed
    auto root = std::make_unique<bt::Parallel>("par", bt::Policy::threshold(5));
    root->addChild(std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; }));
    root->addChild(std::make_unique<bt::Action>("b", [] { return bt::Status::SUCCESS; }));
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(hasIssueCode(report,
        bt::ComplexityAnalyzer::Issue::Code::PARALLEL_THRESHOLD_UNREACHABLE));
    EXPECT_TRUE(report.hasErrors());
}

TEST(Phase7_Complexity, DetectsNoFallbackBehavior) {
    auto root = std::make_unique<bt::Selector>("root");
    root->addChild(std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; }));

    std::vector<bt::BehaviorMeta> metas;
    metas.push_back(bt::BehaviorMeta{.name = "a", .condition = [] { return true; }});
    auto tree = makeTree(std::move(root), std::move(metas));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::NO_FALLBACK_BEHAVIOR));
}

TEST(Phase7_Complexity, DetectsPriorityShadow) {
    auto root = std::make_unique<bt::Selector>("root");
    root->addChild(std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; }));
    root->addChild(std::make_unique<bt::Action>("b", [] { return bt::Status::SUCCESS; }));

    std::vector<bt::BehaviorMeta> metas;
    // "always" has no condition → always valid → shadows "after"
    metas.push_back(bt::BehaviorMeta{.name = "always", .condition = nullptr});
    metas.push_back(bt::BehaviorMeta{.name = "after",  .condition = [] { return true; }});
    auto tree = makeTree(std::move(root), std::move(metas));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::PRIORITY_SHADOW));
    EXPECT_TRUE(report.hasErrors());
}

TEST(Phase7_Complexity, DetectsDepthThresholdViolation) {
    // Build a chain 6 deep with threshold maxDepth=3
    auto leaf = std::make_unique<bt::Action>("leaf", [] { return bt::Status::SUCCESS; });
    std::unique_ptr<bt::Node> current = std::move(leaf);
    for (int level = 0; level < 5; ++level) {
        auto wrap = std::make_unique<bt::Sequence>("level" + std::to_string(level));
        wrap->addChild(std::move(current));
        current = std::move(wrap);
    }
    auto tree = makeTree(std::move(current));

    bt::ComplexityAnalyzer::Thresholds thresholds;
    thresholds.maxDepth = 3;
    auto report = bt::ComplexityAnalyzer::analyze(tree, thresholds);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::DEPTH_EXCEEDED));
}

TEST(Phase7_Complexity, DetectsWidthThresholdViolation) {
    auto root = std::make_unique<bt::Selector>("root");
    for (int idx = 0; idx < 8; ++idx) {
        root->addChild(std::make_unique<bt::Action>("a" + std::to_string(idx),
            [] { return bt::Status::SUCCESS; }));
    }
    auto tree = makeTree(std::move(root));

    bt::ComplexityAnalyzer::Thresholds thresholds;
    thresholds.maxWidth = 4;
    auto report = bt::ComplexityAnalyzer::analyze(tree, thresholds);
    EXPECT_TRUE(hasIssueCode(report, bt::ComplexityAnalyzer::Issue::Code::WIDTH_EXCEEDED));
}

TEST(Phase7_Complexity, CleanTreeHasNoIssues) {
    auto root = std::make_unique<bt::Selector>("root");
    auto seq  = std::make_unique<bt::Sequence>("combat");
    seq->addChild(std::make_unique<bt::Condition>("enemy_visible", [] { return true; }));
    seq->addChild(std::make_unique<bt::Action>("attack", [] { return bt::Status::SUCCESS; }));
    root->addChild(std::move(seq));
    root->addChild(std::make_unique<bt::Action>("patrol", [] { return bt::Status::RUNNING; }));

    std::vector<bt::BehaviorMeta> metas;
    metas.push_back(bt::BehaviorMeta{.name = "combat", .condition = [] { return true; }});
    metas.push_back(bt::BehaviorMeta{.name = "patrol", .condition = nullptr});
    auto tree = makeTree(std::move(root), std::move(metas));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    EXPECT_TRUE(report.clean());
    EXPECT_FALSE(report.hasErrors());
    EXPECT_FALSE(report.summary().empty());
}

TEST(Phase7_Complexity, SummaryContainsMetrics) {
    auto root = std::make_unique<bt::Sequence>("root");
    root->addChild(std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; }));
    auto tree = makeTree(std::move(root));

    auto report = bt::ComplexityAnalyzer::analyze(tree);
    auto summary = report.summary();
    EXPECT_NE(summary.find("nodes"), std::string::npos);
    EXPECT_NE(summary.find("depth"), std::string::npos);
}

// ── Phase7_SubtreeScope ───────────────────────────────────────────────────────

TEST(Phase7_SubtreeScope, TypeNameIsSubtreeScope) {
    auto action = std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; });
    bt::SubtreeScope scope("s", std::move(action));
    EXPECT_EQ(scope.typeName(), "SubtreeScope");
}

TEST(Phase7_SubtreeScope, TickReturnsSameAsChild) {
    auto action = std::make_unique<bt::Action>("a", [] { return bt::Status::SUCCESS; });
    bt::SubtreeScope scope("s", std::move(action));
    EXPECT_EQ(scope.tick(), bt::Status::SUCCESS);
}

TEST(Phase7_SubtreeScope, TickPropagatesFailure) {
    auto action = std::make_unique<bt::Action>("a", [] { return bt::Status::FAILURE; });
    bt::SubtreeScope scope("s", std::move(action));
    EXPECT_EQ(scope.tick(), bt::Status::FAILURE);
}

TEST(Phase7_SubtreeScope, TickPropagatesRunning) {
    auto action = std::make_unique<bt::Action>("a", [] { return bt::Status::RUNNING; });
    bt::SubtreeScope scope("s", std::move(action));
    EXPECT_EQ(scope.tick(), bt::Status::RUNNING);
}

TEST(Phase7_SubtreeScope, ChildVisibleViaChildrenSpan) {
    auto action = std::make_unique<bt::Action>("inner", [] { return bt::Status::SUCCESS; });
    bt::SubtreeScope scope("s", std::move(action));
    auto kids = scope.children();
    ASSERT_EQ(kids.size(), 1);
    EXPECT_EQ(kids[0]->name(), "inner");
}

TEST(Phase7_SubtreeScope, ResetPropagatesInward) {
    auto tracker = std::make_unique<ResetTracker>();
    auto* raw    = tracker.get();
    bt::SubtreeScope scope("s", std::move(tracker));
    scope.reset();
    EXPECT_EQ(raw->resetCount, 1);
}

// ── Phase7_PathExplorer ───────────────────────────────────────────────────────

static constexpr auto kThreeBehaviorYaml = R"yaml(
schema_version: "1.0"
behaviors:
  - name: combat
    when: enemy_visible
    tree:
      type: action
      name: attack
  - name: search
    when: heard_noise
    tree:
      type: action
      name: investigate
  - name: patrol
    tree:
      type: action
      name: wander
)yaml";

static constexpr auto kShadowedYaml = R"yaml(
schema_version: "1.0"
behaviors:
  - name: always_fires
    tree:
      type: action
      name: action_a
  - name: shadowed
    when: some_condition
    tree:
      type: action
      name: action_b
)yaml";

static constexpr auto kSingleBehaviorYaml = R"yaml(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)yaml";

TEST(Phase7_PathExplorer, EnumeratesAllThreeBehaviors) {
    auto schema = bt::SchemaParser::parse(kThreeBehaviorYaml);
    auto paths  = bt::PathExplorer::enumerate(schema);

    std::set<std::string> names;
    for (const auto& path : paths) {
        names.insert(path.activatedBehavior);
    }
    EXPECT_EQ(names.size(), 3);
    EXPECT_TRUE(names.contains("combat"));
    EXPECT_TRUE(names.contains("search"));
    EXPECT_TRUE(names.contains("patrol"));
}

TEST(Phase7_PathExplorer, ShadowedBehaviorNotInPaths) {
    auto schema = bt::SchemaParser::parse(kShadowedYaml);
    auto paths  = bt::PathExplorer::enumerate(schema);

    // only "always_fires" is reachable
    ASSERT_EQ(paths.size(), 1);
    EXPECT_EQ(paths[0].activatedBehavior, "always_fires");
}

TEST(Phase7_PathExplorer, DefaultBehaviorAlwaysInPaths) {
    auto schema = bt::SchemaParser::parse(kSingleBehaviorYaml);
    auto paths  = bt::PathExplorer::enumerate(schema);

    ASSERT_EQ(paths.size(), 1);
    EXPECT_EQ(paths[0].activatedBehavior, "patrol");
}

TEST(Phase7_PathExplorer, ActivePathContainsBehaviorName) {
    auto schema = bt::SchemaParser::parse(kThreeBehaviorYaml);
    auto paths  = bt::PathExplorer::enumerate(schema);

    for (const auto& path : paths) {
        bool found = false;
        for (const auto& node : path.activePath) {
            if (node == path.activatedBehavior) { found = true; }
        }
        EXPECT_TRUE(found) << "Behavior " << path.activatedBehavior
                           << " not found in its own activePath";
    }
}

TEST(Phase7_PathExplorer, FuzzRunsRequestedTicks) {
    auto schema = bt::SchemaParser::parse(kThreeBehaviorYaml);
    auto result = bt::PathExplorer::fuzz(schema, 100, /*seed=*/42);
    EXPECT_EQ(result.ticksRun, 100);
}

TEST(Phase7_PathExplorer, FuzzActivatesMultipleBehaviors) {
    auto schema = bt::SchemaParser::parse(kThreeBehaviorYaml);
    auto result = bt::PathExplorer::fuzz(schema, 500, /*seed=*/1);
    // With random conditions over 500 ticks, all 3 behaviors should appear
    EXPECT_GE(result.activatedBehaviors.size(), 2);
}

TEST(Phase7_PathExplorer, FuzzNeverActivatedReflectsUnreachable) {
    auto schema = bt::SchemaParser::parse(kShadowedYaml);
    auto result = bt::PathExplorer::fuzz(schema, 200, /*seed=*/7);
    // "shadowed" has a condition but "always_fires" comes first with no condition
    EXPECT_TRUE(result.neverActivated.contains("shadowed"));
}

// ── Phase7_Partitioning ───────────────────────────────────────────────────────

// Behavior with a subtree that has 6 nodes: 1 sequence + 5 actions
static constexpr auto kLargeSubtreeYaml = R"yaml(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: sequence
      children:
        - type: action
          name: step1
        - type: action
          name: step2
        - type: action
          name: step3
        - type: action
          name: step4
        - type: action
          name: step5
)yaml";

static constexpr auto kSmallSubtreeYaml = R"yaml(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)yaml";

TEST(Phase7_Partitioning, SmallSubtreeNotWrappedByDefault) {
    bt::LoaderRegistry reg;
    reg.actions["wander"] = [] { return bt::Status::SUCCESS; };
    auto doc  = bt::SchemaParser::parse(kSmallSubtreeYaml);
    bt::PartitionConfig cfg;
    cfg.maxNodesPerScope = 5;
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);
    EXPECT_FALSE(hasNodeOfType(tree.root(), "SubtreeScope"));
}

TEST(Phase7_Partitioning, LargeSubtreeWrappedInScope) {
    bt::LoaderRegistry reg;
    for (int step = 1; step <= 5; ++step) {
        reg.actions["step" + std::to_string(step)] = [] { return bt::Status::SUCCESS; };
    }
    auto doc  = bt::SchemaParser::parse(kLargeSubtreeYaml);
    bt::PartitionConfig cfg;
    cfg.maxNodesPerScope = 5;  // subtree has 6 nodes → wraps
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);
    EXPECT_TRUE(hasNodeOfType(tree.root(), "SubtreeScope"));
}

TEST(Phase7_Partitioning, PartitionedTreeTicksCorrectly) {
    bt::LoaderRegistry reg;
    for (int step = 1; step <= 5; ++step) {
        reg.actions["step" + std::to_string(step)] = [] { return bt::Status::SUCCESS; };
    }
    auto doc = bt::SchemaParser::parse(kLargeSubtreeYaml);
    bt::PartitionConfig cfg;
    cfg.maxNodesPerScope = 5;
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);
    // Partitioned tree must produce the same result as the original
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase7_Partitioning, AutoPartitionFalseSkipsWrapping) {
    bt::LoaderRegistry reg;
    for (int step = 1; step <= 5; ++step) {
        reg.actions["step" + std::to_string(step)] = [] { return bt::Status::SUCCESS; };
    }
    auto doc = bt::SchemaParser::parse(kLargeSubtreeYaml);
    bt::PartitionConfig cfg;
    cfg.maxNodesPerScope = 5;
    cfg.autoPartition    = false;
    auto tree = bt::SchemaLoader::load(doc, reg, {}, cfg);
    EXPECT_FALSE(hasNodeOfType(tree.root(), "SubtreeScope"));
}

}  // namespace
