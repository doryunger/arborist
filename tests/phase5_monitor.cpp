#include <gtest/gtest.h>

#include <any>
#include <chrono>
#include <string>
#include <thread>
#include <unordered_map>

#include "bt/BlackboardDiff.h"
#include "bt/Blackboard.h"
#include "bt/DecisionEmitter.h"
#include "bt/MonitorQuery.h"
#include "bt/MonitorServer.h"
#include "bt/Node.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/Status.h"
#include "bt/TreeSerializer.h"
#include "bt/BehaviorTree.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static bt::Blackboard emptyBoard() { return {}; }

namespace {
class TestLeaf final : public bt::Node {
public:
    explicit TestLeaf(std::string name) : bt::Node(std::move(name)) {}
    [[nodiscard]] bt::Status tick() override { return bt::Status::SUCCESS; }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "TestLeaf"; }
};
}  // namespace

// ── MonitorQuery ──────────────────────────────────────────────────────────────

TEST(Phase5_MonitorQuery, EmptyHistoryReturnsEmpty) {
    bt::DecisionEmitter emitter;
    bt::MonitorQuery query(emitter);
    EXPECT_TRUE(query.filter({}).empty());
}

TEST(Phase5_MonitorQuery, AllReturnsFullHistory) {
    bt::DecisionEmitter emitter;
    emitter.record(1, "attack", bt::Status::RUNNING, emptyBoard());
    emitter.record(2, "patrol", bt::Status::SUCCESS, emptyBoard());
    bt::MonitorQuery query(emitter);
    EXPECT_EQ(query.all().size(), 2u);
}

TEST(Phase5_MonitorQuery, FilterByBehaviorName) {
    bt::DecisionEmitter emitter;
    emitter.record(1, "attack", bt::Status::RUNNING, emptyBoard());
    emitter.record(2, "patrol", bt::Status::SUCCESS, emptyBoard());
    emitter.record(3, "attack", bt::Status::SUCCESS, emptyBoard());
    bt::MonitorQuery query(emitter);
    bt::QueryFilter filter;
    filter.behaviorName = "attack";
    auto results = query.filter(filter);
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0].tickNumber, 1u);
    EXPECT_EQ(results[1].tickNumber, 3u);
}

TEST(Phase5_MonitorQuery, FilterByStatus) {
    bt::DecisionEmitter emitter;
    emitter.record(1, "attack", bt::Status::RUNNING, emptyBoard());
    emitter.record(2, "patrol", bt::Status::SUCCESS, emptyBoard());
    emitter.record(3, "attack", bt::Status::FAILURE, emptyBoard());
    bt::MonitorQuery query(emitter);
    bt::QueryFilter filter;
    filter.status = bt::Status::SUCCESS;
    auto results = query.filter(filter);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].behaviorName, "patrol");
}

TEST(Phase5_MonitorQuery, FilterByTickRange) {
    bt::DecisionEmitter emitter;
    for (std::size_t idx = 1; idx <= 5; ++idx) {
        emitter.record(idx, "action", bt::Status::RUNNING, emptyBoard());
    }
    bt::MonitorQuery query(emitter);
    bt::QueryFilter filter;
    filter.fromTick = 2;
    filter.toTick = 4;
    auto results = query.filter(filter);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].tickNumber, 2u);
    EXPECT_EQ(results[2].tickNumber, 4u);
}

TEST(Phase5_MonitorQuery, FilterCombinesBehaviorNameAndStatus) {
    bt::DecisionEmitter emitter;
    emitter.record(1, "attack", bt::Status::RUNNING, emptyBoard());
    emitter.record(2, "attack", bt::Status::SUCCESS, emptyBoard());
    emitter.record(3, "patrol", bt::Status::SUCCESS, emptyBoard());
    bt::MonitorQuery query(emitter);
    bt::QueryFilter filter;
    filter.behaviorName = "attack";
    filter.status = bt::Status::SUCCESS;
    auto results = query.filter(filter);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].tickNumber, 2u);
}

TEST(Phase5_MonitorQuery, DefaultTickRangeMatchesAll) {
    bt::DecisionEmitter emitter;
    for (std::size_t idx = 1; idx <= 10; ++idx) {
        emitter.record(idx, "x", bt::Status::RUNNING, emptyBoard());
    }
    bt::MonitorQuery query(emitter);
    EXPECT_EQ(query.filter({}).size(), 10u);
}

// ── BlackboardDiff ────────────────────────────────────────────────────────────

TEST(Phase5_BlackboardDiff, NoChanges) {
    std::unordered_map<std::string, std::any> before{{"health", std::any{100}}};
    std::unordered_map<std::string, std::any> after{{"health", std::any{100}}};
    auto diff = bt::diffBlackboards(before, after);
    EXPECT_TRUE(diff.added.empty());
    EXPECT_TRUE(diff.removed.empty());
    EXPECT_TRUE(diff.changed.empty());
}

TEST(Phase5_BlackboardDiff, KeyAdded) {
    std::unordered_map<std::string, std::any> before;
    std::unordered_map<std::string, std::any> after{{"health", std::any{100}}};
    auto diff = bt::diffBlackboards(before, after);
    ASSERT_EQ(diff.added.size(), 1u);
    EXPECT_EQ(diff.added[0], "health");
    EXPECT_TRUE(diff.removed.empty());
    EXPECT_TRUE(diff.changed.empty());
}

TEST(Phase5_BlackboardDiff, KeyRemoved) {
    std::unordered_map<std::string, std::any> before{{"health", std::any{100}}};
    std::unordered_map<std::string, std::any> after;
    auto diff = bt::diffBlackboards(before, after);
    EXPECT_TRUE(diff.added.empty());
    ASSERT_EQ(diff.removed.size(), 1u);
    EXPECT_EQ(diff.removed[0], "health");
    EXPECT_TRUE(diff.changed.empty());
}

TEST(Phase5_BlackboardDiff, IntValueChanged) {
    std::unordered_map<std::string, std::any> before{{"health", std::any{100}}};
    std::unordered_map<std::string, std::any> after{{"health", std::any{50}}};
    auto diff = bt::diffBlackboards(before, after);
    EXPECT_TRUE(diff.added.empty());
    EXPECT_TRUE(diff.removed.empty());
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], "health");
}

TEST(Phase5_BlackboardDiff, BoolValueChanged) {
    std::unordered_map<std::string, std::any> before{{"alive", std::any{true}}};
    std::unordered_map<std::string, std::any> after{{"alive", std::any{false}}};
    auto diff = bt::diffBlackboards(before, after);
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], "alive");
}

TEST(Phase5_BlackboardDiff, StringValueChanged) {
    std::unordered_map<std::string, std::any> before{{"state", std::any{std::string{"idle"}}}};
    std::unordered_map<std::string, std::any> after{{"state", std::any{std::string{"attack"}}}};
    auto diff = bt::diffBlackboards(before, after);
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], "state");
}

TEST(Phase5_BlackboardDiff, TypeChanged) {
    std::unordered_map<std::string, std::any> before{{"value", std::any{100}}};
    std::unordered_map<std::string, std::any> after{{"value", std::any{std::string{"high"}}}};
    auto diff = bt::diffBlackboards(before, after);
    ASSERT_EQ(diff.changed.size(), 1u);
    EXPECT_EQ(diff.changed[0], "value");
}

TEST(Phase5_BlackboardDiff, MultipleChangesSimultaneous) {
    std::unordered_map<std::string, std::any> before{
        {"health", std::any{100}}, {"stamina", std::any{50}}};
    std::unordered_map<std::string, std::any> after{
        {"health", std::any{80}}, {"mana", std::any{30}}};
    auto diff = bt::diffBlackboards(before, after);
    EXPECT_EQ(diff.added.size(), 1u);    // mana
    EXPECT_EQ(diff.removed.size(), 1u);  // stamina
    EXPECT_EQ(diff.changed.size(), 1u);  // health
}

TEST(Phase5_BlackboardDiff, UnchangedIntNotReported) {
    std::unordered_map<std::string, std::any> before{{"x", std::any{42}}};
    std::unordered_map<std::string, std::any> after{{"x", std::any{42}}};
    auto diff = bt::diffBlackboards(before, after);
    EXPECT_TRUE(diff.changed.empty());
}

// ── TreeSerializer ────────────────────────────────────────────────────────────

TEST(Phase5_TreeSerializer, SingleLeafNode) {
    TestLeaf leaf("my_action");
    auto json = bt::TreeSerializer::toJson(leaf);
    EXPECT_NE(json.find(R"("name":"my_action")"), std::string::npos);
    EXPECT_NE(json.find(R"("type":"TestLeaf")"), std::string::npos);
    EXPECT_NE(json.find(R"("children":[])"), std::string::npos);
}

TEST(Phase5_TreeSerializer, CompositeNodeWithChildren) {
    auto selector = std::make_unique<bt::Selector>("root");
    selector->addChild(std::make_unique<TestLeaf>("child_a"));
    selector->addChild(std::make_unique<TestLeaf>("child_b"));
    auto json = bt::TreeSerializer::toJson(*selector);
    EXPECT_NE(json.find(R"("name":"root")"), std::string::npos);
    EXPECT_NE(json.find(R"("type":"Selector")"), std::string::npos);
    EXPECT_NE(json.find(R"("name":"child_a")"), std::string::npos);
    EXPECT_NE(json.find(R"("name":"child_b")"), std::string::npos);
}

TEST(Phase5_TreeSerializer, NestedCompositeStructure) {
    auto root = std::make_unique<bt::Selector>("root");
    auto seq = std::make_unique<bt::Sequence>("seq");
    seq->addChild(std::make_unique<TestLeaf>("leaf"));
    root->addChild(std::move(seq));
    auto json = bt::TreeSerializer::toJson(*root);
    EXPECT_NE(json.find(R"("name":"seq")"), std::string::npos);
    EXPECT_NE(json.find(R"("type":"Sequence")"), std::string::npos);
    EXPECT_NE(json.find(R"("name":"leaf")"), std::string::npos);
}

TEST(Phase5_TreeSerializer, TypeNamesAreCorrect) {
    bt::Selector sel("sel");
    bt::Sequence seq("seq");
    TestLeaf leaf("leaf");
    EXPECT_NE(bt::TreeSerializer::toJson(sel).find(R"("type":"Selector")"), std::string::npos);
    EXPECT_NE(bt::TreeSerializer::toJson(seq).find(R"("type":"Sequence")"), std::string::npos);
    EXPECT_NE(bt::TreeSerializer::toJson(leaf).find(R"("type":"TestLeaf")"), std::string::npos);
}

TEST(Phase5_TreeSerializer, OutputIsValidJsonStructure) {
    auto root = std::make_unique<bt::Selector>("root");
    root->addChild(std::make_unique<TestLeaf>("child"));
    auto json = bt::TreeSerializer::toJson(*root);
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

// ── MonitorServer ─────────────────────────────────────────────────────────────

TEST(Phase5_MonitorServer, NotRunningBeforeStart) {
    auto root = std::make_unique<bt::Selector>("root");
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    bt::MonitorServer server(tree, emitter);
    EXPECT_FALSE(server.running());
}

TEST(Phase5_MonitorServer, RunningAfterStart) {
    auto root = std::make_unique<bt::Selector>("root");
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    bt::MonitorServer server(tree, emitter);
    server.start(18081);
    EXPECT_TRUE(server.running());
    server.stop();
}

TEST(Phase5_MonitorServer, NotRunningAfterStop) {
    auto root = std::make_unique<bt::Selector>("root");
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    bt::MonitorServer server(tree, emitter);
    server.start(18082);
    server.stop();
    EXPECT_FALSE(server.running());
}

TEST(Phase5_MonitorServer, TreeEndpointReturnsJson) {
    auto root = std::make_unique<bt::Selector>("root");
    root->addChild(std::make_unique<TestLeaf>("action"));
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    bt::MonitorServer server(tree, emitter);
    server.start(18083);

    auto response = server.getTree();
    EXPECT_FALSE(response.empty());
    EXPECT_NE(response.find(R"("name":"root")"), std::string::npos);

    server.stop();
}

TEST(Phase5_MonitorServer, HistoryEndpointReturnsJsonArray) {
    auto root = std::make_unique<bt::Selector>("root");
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    emitter.record(1, "attack", bt::Status::SUCCESS, emptyBoard());
    bt::MonitorServer server(tree, emitter);
    server.start(18084);

    auto response = server.getHistory();
    EXPECT_FALSE(response.empty());
    EXPECT_EQ(response.front(), '[');
    EXPECT_NE(response.find("attack"), std::string::npos);

    server.stop();
}

TEST(Phase5_MonitorServer, HistoryEndpointEmptyWhenNoTicks) {
    auto root = std::make_unique<bt::Selector>("root");
    bt::BehaviorTree tree(std::move(root));
    bt::DecisionEmitter emitter;
    bt::MonitorServer server(tree, emitter);
    server.start(18085);

    auto response = server.getHistory();
    EXPECT_EQ(response, "[]");

    server.stop();
}
