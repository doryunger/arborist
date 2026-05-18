#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

#include "bt/Node.h"
#include "bt/Status.h"

namespace {

class FixedNode : public bt::Node {
public:
    explicit FixedNode(std::string nodeName, bt::Status result)
        : bt::Node(std::move(nodeName)), result_(result) {}

    bt::Status tick() override { return result_; }

private:
    bt::Status result_;
};

// Simulates a node that returns RUNNING until reset() is called.
class ResettableNode : public bt::Node {
public:
    explicit ResettableNode() : bt::Node("resettable") {}

    bt::Status tick() override {
        return resetCalled_ ? bt::Status::SUCCESS : bt::Status::RUNNING;
    }

    void reset() override { resetCalled_ = false; }

    void markReset() { resetCalled_ = true; }

private:
    bool resetCalled_{false};
};

}  // namespace

// ── Type trait guarantees ─────────────────────────────────────────────────────

static_assert(std::is_abstract_v<bt::Node>, "Node must be abstract");
static_assert(std::is_polymorphic_v<bt::Node>, "Node must be polymorphic");
static_assert(std::has_virtual_destructor_v<bt::Node>, "Node must have a virtual destructor");
static_assert(!std::is_copy_constructible_v<bt::Node>, "Node must not be copy constructible");
static_assert(!std::is_move_constructible_v<bt::Node>, "Node must not be move constructible");
static_assert(!std::is_copy_assignable_v<bt::Node>, "Node must not be copy assignable");
static_assert(!std::is_move_assignable_v<bt::Node>, "Node must not be move assignable");

// ── tick() ────────────────────────────────────────────────────────────────────

TEST(Step2_Node, TickReturnsSuccess) {
    FixedNode node("test", bt::Status::SUCCESS);
    EXPECT_EQ(node.tick(), bt::Status::SUCCESS);
}

TEST(Step2_Node, TickReturnsFailure) {
    FixedNode node("test", bt::Status::FAILURE);
    EXPECT_EQ(node.tick(), bt::Status::FAILURE);
}

TEST(Step2_Node, TickReturnsRunning) {
    FixedNode node("test", bt::Status::RUNNING);
    EXPECT_EQ(node.tick(), bt::Status::RUNNING);
}

TEST(Step2_Node, PolymorphicTickViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<FixedNode>("base", bt::Status::RUNNING);
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
}

// ── name() ────────────────────────────────────────────────────────────────────

TEST(Step2_Node, NameIsPreserved) {
    FixedNode node("patrol", bt::Status::SUCCESS);
    EXPECT_EQ(node.name(), "patrol");
}

TEST(Step2_Node, EmptyNameIsValid) {
    FixedNode node("", bt::Status::SUCCESS);
    EXPECT_EQ(node.name(), "");
}

TEST(Step2_Node, NameViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<FixedNode>("base", bt::Status::RUNNING);
    EXPECT_EQ(node->name(), "base");
}

// ── reset() ───────────────────────────────────────────────────────────────────

TEST(Step2_Node, DefaultResetIsNoOp) {
    FixedNode node("test", bt::Status::SUCCESS);
    node.reset();
    EXPECT_EQ(node.tick(), bt::Status::SUCCESS);
}

TEST(Step2_Node, ResetCanBeOverridden) {
    ResettableNode node;
    EXPECT_EQ(node.tick(), bt::Status::RUNNING);
    node.markReset();
    EXPECT_EQ(node.tick(), bt::Status::SUCCESS);
    node.reset();
    EXPECT_EQ(node.tick(), bt::Status::RUNNING);
}

TEST(Step2_Node, ResetViaBasePointer) {
    std::unique_ptr<bt::Node> node = std::make_unique<ResettableNode>();
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
    dynamic_cast<ResettableNode*>(node.get())->markReset();
    node->reset();
    EXPECT_EQ(node->tick(), bt::Status::RUNNING);
}
