#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "bt/Action.h"
#include "bt/BehaviorBuilder.h"
#include "bt/BehaviorTree.h"
#include "bt/Blackboard.h"
#include "bt/Condition.h"
#include "bt/DecisionEmitter.h"
#include "bt/Node.h"
#include "bt/Parallel.h"
#include "bt/Policy.h"
#include "bt/PriorityResolver.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/StateRegistry.h"
#include "bt/Status.h"
#include "bt/TreeAssembler.h"
#include "bt/TreeUtils.h"
#include "bt/Validator.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Test helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

class FixedNode : public bt::Node {
public:
    explicit FixedNode(std::string nodeName, bt::Status result)
        : bt::Node(std::move(nodeName)), result_(result) {}

    bt::Status doTick() override { return result_; }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "FixedNode"; }

private:
    bt::Status result_;
};

class ResettableNode : public bt::Node {
public:
    explicit ResettableNode() : bt::Node("resettable") {}

    bt::Status doTick() override {
        return resetCalled_ ? bt::Status::SUCCESS : bt::Status::RUNNING;
    }

    void reset() override { resetCalled_ = false; }
    void markReset() { resetCalled_ = true; }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "ResettableNode"; }

private:
    bool resetCalled_{false};
};

// A leaf node whose status is set externally.
// tickCount_ accumulates across resets so resumption tests can verify
// exactly which nodes were ticked on each tick.
// resetCount_ increments on each reset() call so composite propagation
// can be verified.
class ControllableNode : public bt::Node {
public:
    explicit ControllableNode(std::string nodeName, bt::Status initial = bt::Status::FAILURE)
        : bt::Node(std::move(nodeName)), status_(initial) {}

    bt::Status doTick() override {
        ++tickCount_;
        return status_;
    }

    void reset() override { ++resetCount_; }

    void setStatus(bt::Status status) { status_ = status; }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "TestLeaf"; }
    [[nodiscard]] int tickCount() const noexcept { return tickCount_; }
    [[nodiscard]] int resetCount() const noexcept { return resetCount_; }

private:
    bt::Status status_;
    int tickCount_{0};
    int resetCount_{0};
};

std::unique_ptr<ControllableNode> makeLeaf(std::string name,
                                            bt::Status status = bt::Status::FAILURE) {
    return std::make_unique<ControllableNode>(std::move(name), status);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Status
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Status, ValuesAreDistinct) {
    EXPECT_NE(bt::Status::SUCCESS, bt::Status::FAILURE);
    EXPECT_NE(bt::Status::SUCCESS, bt::Status::RUNNING);
    EXPECT_NE(bt::Status::FAILURE, bt::Status::RUNNING);
}

TEST(Phase1_Status, UnderlyingValuesAreStable) {
    EXPECT_EQ(static_cast<int>(bt::Status::SUCCESS), 0);
    EXPECT_EQ(static_cast<int>(bt::Status::FAILURE), 1);
    EXPECT_EQ(static_cast<int>(bt::Status::RUNNING), 2);
}

TEST(Phase1_Status, ToStringSuccess) {
    EXPECT_EQ(bt::toString(bt::Status::SUCCESS), "SUCCESS");
}

TEST(Phase1_Status, ToStringFailure) {
    EXPECT_EQ(bt::toString(bt::Status::FAILURE), "FAILURE");
}

TEST(Phase1_Status, ToStringRunning) {
    EXPECT_EQ(bt::toString(bt::Status::RUNNING), "RUNNING");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Node
// ═══════════════════════════════════════════════════════════════════════════════

static_assert(std::is_abstract_v<bt::Node>, "Node must be abstract");
static_assert(std::is_polymorphic_v<bt::Node>, "Node must be polymorphic");
static_assert(std::has_virtual_destructor_v<bt::Node>, "Node must have a virtual destructor");
static_assert(!std::is_copy_constructible_v<bt::Node>, "Node must not be copy constructible");
static_assert(!std::is_move_constructible_v<bt::Node>, "Node must not be move constructible");
static_assert(!std::is_copy_assignable_v<bt::Node>, "Node must not be copy assignable");
static_assert(!std::is_move_assignable_v<bt::Node>, "Node must not be move assignable");

TEST(Phase1_Node, TickReturnsAllStatusValues) {
    EXPECT_EQ(FixedNode("a", bt::Status::SUCCESS).tick(), bt::Status::SUCCESS);
    EXPECT_EQ(FixedNode("b", bt::Status::FAILURE).tick(), bt::Status::FAILURE);
    EXPECT_EQ(FixedNode("c", bt::Status::RUNNING).tick(), bt::Status::RUNNING);
}

TEST(Phase1_Node, PolymorphicTickViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<FixedNode>("base", bt::Status::RUNNING);
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
}

TEST(Phase1_Node, NameIsPreserved) {
    EXPECT_EQ(FixedNode("patrol", bt::Status::SUCCESS).name(), "patrol");
}

TEST(Phase1_Node, EmptyNameIsValid) {
    EXPECT_EQ(FixedNode("", bt::Status::SUCCESS).name(), "");
}

TEST(Phase1_Node, NameViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<FixedNode>("base", bt::Status::RUNNING);
    EXPECT_EQ(node->name(), "base");
}

TEST(Phase1_Node, DefaultResetIsNoOp) {
    FixedNode node("test", bt::Status::SUCCESS);
    node.reset();
    EXPECT_EQ(node.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Node, ResetCanBeOverridden) {
    ResettableNode node;
    EXPECT_EQ(node.tick(), bt::Status::RUNNING);
    node.markReset();
    EXPECT_EQ(node.tick(), bt::Status::SUCCESS);
    node.reset();
    EXPECT_EQ(node.tick(), bt::Status::RUNNING);
}

TEST(Phase1_Node, ResetViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<ResettableNode>();
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
    dynamic_cast<ResettableNode*>(node.get())->markReset();
    node->reset();
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
}

// ═══════════════════════════════════════════════════════════════════════════════
// CompositeNode
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_CompositeNode, ChildrenAreStoredInOrder) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("c", bt::Status::SUCCESS));
    EXPECT_EQ(seq.children().size(), 3);
    EXPECT_EQ(seq.children()[0]->name(), "a");
    EXPECT_EQ(seq.children()[1]->name(), "b");
    EXPECT_EQ(seq.children()[2]->name(), "c");
}

TEST(Phase1_CompositeNode, ResetResetsChildIndex) {
    bt::Sequence seq("seq");
    auto* first = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    std::ignore = seq.tick();
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    seq.reset();

    std::ignore = seq.tick();
    EXPECT_EQ(first->tickCount(), 2);
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Phase1_CompositeNode, ResetPropagatestoChildren) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::RUNNING));
    std::ignore = seq.tick();
    seq.reset();
    EXPECT_EQ(child->resetCount(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Sequence
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Sequence, EmptySucceeds) {
    bt::Sequence seq("seq");
    EXPECT_EQ(seq.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Sequence, AllChildrenSucceed) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Sequence, FirstChildFailureShortCircuits) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::FAILURE));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::FAILURE);
    EXPECT_EQ(second->tickCount(), 0);
}

TEST(Phase1_Sequence, MiddleChildFailureShortCircuits) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::FAILURE));
    auto* third = seq.addChildAndGet(makeLeaf("c", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::FAILURE);
    EXPECT_EQ(third->tickCount(), 0);
}

TEST(Phase1_Sequence, RunningChildPausesSequence) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::RUNNING));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
}

TEST(Phase1_Sequence, RunningResumption) {
    bt::Sequence seq("seq");
    auto* first = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);   // unchanged — RUNNING resumption verified
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Phase1_Sequence, ResetsAfterSuccess) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    std::ignore = seq.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

TEST(Phase1_Sequence, ResetsAfterFailure) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    std::ignore = seq.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Selector
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Selector, EmptyFails) {
    bt::Selector sel("sel");
    EXPECT_EQ(sel.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Selector, FirstChildSuccessShortCircuits) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = sel.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(sel.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(second->tickCount(), 0);
}

TEST(Phase1_Selector, AllChildrenFail) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::FAILURE));
    sel.addChild(makeLeaf("b", bt::Status::FAILURE));
    EXPECT_EQ(sel.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Selector, FirstFailThenSucceed) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::FAILURE));
    sel.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(sel.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Selector, RunningResumption) {
    bt::Selector sel("sel");
    auto* first = sel.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    auto* second = sel.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    EXPECT_EQ(sel.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    EXPECT_EQ(sel.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);  // unchanged — RUNNING resumption verified
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Phase1_Selector, ResetsAfterSuccess) {
    bt::Selector sel("sel");
    auto* child = sel.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    std::ignore = sel.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = sel.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

TEST(Phase1_Selector, ResetsAfterFailure) {
    bt::Selector sel("sel");
    auto* child = sel.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    std::ignore = sel.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = sel.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// treeToString
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_TreeUtils, SingleLeafNode) {
    ControllableNode leaf("patrol");
    EXPECT_EQ(bt::treeToString(leaf), "[TestLeaf] patrol\n");
}

TEST(Phase1_TreeUtils, SingleLeafEmptyName) {
    ControllableNode leaf("");
    EXPECT_EQ(bt::treeToString(leaf), "[TestLeaf]\n");
}

TEST(Phase1_TreeUtils, SequenceWithChildren) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    std::string expected =
        "[Sequence] seq\n"
        "├── [TestLeaf] a\n"
        "└── [TestLeaf] b\n";
    EXPECT_EQ(bt::treeToString(seq), expected);
}

TEST(Phase1_TreeUtils, NestedTree) {
    bt::Selector root("root");
    auto seq = std::make_unique<bt::Sequence>("attack_sequence");
    seq->addChild(makeLeaf("enemy_visible", bt::Status::FAILURE));
    seq->addChild(makeLeaf("attack", bt::Status::RUNNING));
    root.addChild(std::move(seq));
    root.addChild(makeLeaf("patrol", bt::Status::SUCCESS));

    std::string expected =
        "[Selector] root\n"
        "├── [Sequence] attack_sequence\n"
        "│   ├── [TestLeaf] enemy_visible\n"
        "│   └── [TestLeaf] attack\n"
        "└── [TestLeaf] patrol\n";
    EXPECT_EQ(bt::treeToString(root), expected);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration — full tree tick scenarios
// ═══════════════════════════════════════════════════════════════════════════════
//
// Tree:
//   [Selector] root
//   ├── [Sequence] attack_sequence
//   │   ├── [TestLeaf] enemy_visible
//   │   └── [TestLeaf] attack
//   └── [TestLeaf] patrol

class Phase1Integration : public ::testing::Test {
protected:
    void SetUp() override {
        auto seq = std::make_unique<bt::Sequence>("attack_sequence");
        enemyVisible_ = seq->addChildAndGet(makeLeaf("enemy_visible", bt::Status::FAILURE));
        attack_ = seq->addChildAndGet(makeLeaf("attack", bt::Status::FAILURE));
        root_.addChild(std::move(seq));
        patrol_ = root_.addChildAndGet(makeLeaf("patrol", bt::Status::SUCCESS));
    }

    bt::Selector root_{"root"};
    ControllableNode* enemyVisible_{nullptr};
    ControllableNode* attack_{nullptr};
    ControllableNode* patrol_{nullptr};
};

TEST_F(Phase1Integration, TreeStructureMatchesExpected) {
    std::string expected =
        "[Selector] root\n"
        "├── [Sequence] attack_sequence\n"
        "│   ├── [TestLeaf] enemy_visible\n"
        "│   └── [TestLeaf] attack\n"
        "└── [TestLeaf] patrol\n";
    EXPECT_EQ(bt::treeToString(root_), expected);
}

TEST_F(Phase1Integration, EnemyNotVisible_PatrolRuns) {
    enemyVisible_->setStatus(bt::Status::FAILURE);
    patrol_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(enemyVisible_->tickCount(), 1);
    EXPECT_EQ(attack_->tickCount(), 0);
    EXPECT_EQ(patrol_->tickCount(), 1);
}

TEST_F(Phase1Integration, EnemyVisible_AttackRunning_TreeRunning) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::RUNNING);
    EXPECT_EQ(root_.tick(), bt::Status::RUNNING);
    EXPECT_EQ(enemyVisible_->tickCount(), 1);
    EXPECT_EQ(attack_->tickCount(), 1);
    EXPECT_EQ(patrol_->tickCount(), 0);
}

TEST_F(Phase1Integration, AttackResumedWithoutReCheckingEnemyVisible) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::RUNNING);
    std::ignore = root_.tick();

    std::ignore = root_.tick();
    EXPECT_EQ(enemyVisible_->tickCount(), 1);  // RUNNING resumption verified
    EXPECT_EQ(attack_->tickCount(), 2);
}

TEST_F(Phase1Integration, AttackSucceeds_TreeSucceeds) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(patrol_->tickCount(), 0);
}

TEST_F(Phase1Integration, AttackFails_PatrolFallback) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::FAILURE);
    patrol_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(patrol_->tickCount(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Parallel + Policy
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Parallel, AllPolicy_AllSucceed) {
    bt::Parallel par("par", bt::Policy::all());
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(par.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Parallel, AllPolicy_OneFailure) {
    bt::Parallel par("par", bt::Policy::all());
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::FAILURE));
    EXPECT_EQ(par.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Parallel, AllPolicy_OneRunning) {
    bt::Parallel par("par", bt::Policy::all());
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::RUNNING));
    EXPECT_EQ(par.tick(), bt::Status::RUNNING);
}

TEST(Phase1_Parallel, AnyPolicy_OneSuccess) {
    bt::Parallel par("par", bt::Policy::any());
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::FAILURE));
    EXPECT_EQ(par.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Parallel, AnyPolicy_AllFail) {
    bt::Parallel par("par", bt::Policy::any());
    par.addChild(makeLeaf("a", bt::Status::FAILURE));
    par.addChild(makeLeaf("b", bt::Status::FAILURE));
    EXPECT_EQ(par.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Parallel, AnyPolicy_Running) {
    bt::Parallel par("par", bt::Policy::any());
    par.addChild(makeLeaf("a", bt::Status::FAILURE));
    par.addChild(makeLeaf("b", bt::Status::RUNNING));
    EXPECT_EQ(par.tick(), bt::Status::RUNNING);
}

TEST(Phase1_Parallel, ThresholdPolicy_Satisfied) {
    bt::Parallel par("par", bt::Policy::threshold(2));
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::SUCCESS));
    par.addChild(makeLeaf("c", bt::Status::FAILURE));
    EXPECT_EQ(par.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Parallel, ThresholdPolicy_Impossible) {
    bt::Parallel par("par", bt::Policy::threshold(2));
    par.addChild(makeLeaf("a", bt::Status::FAILURE));
    par.addChild(makeLeaf("b", bt::Status::FAILURE));
    par.addChild(makeLeaf("c", bt::Status::FAILURE));
    EXPECT_EQ(par.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Parallel, ThresholdPolicy_StillRunning) {
    bt::Parallel par("par", bt::Policy::threshold(2));
    par.addChild(makeLeaf("a", bt::Status::SUCCESS));
    par.addChild(makeLeaf("b", bt::Status::RUNNING));
    par.addChild(makeLeaf("c", bt::Status::RUNNING));
    EXPECT_EQ(par.tick(), bt::Status::RUNNING);
}

TEST(Phase1_Parallel, TicksAllChildrenEveryCall) {
    bt::Parallel par("par", bt::Policy::any());
    auto* first = par.addChildAndGet(makeLeaf("a", bt::Status::RUNNING));
    auto* second = par.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));
    std::ignore = par.tick();
    std::ignore = par.tick();
    EXPECT_EQ(first->tickCount(), 2);   // both ticked every call — no resumption
    EXPECT_EQ(second->tickCount(), 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Action + Condition
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Action, CallbackInvokedOnTick) {
    int callCount = 0;
    bt::Action action("act", [&callCount] {
        ++callCount;
        return bt::Status::SUCCESS;
    });
    EXPECT_EQ(action.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(callCount, 1);
}

TEST(Phase1_Action, ReturnsCallbackStatus) {
    bt::Action running("run", [] { return bt::Status::RUNNING; });
    bt::Action failing("fail", [] { return bt::Status::FAILURE; });
    EXPECT_EQ(running.tick(), bt::Status::RUNNING);
    EXPECT_EQ(failing.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Condition, TruePredicateReturnsSuccess) {
    bt::Condition cond("cond", [] { return true; });
    EXPECT_EQ(cond.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_Condition, FalsePredicateReturnsFailure) {
    bt::Condition cond("cond", [] { return false; });
    EXPECT_EQ(cond.tick(), bt::Status::FAILURE);
}

TEST(Phase1_Condition, ReflectsChangingState) {
    bool flag = false;
    bt::Condition cond("cond", [&flag] { return flag; });
    EXPECT_EQ(cond.tick(), bt::Status::FAILURE);
    flag = true;
    EXPECT_EQ(cond.tick(), bt::Status::SUCCESS);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Blackboard
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Blackboard, SetAndGet) {
    bt::Blackboard board;
    board.set<int>("health", 100);
    EXPECT_EQ(board.get<int>("health"), 100);
}

TEST(Phase1_Blackboard, HasReturnsTrueForExistingKey) {
    bt::Blackboard board;
    board.set<bool>("visible", true);
    EXPECT_TRUE(board.has("visible"));
    EXPECT_FALSE(board.has("missing"));
}

TEST(Phase1_Blackboard, RegisterSourcePullsOnGet) {
    bt::Blackboard board;
    int engineValue = 42;
    board.registerSource<int>("sensor", [&engineValue] { return engineValue; });
    EXPECT_EQ(board.get<int>("sensor"), 42);
    engineValue = 99;
    EXPECT_EQ(board.get<int>("sensor"), 99);
}

TEST(Phase1_Blackboard, RefreshPullsAllSources) {
    bt::Blackboard board;
    int value = 1;
    board.registerSource<int>("val", [&value] { return value; });
    board.refresh();
    value = 2;
    // After refresh(), the snapshot is from before the change
    EXPECT_EQ(board.get<int>("val"), 1);
    board.refresh();
    EXPECT_EQ(board.get<int>("val"), 2);
}

TEST(Phase1_Blackboard, OverwriteWithSet) {
    bt::Blackboard board;
    board.set<std::string>("name", "patrol");
    board.set<std::string>("name", "attack");
    EXPECT_EQ(board.get<std::string>("name"), "attack");
}

// ═══════════════════════════════════════════════════════════════════════════════
// BehaviorTree
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_BehaviorTree, TickDrivesRootNode) {
    bool ticked = false;
    auto root = std::make_unique<bt::Action>("root", [&ticked] {
        ticked = true;
        return bt::Status::SUCCESS;
    });
    bt::BehaviorTree tree(std::move(root));
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
    EXPECT_TRUE(ticked);
}

TEST(Phase1_BehaviorTree, TickCountIncrements) {
    auto root = std::make_unique<bt::Action>("root", [] { return bt::Status::RUNNING; });
    bt::BehaviorTree tree(std::move(root));
    std::ignore = tree.tick();
    std::ignore = tree.tick();
    EXPECT_EQ(tree.tickCount(), 2);
}

TEST(Phase1_BehaviorTree, BlackboardRefreshedBeforeTick) {
    int engineValue = 10;
    bt::Blackboard board;
    board.registerSource<int>("val", [&engineValue] { return engineValue; });

    auto root = std::make_unique<bt::Action>("root", [] { return bt::Status::SUCCESS; });

    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree(std::move(root), std::move(board), {}, &emitter);

    // Change value after board is moved into tree — refresh() must pull this update.
    engineValue = 99;
    std::ignore = tree.tick();

    const auto& history = emitter.history();
    ASSERT_FALSE(history.empty());
    const auto& snapshot = history[0].blackboardSnapshot;
    ASSERT_TRUE(snapshot.contains("val"));
    EXPECT_EQ(std::any_cast<int>(snapshot.at("val")), 99);
}

TEST(Phase1_BehaviorTree, InterruptionSwitchesWhenHigherPriorityAvailable) {
    bool highPriority = false;
    int lowTicks = 0;
    int highTicks = 0;

    bt::BehaviorBuilder builder;
    builder.behavior("high").when([&highPriority] { return highPriority; }).onTick([&highTicks] {
        ++highTicks;
        return bt::Status::RUNNING;
    });
    builder.behavior("low").onTick([&lowTicks] {
        ++lowTicks;
        return bt::Status::RUNNING;
    });

    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));

    std::ignore = tree.tick();  // low runs (high condition false)
    EXPECT_EQ(lowTicks, 1);
    EXPECT_EQ(highTicks, 0);

    highPriority = true;
    std::ignore = tree.tick();  // interrupts low, high runs
    EXPECT_EQ(highTicks, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Builder layer
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_BehaviorBuilder, RegistersBehaviorsInOrder) {
    bt::BehaviorBuilder builder;
    builder.behavior("attack").when([] { return true; }).onTick([] { return bt::Status::RUNNING; });
    builder.behavior("patrol").onTick([] { return bt::Status::SUCCESS; });
    EXPECT_EQ(builder.entries().size(), 2);
    EXPECT_EQ(builder.entries()[0].name, "attack");
    EXPECT_EQ(builder.entries()[1].name, "patrol");
}

TEST(Phase1_BehaviorBuilder, ConditionStoredCorrectly) {
    bt::BehaviorBuilder builder;
    builder.behavior("test").when([] { return true; }).onTick([] { return bt::Status::SUCCESS; });
    EXPECT_TRUE(builder.entries()[0].condition);
    EXPECT_TRUE(builder.entries()[0].condition());
}

TEST(Phase1_BehaviorBuilder, InterruptibleFlagStored) {
    bt::BehaviorBuilder builder;
    builder.behavior("test")
        .onTick([] { return bt::Status::SUCCESS; })
        .interruptible(false);
    EXPECT_FALSE(builder.entries()[0].interruptible);
}

TEST(Phase1_PriorityResolver, ReturnsFirstValidBehavior) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "a",
                        .condition = [] { return false; },
                        .onTick = [] { return bt::Status::SUCCESS; }});
    entries.push_back({.name = "b",
                        .condition = [] { return true; },
                        .onTick = [] { return bt::Status::SUCCESS; }});
    auto result = bt::PriorityResolver::resolve(entries);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1);
}

TEST(Phase1_PriorityResolver, NullConditionIsAlwaysValid) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "default", .onTick = [] { return bt::Status::SUCCESS; }});
    auto result = bt::PriorityResolver::resolve(entries);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
}

TEST(Phase1_PriorityResolver, ReturnsNulloptWhenNoneValid) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "a",
                        .condition = [] { return false; },
                        .onTick = [] { return bt::Status::SUCCESS; }});
    EXPECT_FALSE(bt::PriorityResolver::resolve(entries).has_value());
}

TEST(Phase1_StateRegistry, AppliesSourcestoBlackboard) {
    bt::StateRegistry registry;
    registry.state<int>("health", [] { return 80; });
    bt::Blackboard board;
    registry.applyTo(board);
    EXPECT_EQ(board.get<int>("health"), 80);
}

TEST(Phase1_TreeAssembler, AssemblesCorrectStructure) {
    bt::BehaviorBuilder builder;
    builder.behavior("attack")
        .when([] { return true; })
        .onTick([] { return bt::Status::SUCCESS; });
    builder.behavior("patrol").onTick([] { return bt::Status::SUCCESS; });

    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase1_BehaviorAction, EnterCalledOnFirstTick) {
    int enterCount = 0;
    int tickCount = 0;
    bt::BehaviorBuilder builder;
    builder.behavior("test")
        .onEnter([&enterCount] { ++enterCount; })
        .onTick([&tickCount] {
            ++tickCount;
            return bt::Status::RUNNING;
        });

    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));
    std::ignore = tree.tick();
    std::ignore = tree.tick();
    EXPECT_EQ(enterCount, 1);   // only called once despite two ticks
    EXPECT_EQ(tickCount, 2);
}

TEST(Phase1_BehaviorAction, ExitCalledOnCompletion) {
    int exitCount = 0;
    bt::BehaviorBuilder builder;
    builder.behavior("test")
        .onTick([] { return bt::Status::SUCCESS; })
        .onExit([&exitCount] { ++exitCount; });

    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));
    std::ignore = tree.tick();
    EXPECT_EQ(exitCount, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Validator
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_Validator, EmptyEntriesIsError) {
    auto warnings = bt::Validator::validate({});
    ASSERT_EQ(warnings.size(), 1);
    EXPECT_TRUE(warnings[0].isError());
}

TEST(Phase1_Validator, MissingOnTickIsError) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "broken"});  // no onTick
    auto warnings = bt::Validator::validate(entries);
    EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(),
                             [](const auto& warn) { return warn.isError(); }));
}

TEST(Phase1_Validator, DuplicateNameIsError) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "dup", .onTick = [] { return bt::Status::SUCCESS; }});
    entries.push_back({.name = "dup", .onTick = [] { return bt::Status::SUCCESS; }});
    auto warnings = bt::Validator::validate(entries);
    EXPECT_TRUE(std::any_of(warnings.begin(), warnings.end(),
                             [](const auto& warn) { return warn.isError(); }));
}

TEST(Phase1_Validator, NoDefaultIsWarning) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "attack",
                        .condition = [] { return true; },
                        .onTick = [] { return bt::Status::SUCCESS; }});
    auto warnings = bt::Validator::validate(entries);
    EXPECT_FALSE(warnings.empty());
    EXPECT_FALSE(warnings[0].isError());  // warning, not error
}

TEST(Phase1_Validator, ValidRegistrationProducesNoErrors) {
    std::vector<bt::BehaviorEntry> entries;
    entries.push_back({.name = "attack",
                        .condition = [] { return true; },
                        .onTick = [] { return bt::Status::SUCCESS; }});
    entries.push_back({.name = "patrol", .onTick = [] { return bt::Status::SUCCESS; }});
    auto warnings = bt::Validator::validate(entries);
    EXPECT_TRUE(std::none_of(warnings.begin(), warnings.end(),
                              [](const auto& warn) { return warn.isError(); }));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Decision emitter
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase1_DecisionEmitter, RecordsTickNumber) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});
    ASSERT_EQ(emitter.history().size(), 1);
    EXPECT_EQ(emitter.history()[0].tickNumber, 1);
}

TEST(Phase1_DecisionEmitter, RecordsBehaviorNameAndResult) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    emitter.record(1, "attack", bt::Status::RUNNING, board, {});
    EXPECT_EQ(emitter.history()[0].behaviorName, "attack");
    EXPECT_EQ(emitter.history()[0].result, bt::Status::RUNNING);
}

TEST(Phase1_DecisionEmitter, HistoryAccumulates) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});
    emitter.record(2, "attack", bt::Status::RUNNING, board, {});
    emitter.record(3, "attack", bt::Status::SUCCESS, board, {});
    EXPECT_EQ(emitter.history().size(), 3);
}

TEST(Phase1_DecisionEmitter, ClearResetsHistory) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});
    emitter.clear();
    EXPECT_TRUE(emitter.history().empty());
}

TEST(Phase1_DecisionEmitter, BlackboardSnapshotCaptured) {
    bt::DecisionEmitter emitter;
    bt::Blackboard board;
    board.set<int>("health", 75);
    board.refresh();
    emitter.record(1, "patrol", bt::Status::SUCCESS, board, {});
    EXPECT_TRUE(emitter.history()[0].blackboardSnapshot.count("health") > 0);
}

TEST(Phase1_DecisionEmitter, IntegratesWithBehaviorTree) {
    bt::DecisionEmitter emitter;
    bt::BehaviorBuilder builder;
    builder.behavior("patrol").onTick([] { return bt::Status::SUCCESS; });

    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));
    tree.setEmitter(&emitter);

    std::ignore = tree.tick();
    std::ignore = tree.tick();
    EXPECT_EQ(emitter.history().size(), 2);
    EXPECT_EQ(emitter.history()[0].tickNumber, 1);
    EXPECT_EQ(emitter.history()[1].tickNumber, 2);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Active-path tracking
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ActivePath, LeafNodeStampedOnTick) {
    auto leaf = std::make_unique<ControllableNode>("leaf", bt::Status::SUCCESS);
    bt::Node::setCurrentTickId(1);
    std::ignore = leaf->tick();
    EXPECT_EQ(leaf->lastTickId(), 1u);
}

TEST(ActivePath, UntickedNodeHasZeroStamp) {
    ControllableNode leaf("leaf", bt::Status::SUCCESS);
    EXPECT_EQ(leaf.lastTickId(), 0u);
}

TEST(ActivePath, SequenceStampsAllTickedNodes) {
    bt::Sequence seq("seq");
    auto* first  = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    bt::Node::setCurrentTickId(7);
    std::ignore = seq.tick();
    EXPECT_EQ(seq.lastTickId(),    7u);
    EXPECT_EQ(first->lastTickId(), 7u);
    EXPECT_EQ(second->lastTickId(), 7u);
}

TEST(ActivePath, SequenceDoesNotStampSkippedNodes) {
    bt::Sequence seq("seq");
    auto* first  = seq.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    bt::Node::setCurrentTickId(3);
    std::ignore = seq.tick();
    EXPECT_EQ(first->lastTickId(),  3u);  // was ticked — failed
    EXPECT_EQ(second->lastTickId(), 0u);  // was skipped — not stamped
}

TEST(ActivePath, SelectorStampsOnlyTriedNodes) {
    bt::Selector sel("sel");
    auto* first  = sel.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    auto* second = sel.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    auto* third  = sel.addChildAndGet(makeLeaf("c", bt::Status::SUCCESS));
    bt::Node::setCurrentTickId(5);
    std::ignore = sel.tick();
    EXPECT_EQ(first->lastTickId(),  5u);
    EXPECT_EQ(second->lastTickId(), 5u);
    EXPECT_EQ(third->lastTickId(),  0u);  // selector short-circuits on second SUCCESS
}

TEST(ActivePath, BehaviorTreeEmitsActivePathInTickRecord) {
    bt::DecisionEmitter emitter;
    bt::BehaviorBuilder builder;
    builder.behavior("patrol").onTick([] { return bt::Status::RUNNING; });
    auto tree = bt::TreeAssembler::assemble(
        std::vector<bt::BehaviorEntry>(builder.entries().begin(), builder.entries().end()));
    tree.setEmitter(&emitter);

    std::ignore = tree.tick();
    ASSERT_EQ(emitter.history().size(), 1u);
    const auto& path = emitter.history()[0].activePath;
    EXPECT_FALSE(path.empty());
    // Root selector, behavior sequence, and the BehaviorAction leaf should all be present.
    EXPECT_NE(std::find(path.begin(), path.end(), "root"), path.end());
    EXPECT_NE(std::find(path.begin(), path.end(), "patrol"), path.end());
}

TEST(ActivePath, ActivePathIncludesDeepNestedNodes) {
    // Selector → Sequence(cond, action)
    bt::Selector sel("root");
    auto seq = std::make_unique<bt::Sequence>("seq");
    auto* cond   = seq->addChildAndGet(makeLeaf("cond",   bt::Status::SUCCESS));
    auto* action = seq->addChildAndGet(makeLeaf("action", bt::Status::RUNNING));
    sel.addChild(std::move(seq));

    bt::DecisionEmitter emitter;
    auto tree = bt::BehaviorTree(
        [&]() -> std::unique_ptr<bt::Node> {
            auto root = std::make_unique<bt::Selector>("root");
            auto sequence = std::make_unique<bt::Sequence>("seq");
            sequence->addChild(makeLeaf("cond",   bt::Status::SUCCESS));
            sequence->addChild(makeLeaf("action", bt::Status::RUNNING));
            root->addChild(std::move(sequence));
            return root;
        }());
    tree.setEmitter(&emitter);

    std::ignore = tree.tick();
    ASSERT_EQ(emitter.history().size(), 1u);
    const auto& path = emitter.history()[0].activePath;
    // All four nodes were ticked: root, seq, cond, action
    EXPECT_EQ(path.size(), 4u);
    EXPECT_EQ(path[0], "root");
    EXPECT_EQ(path[1], "seq");
    EXPECT_EQ(path[2], "cond");
    EXPECT_EQ(path[3], "action");
}

TEST(ActivePath, TickIdChangesEachBehaviorTreeTick) {
    ControllableNode leaf("leaf", bt::Status::RUNNING);
    bt::Node::setCurrentTickId(10);
    std::ignore = leaf.tick();
    EXPECT_EQ(leaf.lastTickId(), 10u);

    bt::Node::setCurrentTickId(11);
    std::ignore = leaf.tick();
    EXPECT_EQ(leaf.lastTickId(), 11u);
}
