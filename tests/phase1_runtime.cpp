#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "bt/Node.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/Status.h"
#include "bt/TreeUtils.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Test helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

class FixedNode : public bt::Node {
public:
    explicit FixedNode(std::string nodeName, bt::Status result)
        : bt::Node(std::move(nodeName)), result_(result) {}

    bt::Status tick() override { return result_; }
    [[nodiscard]] std::string_view typeName() const noexcept override { return "FixedNode"; }

private:
    bt::Status result_;
};

class ResettableNode : public bt::Node {
public:
    explicit ResettableNode() : bt::Node("resettable") {}

    bt::Status tick() override {
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

    bt::Status tick() override {
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
