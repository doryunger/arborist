#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bt/BehaviorBuilder.h"
#include "bt/MockEngine.h"
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"
#include "bt/Simulator.h"
#include "bt/Status.h"
#include "bt/TreeAssembler.h"

// ═══════════════════════════════════════════════════════════════════════════════
// MockEngine
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase4_MockEngine, ConditionReadsStateKey) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto* condFn = reg.findCondition("enemy_near");
    ASSERT_NE(condFn, nullptr);
    EXPECT_FALSE((*condFn)());

    engine.setState("enemy_near", true);
    EXPECT_TRUE((*condFn)());
}

TEST(Phase4_MockEngine, ActionReturnsConfiguredStatus) {
    bt::MockEngine engine;
    engine.addAction("fire", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto* actionFn = reg.findAction("fire");
    ASSERT_NE(actionFn, nullptr);
    EXPECT_EQ((*actionFn)(), bt::Status::RUNNING);
}

TEST(Phase4_MockEngine, ActionRecordsCallCount) {
    bt::MockEngine engine;
    engine.addAction("wander", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto* actionFn = reg.findAction("wander");
    ASSERT_NE(actionFn, nullptr);
    (*actionFn)();
    (*actionFn)();
    (*actionFn)();
    EXPECT_EQ(engine.callCount("wander"), 3);
}

TEST(Phase4_MockEngine, StateChangeAffectsLiveCondition) {
    bt::MockEngine engine;
    engine.setState("flag", false);
    engine.addCondition("flag_check", "flag");

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto* condFn = reg.findCondition("flag_check");
    EXPECT_FALSE((*condFn)());
    engine.setState("flag", true);
    EXPECT_TRUE((*condFn)());
}

TEST(Phase4_MockEngine, MultipleActionsTrackedIndependently) {
    bt::MockEngine engine;
    engine.addAction("move", bt::Status::SUCCESS);
    engine.addAction("shoot", bt::Status::SUCCESS);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    (*reg.findAction("move"))();
    (*reg.findAction("move"))();
    (*reg.findAction("shoot"))();

    EXPECT_EQ(engine.callCount("move"), 2);
    EXPECT_EQ(engine.callCount("shoot"), 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Simulator
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

const std::string kPatrolAttackYaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: fire
  - name: patrol
    tree:
      type: action
      name: wander
)";

}  // namespace

TEST(Phase4_Simulator, RunsForNTicks) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("wander", bt::Status::RUNNING);
    engine.addAction("fire", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(kPatrolAttackYaml, reg);
    bt::Simulator sim(std::move(tree));
    auto result = sim.run(5);

    EXPECT_EQ(result.ticksRun, 5);
    EXPECT_EQ(result.history.size(), 5);
}

TEST(Phase4_Simulator, StopsEarlyOnTerminalStatus) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("wander", bt::Status::SUCCESS);  // terminal
    engine.addAction("fire", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(kPatrolAttackYaml, reg);
    bt::Simulator sim(std::move(tree));
    auto result = sim.run(10);

    EXPECT_EQ(result.ticksRun, 1);  // stops after first SUCCESS
    EXPECT_EQ(result.finalStatus, bt::Status::SUCCESS);
}

TEST(Phase4_Simulator, StateChangeAtTickFiringOrder) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("wander", bt::Status::RUNNING);
    engine.addAction("fire", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(kPatrolAttackYaml, reg);
    bt::Simulator sim(std::move(tree));

    // Before tick 4: enemy appears
    sim.atTick(4, [&engine] { engine.setState("enemy_near", true); });
    auto result = sim.run(6);

    // Ticks 1-3: patrol (enemy not near)
    EXPECT_EQ(result.history[0].behaviorName, "patrol");
    EXPECT_EQ(result.history[2].behaviorName, "patrol");
    // Tick 4 onward: attack
    EXPECT_EQ(result.history[3].behaviorName, "attack");
    EXPECT_EQ(result.history[5].behaviorName, "attack");
}

TEST(Phase4_Simulator, MultipleHooksAtSameTick) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.setState("flag", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("wander", bt::Status::RUNNING);
    engine.addAction("fire", bt::Status::RUNNING);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(kPatrolAttackYaml, reg);
    bt::Simulator sim(std::move(tree));

    int hookCallCount = 0;
    sim.atTick(2, [&hookCallCount] { ++hookCallCount; });
    sim.atTick(2, [&hookCallCount] { ++hookCallCount; });
    sim.run(3);

    EXPECT_EQ(hookCallCount, 2);
}

TEST(Phase4_Simulator, IntegrationScenario_PatrolThenAttack) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("wander", bt::Status::RUNNING);
    engine.addAction("fire", bt::Status::SUCCESS);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(kPatrolAttackYaml, reg);
    bt::Simulator sim(std::move(tree));
    sim.atTick(3, [&engine] { engine.setState("enemy_near", true); });
    auto result = sim.run(10);

    // First two ticks patrol, third tick enemy appears and attack succeeds
    EXPECT_EQ(result.history[0].behaviorName, "patrol");
    EXPECT_EQ(result.history[1].behaviorName, "patrol");
    EXPECT_EQ(result.history[2].behaviorName, "attack");
    EXPECT_EQ(result.finalStatus, bt::Status::SUCCESS);
    EXPECT_EQ(result.ticksRun, 3);
}

// ═══════════════════════════════════════════════════════════════════════════════
// BehaviorTree::reload
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase4_Reload, NewRootRunsAfterReload) {
    bt::BehaviorBuilder builder;
    builder.behavior("first").onTick([] { return bt::Status::RUNNING; });
    auto tree = bt::TreeAssembler::assemble(builder.entries());

    EXPECT_EQ(tree.tick(), bt::Status::RUNNING);

    bt::BehaviorBuilder builder2;
    builder2.behavior("second").onTick([] { return bt::Status::SUCCESS; });
    tree.reload(bt::TreeAssembler::assemble(builder2.entries()));

    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase4_Reload, TickCountPreservedAfterReload) {
    bt::BehaviorBuilder builder;
    builder.behavior("first").onTick([] { return bt::Status::RUNNING; });
    auto tree = bt::TreeAssembler::assemble(builder.entries());

    std::ignore = tree.tick();
    std::ignore = tree.tick();
    EXPECT_EQ(tree.tickCount(), 2);

    bt::BehaviorBuilder builder2;
    builder2.behavior("second").onTick([] { return bt::Status::RUNNING; });
    tree.reload(bt::TreeAssembler::assemble(builder2.entries()));

    std::ignore = tree.tick();
    EXPECT_EQ(tree.tickCount(), 3);  // preserved across reload
}

TEST(Phase4_Reload, RunningStateResetOnReload) {
    int enterCount = 0;
    bt::BehaviorBuilder builder;
    builder.behavior("first")
        .onEnter([&enterCount] { ++enterCount; })
        .onTick([] { return bt::Status::RUNNING; });
    auto tree = bt::TreeAssembler::assemble(builder.entries());

    std::ignore = tree.tick();
    std::ignore = tree.tick();
    EXPECT_EQ(enterCount, 1);  // onEnter called once while RUNNING

    // Reload resets state — new tree's onEnter fires fresh
    int enterCount2 = 0;
    bt::BehaviorBuilder builder2;
    builder2.behavior("second")
        .onEnter([&enterCount2] { ++enterCount2; })
        .onTick([] { return bt::Status::RUNNING; });
    tree.reload(bt::TreeAssembler::assemble(builder2.entries()));

    std::ignore = tree.tick();
    EXPECT_EQ(enterCount2, 1);  // new behavior starts fresh
}

TEST(Phase4_Reload, ReloadFromSchemaLoader) {
    const std::string yaml1 = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    const std::string yaml2 = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    tree:
      type: action
      name: fire
)";

    bt::MockEngine engine;
    engine.addAction("wander", bt::Status::RUNNING);
    engine.addAction("fire", bt::Status::SUCCESS);

    bt::RuntimeRegistry reg(":memory:");
    engine.applyTo(reg);

    auto tree = bt::SchemaLoader::load(yaml1, reg);
    EXPECT_EQ(tree.tick(), bt::Status::RUNNING);

    tree.reload(bt::SchemaLoader::load(yaml2, reg));
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}
