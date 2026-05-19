#include <gtest/gtest.h>

#include <any>
#include <string>

#include "bt/ContractValidator.h"
#include "bt/MockEngine.h"
#include "bt/RuntimeRegistry.h"
#include "bt/ScenarioRunner.h"
#include "bt/ScenarioStep.h"
#include "bt/SchemaLoader.h"
#include "bt/Status.h"

// ── Shared YAML ───────────────────────────────────────────────────────────────

namespace {

const std::string kPatrolAttackYaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: attack
  - name: patrol
    tree:
      type: action
      name: patrol
)";

void applyEngine(bt::MockEngine& engine, bt::RuntimeRegistry& reg) {
    engine.applyTo(reg);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// ScenarioRunner
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ScenarioRunner, RunsForMaxTicksWithNoExpectations) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    auto result = runner.run(5);

    EXPECT_EQ(result.ticksRun, 5u);
    EXPECT_EQ(result.history.size(), 5u);
}

TEST(ScenarioRunner, StepPassesWhenBehaviorMatchesExpected) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.expect(1, "patrol");

    auto result = runner.run(3);
    ASSERT_EQ(result.stepResults.size(), 1u);
    EXPECT_TRUE(result.stepResults[0].passed);
    EXPECT_EQ(result.stepResults[0].actualBehavior, "patrol");
}

TEST(ScenarioRunner, StepFailsWhenBehaviorMismatch) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.expect(1, "attack");  // wrong: patrol should run when enemy not near

    auto result = runner.run(3);
    ASSERT_EQ(result.stepResults.size(), 1u);
    EXPECT_FALSE(result.stepResults[0].passed);
    EXPECT_EQ(result.stepResults[0].expectedBehavior, "attack");
    EXPECT_EQ(result.stepResults[0].actualBehavior, "patrol");
}

TEST(ScenarioRunner, StateHookFiresBeforeExpectedTick) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.atTick(4, [&engine] { engine.setState("enemy_near", true); });
    runner.expect(3, "patrol");
    runner.expect(4, "attack");

    auto result = runner.run(6);
    ASSERT_EQ(result.stepResults.size(), 2u);
    EXPECT_TRUE(result.stepResults[0].passed);  // tick 3: patrol
    EXPECT_TRUE(result.stepResults[1].passed);  // tick 4: attack
}

TEST(ScenarioRunner, AllPassedTrueWhenAllStepsPass) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.expect(1, "patrol");
    runner.expect(2, "patrol");

    auto result = runner.run(3);
    EXPECT_TRUE(result.allPassed);
}

TEST(ScenarioRunner, AllPassedFalseWhenAnyStepFails) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.expect(1, "patrol");
    runner.expect(2, "attack");  // will fail

    auto result = runner.run(3);
    EXPECT_FALSE(result.allPassed);
}

TEST(ScenarioRunner, StopsEarlyOnTerminalStatus) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::SUCCESS);  // terminal
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    auto result = runner.run(10);

    EXPECT_EQ(result.ticksRun, 1u);
    EXPECT_EQ(result.finalStatus, bt::Status::SUCCESS);
}

TEST(ScenarioRunner, MultipleHooksAtSameTick) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::RUNNING);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    int callCount = 0;
    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.atTick(2, [&callCount] { ++callCount; });
    runner.atTick(2, [&callCount] { ++callCount; });
    runner.run(3);

    EXPECT_EQ(callCount, 2);
}

TEST(ScenarioRunner, IntegrationScenario_PatrolThenAttackThenStop) {
    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("patrol", bt::Status::RUNNING);
    engine.addAction("attack", bt::Status::SUCCESS);
    bt::RuntimeRegistry reg(":memory:");
    applyEngine(engine, reg);

    bt::ScenarioRunner runner(bt::SchemaLoader::load(kPatrolAttackYaml, reg));
    runner.atTick(3, [&engine] { engine.setState("enemy_near", true); });
    runner.expect(1, "patrol");
    runner.expect(2, "patrol");
    runner.expect(3, "attack");

    auto result = runner.run(10);

    EXPECT_TRUE(result.allPassed);
    EXPECT_EQ(result.finalStatus, bt::Status::SUCCESS);
    EXPECT_EQ(result.ticksRun, 3u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// ContractValidator
// ═══════════════════════════════════════════════════════════════════════════════

// Helpers: build ScenarioResult directly from known tick records so the
// validator tests stay focused on contract logic, not tree setup.

static bt::ScenarioResult makeResult(std::vector<bt::TickRecord> records) {
    bt::ScenarioResult result;
    result.history = std::move(records);
    result.ticksRun = result.history.size();
    result.allPassed = true;
    result.finalStatus = bt::Status::RUNNING;
    return result;
}

static bt::TickRecord makeRecord(std::size_t tick,
                                  const std::string& behavior,
                                  bt::Status status,
                                  std::unordered_map<std::string, std::any> snapshot) {
    bt::TickRecord record;
    record.tickNumber = tick;
    record.behaviorName = behavior;
    record.result = status;
    record.blackboardSnapshot = std::move(snapshot);
    return record;
}

TEST(ContractValidator, CleanContractPassesValidation) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack")
        .reads("enemy_near")
        .writes("target")
        .impl([] { return bt::Status::SUCCESS; });

    // Blackboard has enemy_near (read satisfied), target changes (write observed)
    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING,
                   {{"enemy_near", std::any{true}}, {"target", std::any{0}}}),
        makeRecord(2, "attack", bt::Status::SUCCESS,
                   {{"enemy_near", std::any{true}}, {"target", std::any{1}}}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);
    EXPECT_TRUE(violations.empty());
}

TEST(ContractValidator, DeclaredReadNotSatisfied) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack").reads("enemy_near").impl([] { return bt::Status::SUCCESS; });

    // enemy_near is declared as a read but never appears in the blackboard
    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING, {}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    ASSERT_FALSE(violations.empty());
    EXPECT_EQ(violations[0].type, bt::ViolationType::READ_NOT_SATISFIED);
    EXPECT_EQ(violations[0].key, "enemy_near");
    EXPECT_EQ(violations[0].behaviorName, "attack");
}

TEST(ContractValidator, DeclaredWriteNotObserved) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack").writes("target").impl([] { return bt::Status::SUCCESS; });

    // target is declared as a write but never changes across the run
    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING, {{"target", std::any{0}}}),
        makeRecord(2, "attack", bt::Status::RUNNING, {{"target", std::any{0}}}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    ASSERT_FALSE(violations.empty());
    EXPECT_EQ(violations[0].type, bt::ViolationType::WRITE_NOT_OBSERVED);
    EXPECT_EQ(violations[0].key, "target");
}

TEST(ContractValidator, UndeclaredWriteDetected) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack").reads("enemy_near").impl([] { return bt::Status::SUCCESS; });
    // target changes but is NOT declared in writes

    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING,
                   {{"enemy_near", std::any{true}}, {"target", std::any{0}}}),
        makeRecord(2, "attack", bt::Status::RUNNING,
                   {{"enemy_near", std::any{true}}, {"target", std::any{1}}}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    auto undeclared = std::find_if(violations.begin(), violations.end(), [](const auto& val) {
        return val.type == bt::ViolationType::UNDECLARED_WRITE;
    });
    ASSERT_NE(undeclared, violations.end());
    EXPECT_EQ(undeclared->key, "target");
    EXPECT_EQ(undeclared->behaviorName, "attack");
}

TEST(ContractValidator, MultipleViolationsReported) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack")
        .reads("enemy_near")   // never in blackboard → READ_NOT_SATISFIED
        .writes("target")      // never changes → WRITE_NOT_OBSERVED
        .impl([] { return bt::Status::SUCCESS; });

    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING, {}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    EXPECT_GE(violations.size(), 2u);
}

TEST(ContractValidator, ViolationMessageIsNonEmpty) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack").reads("health").impl([] { return bt::Status::SUCCESS; });

    auto result = makeResult({makeRecord(1, "attack", bt::Status::RUNNING, {})});

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    ASSERT_FALSE(violations.empty());
    EXPECT_FALSE(violations[0].message.empty());
}

TEST(ContractValidator, BehaviorWithNoContractSkipped) {
    bt::RuntimeRegistry reg(":memory:");
    // "patrol" has no declared reads or writes in the registry

    auto result = makeResult({
        makeRecord(1, "patrol", bt::Status::RUNNING, {}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);
    EXPECT_TRUE(violations.empty());
}

TEST(ContractValidator, OnlyChecksKeysForActiveBehavior) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack").writes("target");
    // "patrol" runs on tick 1 and target doesn't change then — but patrol
    // has no declared writes, so no violation expected for that tick.
    // Attack runs on tick 2 and target changes → clean contract.

    auto result = makeResult({
        makeRecord(1, "patrol",  bt::Status::RUNNING, {{"target", std::any{0}}}),
        makeRecord(2, "attack",  bt::Status::RUNNING, {{"target", std::any{1}}}),
    });

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);
    EXPECT_TRUE(violations.empty());
}

TEST(ContractValidator, CustomBehaviorToActionMapping) {
    bt::RuntimeRegistry reg(":memory:");
    // Action in registry is named "fire", but behavior in tree is named "attack"
    reg.action("fire").reads("enemy_near");

    // Blackboard has enemy_near — contract is satisfied when we know to look up "fire"
    auto result = makeResult({
        makeRecord(1, "attack", bt::Status::RUNNING, {{"enemy_near", std::any{true}}}),
    });

    std::unordered_map<std::string, std::string> mapping{{"attack", "fire"}};
    bt::ContractValidator validator(reg.store(), std::move(mapping));
    auto violations = validator.validate(result);
    EXPECT_TRUE(violations.empty());
}

TEST(ContractValidator, IntentIncludedInViolationMessage) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("attack")
        .intent("Engage the nearest enemy")
        .reads("health")  // never in blackboard
        .impl([] { return bt::Status::SUCCESS; });

    auto result = makeResult({makeRecord(1, "attack", bt::Status::RUNNING, {})});

    bt::ContractValidator validator(reg.store());
    auto violations = validator.validate(result);

    ASSERT_FALSE(violations.empty());
    EXPECT_NE(violations[0].message.find("Engage the nearest enemy"), std::string::npos);
}
