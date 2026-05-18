#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <tuple>

#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/TreeUtils.h"

namespace {

// A leaf node whose status is set externally.
// tickCount_ accumulates across resets so resumption tests can verify
// exactly which nodes were ticked on each tick.
// resetCount_ increments on each reset() call so we can verify
// that CompositeNode::reset() propagates to children.
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

// ── CompositeNode ─────────────────────────────────────────────────────────────

TEST(Step3_CompositeNode, ChildrenAreStoredInOrder) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("c", bt::Status::SUCCESS));
    EXPECT_EQ(seq.children().size(), 3);
    EXPECT_EQ(seq.children()[0]->name(), "a");
    EXPECT_EQ(seq.children()[1]->name(), "b");
    EXPECT_EQ(seq.children()[2]->name(), "c");
}

TEST(Step3_CompositeNode, ResetResetsChildIndex) {
    bt::Sequence seq("seq");
    auto* first = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    std::ignore = seq.tick();  // first succeeds, second runs → RUNNING, index stops at second
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    seq.reset();  // index resets to 0

    std::ignore = seq.tick();  // starts from first again
    EXPECT_EQ(first->tickCount(), 2);   // re-ticked — index was reset
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Step3_CompositeNode, ResetPropagatestoChildren) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::RUNNING));

    std::ignore = seq.tick();
    EXPECT_EQ(child->resetCount(), 0);

    seq.reset();
    EXPECT_EQ(child->resetCount(), 1);  // reset propagated
}

// ── Sequence ──────────────────────────────────────────────────────────────────

TEST(Step3_Sequence, EmptySequenceSucceeds) {
    bt::Sequence seq("seq");
    EXPECT_EQ(seq.tick(), bt::Status::SUCCESS);
}

TEST(Step3_Sequence, AllChildrenSucceed) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::SUCCESS);
}

TEST(Step3_Sequence, FirstChildFailureShortCircuits) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::FAILURE));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::FAILURE);
    EXPECT_EQ(second->tickCount(), 0);  // never reached
}

TEST(Step3_Sequence, MiddleChildFailureShortCircuits) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::FAILURE));
    auto* third = seq.addChildAndGet(makeLeaf("c", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::FAILURE);
    EXPECT_EQ(third->tickCount(), 0);
}

TEST(Step3_Sequence, RunningChildPausesSequence) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::RUNNING));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
}

TEST(Step3_Sequence, RunningResumption) {
    bt::Sequence seq("seq");
    auto* first = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = seq.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    // Tick 1: first succeeds, second returns RUNNING
    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    // Tick 2: resumes at second — first must NOT be re-ticked
    EXPECT_EQ(seq.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);   // unchanged — RUNNING resumption verified
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Step3_Sequence, ResetsAfterSuccess) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 1);
    EXPECT_EQ(child->resetCount(), 1);  // reset propagated after success
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 2);   // started fresh
}

TEST(Step3_Sequence, ResetsAfterFailure) {
    bt::Sequence seq("seq");
    auto* child = seq.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 1);
    EXPECT_EQ(child->resetCount(), 1);  // reset propagated after failure
    std::ignore = seq.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

// ── Selector ──────────────────────────────────────────────────────────────────

TEST(Step3_Selector, EmptySelectorFails) {
    bt::Selector sel("sel");
    EXPECT_EQ(sel.tick(), bt::Status::FAILURE);
}

TEST(Step3_Selector, FirstChildSuccessShortCircuits) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::SUCCESS));
    auto* second = sel.addChildAndGet(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(sel.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(second->tickCount(), 0);
}

TEST(Step3_Selector, AllChildrenFail) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::FAILURE));
    sel.addChild(makeLeaf("b", bt::Status::FAILURE));
    EXPECT_EQ(sel.tick(), bt::Status::FAILURE);
}

TEST(Step3_Selector, FirstFailThenSucceed) {
    bt::Selector sel("sel");
    sel.addChild(makeLeaf("a", bt::Status::FAILURE));
    sel.addChild(makeLeaf("b", bt::Status::SUCCESS));
    EXPECT_EQ(sel.tick(), bt::Status::SUCCESS);
}

TEST(Step3_Selector, RunningResumption) {
    bt::Selector sel("sel");
    auto* first = sel.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    auto* second = sel.addChildAndGet(makeLeaf("b", bt::Status::RUNNING));

    // Tick 1: first fails, second returns RUNNING
    EXPECT_EQ(sel.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);
    EXPECT_EQ(second->tickCount(), 1);

    // Tick 2: resumes at second — first must NOT be re-ticked
    EXPECT_EQ(sel.tick(), bt::Status::RUNNING);
    EXPECT_EQ(first->tickCount(), 1);  // unchanged — RUNNING resumption verified
    EXPECT_EQ(second->tickCount(), 2);
}

TEST(Step3_Selector, ResetsAfterSuccess) {
    bt::Selector sel("sel");
    auto* child = sel.addChildAndGet(makeLeaf("a", bt::Status::SUCCESS));
    std::ignore = sel.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = sel.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

TEST(Step3_Selector, ResetsAfterFailure) {
    bt::Selector sel("sel");
    auto* child = sel.addChildAndGet(makeLeaf("a", bt::Status::FAILURE));
    std::ignore = sel.tick();
    EXPECT_EQ(child->resetCount(), 1);
    std::ignore = sel.tick();
    EXPECT_EQ(child->tickCount(), 2);
}

// ── treeToString ──────────────────────────────────────────────────────────────

TEST(Step3_TreeUtils, SingleLeafNode) {
    ControllableNode leaf("patrol");
    EXPECT_EQ(bt::treeToString(leaf), "[TestLeaf] patrol\n");
}

TEST(Step3_TreeUtils, SingleLeafEmptyName) {
    ControllableNode leaf("");
    EXPECT_EQ(bt::treeToString(leaf), "[TestLeaf]\n");
}

TEST(Step3_TreeUtils, SequenceWithChildren) {
    bt::Sequence seq("seq");
    seq.addChild(makeLeaf("a", bt::Status::SUCCESS));
    seq.addChild(makeLeaf("b", bt::Status::SUCCESS));
    std::string expected =
        "[Sequence] seq\n"
        "├── [TestLeaf] a\n"
        "└── [TestLeaf] b\n";
    EXPECT_EQ(bt::treeToString(seq), expected);
}

TEST(Step3_TreeUtils, NestedTree) {
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

// ── Integration ───────────────────────────────────────────────────────────────
//
// Tree:
//   [Selector] root
//   ├── [Sequence] attack_sequence
//   │   ├── [TestLeaf] enemy_visible
//   │   └── [TestLeaf] attack
//   └── [TestLeaf] patrol

class Step3Integration : public ::testing::Test {
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

TEST_F(Step3Integration, TreeStructureMatchesExpected) {
    std::string expected =
        "[Selector] root\n"
        "├── [Sequence] attack_sequence\n"
        "│   ├── [TestLeaf] enemy_visible\n"
        "│   └── [TestLeaf] attack\n"
        "└── [TestLeaf] patrol\n";
    EXPECT_EQ(bt::treeToString(root_), expected);
}

TEST_F(Step3Integration, EnemyNotVisible_PatrolRuns) {
    enemyVisible_->setStatus(bt::Status::FAILURE);
    patrol_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(enemyVisible_->tickCount(), 1);
    EXPECT_EQ(attack_->tickCount(), 0);
    EXPECT_EQ(patrol_->tickCount(), 1);
}

TEST_F(Step3Integration, EnemyVisible_AttackRunning_TreeRunning) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::RUNNING);
    EXPECT_EQ(root_.tick(), bt::Status::RUNNING);
    EXPECT_EQ(enemyVisible_->tickCount(), 1);
    EXPECT_EQ(attack_->tickCount(), 1);
    EXPECT_EQ(patrol_->tickCount(), 0);
}

TEST_F(Step3Integration, AttackResumedWithoutReCheckingEnemyVisible) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::RUNNING);
    std::ignore = root_.tick();  // tick 1: both nodes ticked → RUNNING

    std::ignore = root_.tick();  // tick 2: resumes at attack — enemy_visible must NOT be re-ticked
    EXPECT_EQ(enemyVisible_->tickCount(), 1);  // RUNNING resumption verified
    EXPECT_EQ(attack_->tickCount(), 2);
}

TEST_F(Step3Integration, AttackSucceeds_TreeSucceeds) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(patrol_->tickCount(), 0);
}

TEST_F(Step3Integration, AttackFails_PatrolFallback) {
    enemyVisible_->setStatus(bt::Status::SUCCESS);
    attack_->setStatus(bt::Status::FAILURE);
    patrol_->setStatus(bt::Status::SUCCESS);
    EXPECT_EQ(root_.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(patrol_->tickCount(), 1);
}
