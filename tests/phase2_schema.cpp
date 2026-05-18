#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "bt/SchemaNode.h"
#include "bt/SchemaParser.h"
#include "bt/SchemaValidator.h"
#include "bt/SchemaLoader.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Step 1 — Schema types + YAML parser
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase2_SchemaParser, ParsesSchemaVersion) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.schemaVersion, "1.0");
}

TEST(Phase2_SchemaParser, MissingVersionThrows) {
    const std::string yaml = R"(
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    EXPECT_THROW(bt::SchemaParser::parse(yaml), bt::SchemaParseError);
}

TEST(Phase2_SchemaParser, ParsesBehaviorName) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
  - name: attack
    tree:
      type: action
      name: fire
)";
    auto doc = bt::SchemaParser::parse(yaml);
    ASSERT_EQ(doc.behaviors.size(), 2);
    EXPECT_EQ(doc.behaviors[0].name, "patrol");
    EXPECT_EQ(doc.behaviors[1].name, "attack");
}

TEST(Phase2_SchemaParser, ParsesWhenCondition) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: fire
)";
    auto doc = bt::SchemaParser::parse(yaml);
    ASSERT_EQ(doc.behaviors.size(), 1);
    EXPECT_EQ(doc.behaviors[0].condition, "enemy_near");
}

TEST(Phase2_SchemaParser, ParsesInterruptibleFlag) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    interruptible: false
    tree:
      type: action
      name: fire
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_FALSE(doc.behaviors[0].interruptible);
}

TEST(Phase2_SchemaParser, DefaultInterruptibleIsTrue) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_TRUE(doc.behaviors[0].interruptible);
}

TEST(Phase2_SchemaParser, ParsesIntentAnnotation) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    intent: "Default roaming behavior"
    tree:
      type: action
      name: wander
      intent: "Walk to random point"
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.behaviors[0].intent, "Default roaming behavior");
    EXPECT_EQ(doc.behaviors[0].tree->intent, "Walk to random point");
}

TEST(Phase2_SchemaParser, ParsesStateDeclarations) {
    const std::string yaml = R"(
schema_version: "1.0"
state:
  - key: health
    type: int
  - key: enemy_near
    type: bool
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    auto doc = bt::SchemaParser::parse(yaml);
    ASSERT_EQ(doc.stateDeclarations.size(), 2);
    EXPECT_EQ(doc.stateDeclarations[0].key, "health");
    EXPECT_EQ(doc.stateDeclarations[0].type, "int");
    EXPECT_EQ(doc.stateDeclarations[1].key, "enemy_near");
    EXPECT_EQ(doc.stateDeclarations[1].type, "bool");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Step 2 — All node types
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase2_SchemaParser, ParsesActionNode) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    auto doc = bt::SchemaParser::parse(yaml);
    ASSERT_TRUE(doc.behaviors[0].tree);
    EXPECT_EQ(doc.behaviors[0].tree->type, bt::SchemaNodeType::ACTION);
    EXPECT_EQ(doc.behaviors[0].tree->name, "wander");
}

TEST(Phase2_SchemaParser, ParsesConditionNode) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: check
    tree:
      type: condition
      name: has_ammo
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.behaviors[0].tree->type, bt::SchemaNodeType::CONDITION);
    EXPECT_EQ(doc.behaviors[0].tree->name, "has_ammo");
}

TEST(Phase2_SchemaParser, ParsesSequenceNode) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    tree:
      type: sequence
      children:
        - type: condition
          name: has_ammo
        - type: action
          name: fire
)";
    auto doc = bt::SchemaParser::parse(yaml);
    auto& tree = doc.behaviors[0].tree;
    EXPECT_EQ(tree->type, bt::SchemaNodeType::SEQUENCE);
    ASSERT_EQ(tree->children.size(), 2);
    EXPECT_EQ(tree->children[0]->type, bt::SchemaNodeType::CONDITION);
    EXPECT_EQ(tree->children[1]->type, bt::SchemaNodeType::ACTION);
}

TEST(Phase2_SchemaParser, ParsesSelectorNode) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: fallback
    tree:
      type: selector
      children:
        - type: action
          name: try_primary
        - type: action
          name: try_secondary
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.behaviors[0].tree->type, bt::SchemaNodeType::SELECTOR);
    EXPECT_EQ(doc.behaviors[0].tree->children.size(), 2);
}

TEST(Phase2_SchemaParser, ParsesParallelNodeWithPolicy) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: multitask
    tree:
      type: parallel
      policy: any
      children:
        - type: action
          name: move
        - type: action
          name: shoot
)";
    auto doc = bt::SchemaParser::parse(yaml);
    auto& tree = doc.behaviors[0].tree;
    EXPECT_EQ(tree->type, bt::SchemaNodeType::PARALLEL);
    EXPECT_EQ(tree->policy, bt::SchemaPolicy::ANY);
    EXPECT_EQ(tree->children.size(), 2);
}

TEST(Phase2_SchemaParser, ParsesParallelAllPolicy) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: multitask
    tree:
      type: parallel
      policy: all
      children:
        - type: action
          name: move
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.behaviors[0].tree->policy, bt::SchemaPolicy::ALL);
}

TEST(Phase2_SchemaParser, ParsesParallelThresholdPolicy) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: multitask
    tree:
      type: parallel
      policy: threshold
      threshold: 2
      children:
        - type: action
          name: a
        - type: action
          name: b
        - type: action
          name: c
)";
    auto doc = bt::SchemaParser::parse(yaml);
    EXPECT_EQ(doc.behaviors[0].tree->policy, bt::SchemaPolicy::THRESHOLD);
    EXPECT_EQ(doc.behaviors[0].tree->threshold, 2);
}

TEST(Phase2_SchemaParser, ParsesDeepNestedTree) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: complex
    tree:
      type: selector
      children:
        - type: sequence
          children:
            - type: condition
              name: is_ready
            - type: action
              name: execute
        - type: action
          name: fallback
)";
    auto doc = bt::SchemaParser::parse(yaml);
    auto& root = doc.behaviors[0].tree;
    EXPECT_EQ(root->type, bt::SchemaNodeType::SELECTOR);
    EXPECT_EQ(root->children[0]->type, bt::SchemaNodeType::SEQUENCE);
    EXPECT_EQ(root->children[0]->children[0]->type, bt::SchemaNodeType::CONDITION);
}

TEST(Phase2_SchemaParser, UnknownNodeTypeThrows) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: bad
    tree:
      type: teleport
      name: somewhere
)";
    EXPECT_THROW(bt::SchemaParser::parse(yaml), bt::SchemaParseError);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Step 3 — Schema validator
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase2_SchemaValidator, EmptyBehaviorsIsError) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    auto results = bt::SchemaValidator::validate(doc);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, MissingTreeIsError) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    bt::BehaviorSchema b;
    b.name = "broken";
    // no tree
    doc.behaviors.push_back(std::move(b));
    auto results = bt::SchemaValidator::validate(doc);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, DuplicateNameIsError) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    for (int i = 0; i < 2; ++i) {
        bt::BehaviorSchema b;
        b.name = "dup";
        b.tree = std::make_unique<bt::SchemaNode>();
        b.tree->type = bt::SchemaNodeType::ACTION;
        b.tree->name = "act";
        doc.behaviors.push_back(std::move(b));
    }
    auto results = bt::SchemaValidator::validate(doc);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, NoDefaultBehaviorIsWarning) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    bt::BehaviorSchema b;
    b.name = "attack";
    b.condition = "enemy_near";
    b.tree = std::make_unique<bt::SchemaNode>();
    b.tree->type = bt::SchemaNodeType::ACTION;
    b.tree->name = "fire";
    doc.behaviors.push_back(std::move(b));
    auto results = bt::SchemaValidator::validate(doc);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return !r.isError(); }));
    EXPECT_FALSE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, ValidDocProducesNoErrors) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    {
        bt::BehaviorSchema b;
        b.name = "attack";
        b.condition = "enemy_near";
        b.tree = std::make_unique<bt::SchemaNode>();
        b.tree->type = bt::SchemaNodeType::ACTION;
        b.tree->name = "fire";
        doc.behaviors.push_back(std::move(b));
    }
    {
        bt::BehaviorSchema b;
        b.name = "patrol";
        b.tree = std::make_unique<bt::SchemaNode>();
        b.tree->type = bt::SchemaNodeType::ACTION;
        b.tree->name = "wander";
        doc.behaviors.push_back(std::move(b));
    }
    auto results = bt::SchemaValidator::validate(doc);
    EXPECT_FALSE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, UnknownConditionNameIsError) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    bt::BehaviorSchema b;
    b.name = "attack";
    b.condition = "ghost_condition";
    b.tree = std::make_unique<bt::SchemaNode>();
    b.tree->type = bt::SchemaNodeType::ACTION;
    b.tree->name = "fire";
    doc.behaviors.push_back(std::move(b));

    bt::SchemaRegistry registry;
    registry.conditions = {"enemy_near"};  // ghost_condition not registered
    auto results = bt::SchemaValidator::validate(doc, registry);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, UnknownActionNameIsError) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    bt::BehaviorSchema b;
    b.name = "patrol";
    b.tree = std::make_unique<bt::SchemaNode>();
    b.tree->type = bt::SchemaNodeType::ACTION;
    b.tree->name = "ghost_action";
    doc.behaviors.push_back(std::move(b));

    bt::SchemaRegistry registry;
    registry.actions = {"wander"};  // ghost_action not registered
    auto results = bt::SchemaValidator::validate(doc, registry);
    EXPECT_TRUE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

TEST(Phase2_SchemaValidator, KnownRefsProduceNoErrors) {
    bt::SchemaDoc doc;
    doc.schemaVersion = "1.0";
    bt::BehaviorSchema b;
    b.name = "attack";
    b.condition = "enemy_near";
    b.tree = std::make_unique<bt::SchemaNode>();
    b.tree->type = bt::SchemaNodeType::ACTION;
    b.tree->name = "fire";
    doc.behaviors.push_back(std::move(b));
    {
        bt::BehaviorSchema def;
        def.name = "patrol";
        def.tree = std::make_unique<bt::SchemaNode>();
        def.tree->type = bt::SchemaNodeType::ACTION;
        def.tree->name = "wander";
        doc.behaviors.push_back(std::move(def));
    }

    bt::SchemaRegistry registry;
    registry.conditions = {"enemy_near"};
    registry.actions = {"fire", "wander"};
    auto results = bt::SchemaValidator::validate(doc, registry);
    EXPECT_FALSE(std::any_of(results.begin(), results.end(),
        [](const auto& r) { return r.isError(); }));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Step 4 — Schema → runtime tree
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase2_SchemaLoader, LoadsSingleActionBehavior) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    bool wanderCalled = false;
    bt::LoaderRegistry reg;
    reg.actions["wander"] = [&wanderCalled] { wanderCalled = true; return bt::Status::SUCCESS; };

    auto tree = bt::SchemaLoader::load(yaml, reg);
    std::ignore = tree.tick();
    EXPECT_TRUE(wanderCalled);
}

TEST(Phase2_SchemaLoader, ConditionNodeWiresCorrectly) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: check
    tree:
      type: condition
      name: is_ready
)";
    bool ready = false;
    bt::LoaderRegistry reg;
    reg.conditions["is_ready"] = [&ready] { return ready; };

    auto tree = bt::SchemaLoader::load(yaml, reg);
    EXPECT_EQ(tree.tick(), bt::Status::FAILURE);
    ready = true;
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase2_SchemaLoader, SequenceNodeWiresCorrectly) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    tree:
      type: sequence
      children:
        - type: condition
          name: has_ammo
        - type: action
          name: fire
)";
    bool ammo = true;
    int fired = 0;
    bt::LoaderRegistry reg;
    reg.conditions["has_ammo"] = [&ammo] { return ammo; };
    reg.actions["fire"] = [&fired] { ++fired; return bt::Status::SUCCESS; };

    auto tree = bt::SchemaLoader::load(yaml, reg);
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
    EXPECT_EQ(fired, 1);

    ammo = false;
    EXPECT_EQ(tree.tick(), bt::Status::FAILURE);
    EXPECT_EQ(fired, 1);  // fire not called when condition fails
}

TEST(Phase2_SchemaLoader, WhenConditionWiresPriorityCorrectly) {
    const std::string yaml = R"(
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
    bool enemyNear = false;
    int fireCalls = 0;
    int wanderCalls = 0;
    bt::LoaderRegistry reg;
    reg.conditions["enemy_near"] = [&enemyNear] { return enemyNear; };
    reg.actions["fire"] = [&fireCalls] { ++fireCalls; return bt::Status::RUNNING; };
    reg.actions["wander"] = [&wanderCalls] { ++wanderCalls; return bt::Status::RUNNING; };

    auto tree = bt::SchemaLoader::load(yaml, reg);
    std::ignore = tree.tick();
    EXPECT_EQ(wanderCalls, 1);
    EXPECT_EQ(fireCalls, 0);

    enemyNear = true;
    std::ignore = tree.tick();
    EXPECT_EQ(fireCalls, 1);
}

TEST(Phase2_SchemaLoader, ParallelNodeWithAnyPolicy) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: multitask
    tree:
      type: parallel
      policy: any
      children:
        - type: action
          name: a
        - type: action
          name: b
)";
    bt::LoaderRegistry reg;
    reg.actions["a"] = [] { return bt::Status::SUCCESS; };
    reg.actions["b"] = [] { return bt::Status::FAILURE; };

    auto tree = bt::SchemaLoader::load(yaml, reg);
    EXPECT_EQ(tree.tick(), bt::Status::SUCCESS);
}

TEST(Phase2_SchemaLoader, UnknownActionThrows) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: ghost_action
)";
    bt::LoaderRegistry reg;
    EXPECT_THROW(bt::SchemaLoader::load(yaml, reg), bt::SchemaLoadError);
}

TEST(Phase2_SchemaLoader, UnknownConditionThrows) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: ghost_condition
    tree:
      type: action
      name: fire
  - name: patrol
    tree:
      type: action
      name: wander
)";
    bt::LoaderRegistry reg;
    reg.actions["fire"] = [] { return bt::Status::SUCCESS; };
    reg.actions["wander"] = [] { return bt::Status::SUCCESS; };
    EXPECT_THROW(bt::SchemaLoader::load(yaml, reg), bt::SchemaLoadError);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Step 5 — Subtree composition
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase2_Subtree, SubtreeReferenceExpandsInline) {
    const std::string rootYaml = R"(
schema_version: "1.0"
import:
  - combat
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    const std::string combatYaml = R"(
schema_version: "1.0"
subtree: combat
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: fire
)";
    bool enemyNear = false;
    int fireCalls = 0;
    int wanderCalls = 0;
    bt::LoaderRegistry reg;
    reg.conditions["enemy_near"] = [&enemyNear] { return enemyNear; };
    reg.actions["fire"] = [&fireCalls] { ++fireCalls; return bt::Status::RUNNING; };
    reg.actions["wander"] = [&wanderCalls] { ++wanderCalls; return bt::Status::RUNNING; };

    bt::SchemaManifest manifest;
    manifest.add("combat", combatYaml);

    auto tree = bt::SchemaLoader::loadWithManifest(rootYaml, manifest, reg);
    std::ignore = tree.tick();
    EXPECT_EQ(wanderCalls, 1);

    enemyNear = true;
    std::ignore = tree.tick();
    EXPECT_EQ(fireCalls, 1);
}

TEST(Phase2_Subtree, CycleDetectionThrows) {
    const std::string aYaml = R"(
schema_version: "1.0"
subtree: a
import:
  - b
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    const std::string bYaml = R"(
schema_version: "1.0"
subtree: b
import:
  - a
behaviors:
  - name: attack
    tree:
      type: action
      name: fire
)";
    bt::LoaderRegistry reg;
    reg.actions["wander"] = [] { return bt::Status::SUCCESS; };
    reg.actions["fire"] = [] { return bt::Status::SUCCESS; };

    bt::SchemaManifest manifest;
    manifest.add("a", aYaml);
    manifest.add("b", bYaml);

    EXPECT_THROW(bt::SchemaLoader::loadWithManifest(aYaml, manifest, reg), bt::SchemaCycleError);
}

TEST(Phase2_Subtree, UnknownImportThrows) {
    const std::string yaml = R"(
schema_version: "1.0"
import:
  - nonexistent
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    bt::LoaderRegistry reg;
    reg.actions["wander"] = [] { return bt::Status::SUCCESS; };

    bt::SchemaManifest manifest;
    EXPECT_THROW(bt::SchemaLoader::loadWithManifest(yaml, manifest, reg), bt::SchemaLoadError);
}
