#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bt/RegistrySpec.h"
#include "bt/RegistryStore.h"
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaSerializer.h"
#include "bt/SchemaParser.h"
#include "bt/Status.h"

// ═══════════════════════════════════════════════════════════════════════════════
// RegistryStore — SQLite persistence
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase3_RegistryStore, StoresAndRetrievesAction) {
    bt::RegistryStore store(":memory:");
    bt::ActionSpec spec{"fire_weapon", "Fire at target", {}, {}};
    store.upsertAction(spec);

    auto result = store.findAction("fire_weapon");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "fire_weapon");
    EXPECT_EQ(result->intent, "Fire at target");
}

TEST(Phase3_RegistryStore, StoresAndRetrievesCondition) {
    bt::RegistryStore store(":memory:");
    bt::ConditionSpec spec{"has_ammo", "True when ammo > 0", {}};
    store.upsertCondition(spec);

    auto result = store.findCondition("has_ammo");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->name, "has_ammo");
    EXPECT_EQ(result->intent, "True when ammo > 0");
}

TEST(Phase3_RegistryStore, StoresAndRetrievesStateKey) {
    bt::RegistryStore store(":memory:");
    store.upsertStateKey("ammo_count", "int");

    auto keys = store.allStateKeys();
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0].key, "ammo_count");
    EXPECT_EQ(keys[0].type, "int");
}

TEST(Phase3_RegistryStore, StoresActionReadsAndWrites) {
    bt::RegistryStore store(":memory:");
    bt::ActionSpec spec{"fire_weapon", "", {"ammo_count", "target_visible"}, {"ammo_count"}};
    store.upsertAction(spec);

    auto result = store.findAction("fire_weapon");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->reads.size(), 2);
    EXPECT_EQ(result->writes.size(), 1);
    EXPECT_EQ(result->writes[0], "ammo_count");
}

TEST(Phase3_RegistryStore, StoresConditionReads) {
    bt::RegistryStore store(":memory:");
    bt::ConditionSpec spec{"has_ammo", "", {"ammo_count"}};
    store.upsertCondition(spec);

    auto result = store.findCondition("has_ammo");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->reads.size(), 1);
    EXPECT_EQ(result->reads[0], "ammo_count");
}

TEST(Phase3_RegistryStore, UpsertUpdatesExistingAction) {
    bt::RegistryStore store(":memory:");
    store.upsertAction({"fire_weapon", "old intent", {}, {}});
    store.upsertAction({"fire_weapon", "new intent", {"ammo_count"}, {}});

    auto result = store.findAction("fire_weapon");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->intent, "new intent");
    EXPECT_EQ(result->reads.size(), 1);
}

TEST(Phase3_RegistryStore, AllActionsReturnsAll) {
    bt::RegistryStore store(":memory:");
    store.upsertAction({"fire_weapon", "", {}, {}});
    store.upsertAction({"reload", "", {}, {}});

    auto actions = store.allActions();
    EXPECT_EQ(actions.size(), 2);
}

TEST(Phase3_RegistryStore, MissingActionReturnsNullopt) {
    bt::RegistryStore store(":memory:");
    EXPECT_FALSE(store.findAction("ghost").has_value());
}

// ═══════════════════════════════════════════════════════════════════════════════
// RuntimeRegistry — fluent builder API
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase3_RuntimeRegistry, ActionSpecStoredOnImpl) {
    bt::RuntimeRegistry reg(":memory:");
    reg.action("fire_weapon")
        .intent("Fire at target")
        .reads("ammo_count")
        .writes("enemy_health")
        .impl([] { return bt::Status::SUCCESS; });

    auto spec = reg.store().findAction("fire_weapon");
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->intent, "Fire at target");
    EXPECT_EQ(spec->reads.size(), 1);
    EXPECT_EQ(spec->writes.size(), 1);
}

TEST(Phase3_RuntimeRegistry, ConditionSpecStoredOnImpl) {
    bt::RuntimeRegistry reg(":memory:");
    reg.condition("has_ammo")
        .intent("Ammo check")
        .reads("ammo_count")
        .impl([] { return true; });

    auto spec = reg.store().findCondition("has_ammo");
    ASSERT_TRUE(spec.has_value());
    EXPECT_EQ(spec->intent, "Ammo check");
    ASSERT_EQ(spec->reads.size(), 1);
    EXPECT_EQ(spec->reads[0], "ammo_count");
}

TEST(Phase3_RuntimeRegistry, StateKeyStored) {
    bt::RuntimeRegistry reg(":memory:");
    reg.state("ammo_count", "int");
    reg.state("target_visible", "bool");

    auto keys = reg.store().allStateKeys();
    EXPECT_EQ(keys.size(), 2);
}

TEST(Phase3_RuntimeRegistry, ActionImplIsCallable) {
    bt::RuntimeRegistry reg(":memory:");
    int calls = 0;
    reg.action("fire_weapon")
        .impl([&calls] { ++calls; return bt::Status::SUCCESS; });

    auto fn = reg.findAction("fire_weapon");
    ASSERT_TRUE(fn != nullptr);
    EXPECT_EQ((*fn)(), bt::Status::SUCCESS);
    EXPECT_EQ(calls, 1);
}

TEST(Phase3_RuntimeRegistry, ConditionImplIsCallable) {
    bt::RuntimeRegistry reg(":memory:");
    bool flag = false;
    reg.condition("is_ready")
        .impl([&flag] { return flag; });

    auto fn = reg.findCondition("is_ready");
    ASSERT_TRUE(fn != nullptr);
    EXPECT_FALSE((*fn)());
    flag = true;
    EXPECT_TRUE((*fn)());
}

TEST(Phase3_RuntimeRegistry, UnknownActionReturnsNullptr) {
    bt::RuntimeRegistry reg(":memory:");
    EXPECT_EQ(reg.findAction("ghost"), nullptr);
}

TEST(Phase3_RuntimeRegistry, UnknownConditionReturnsNullptr) {
    bt::RuntimeRegistry reg(":memory:");
    EXPECT_EQ(reg.findCondition("ghost"), nullptr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SchemaSerializer — RegistryStore → YAML catalog
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase3_SchemaSerializer, SerializesActions) {
    bt::RegistryStore store(":memory:");
    store.upsertAction({"fire_weapon", "Fire at target", {"ammo_count"}, {"enemy_health"}});

    std::string yaml = bt::SchemaSerializer::toYaml(store);
    EXPECT_NE(yaml.find("fire_weapon"), std::string::npos);
    EXPECT_NE(yaml.find("ammo_count"), std::string::npos);
    EXPECT_NE(yaml.find("enemy_health"), std::string::npos);
}

TEST(Phase3_SchemaSerializer, SerializesConditions) {
    bt::RegistryStore store(":memory:");
    store.upsertCondition({"has_ammo", "Ammo check", {"ammo_count"}});

    std::string yaml = bt::SchemaSerializer::toYaml(store);
    EXPECT_NE(yaml.find("has_ammo"), std::string::npos);
}

TEST(Phase3_SchemaSerializer, SerializesStateKeys) {
    bt::RegistryStore store(":memory:");
    store.upsertStateKey("health", "int");
    store.upsertStateKey("target_visible", "bool");

    std::string yaml = bt::SchemaSerializer::toYaml(store);
    EXPECT_NE(yaml.find("health"), std::string::npos);
    EXPECT_NE(yaml.find("target_visible"), std::string::npos);
}

TEST(Phase3_SchemaSerializer, OutputIsValidYAML) {
    bt::RegistryStore store(":memory:");
    store.upsertStateKey("ammo_count", "int");
    store.upsertAction({"fire_weapon", "Fire", {"ammo_count"}, {"ammo_count"}});
    store.upsertCondition({"has_ammo", "Check ammo", {"ammo_count"}});

    std::string yaml = bt::SchemaSerializer::toYaml(store);
    // Must be parseable — schema_version is included by the serializer
    EXPECT_NO_THROW(bt::SchemaParser::parse(yaml));
}

// ═══════════════════════════════════════════════════════════════════════════════
// SchemaLoader with RuntimeRegistry
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Phase3_SchemaLoader, LoadsFromRuntimeRegistry) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: wander
)";
    bt::RuntimeRegistry reg(":memory:");
    bool called = false;
    reg.action("wander")
        .intent("Walk to random waypoint")
        .impl([&called] { called = true; return bt::Status::SUCCESS; });

    auto tree = bt::SchemaLoader::load(yaml, reg);
    std::ignore = tree.tick();
    EXPECT_TRUE(called);
}

TEST(Phase3_SchemaLoader, RuntimeRegistryConditionWires) {
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
    bt::RuntimeRegistry reg(":memory:");
    reg.condition("enemy_near").reads("enemy_near").impl([&enemyNear] { return enemyNear; });
    reg.action("fire").writes("enemy_health").impl([&fireCalls] {
        ++fireCalls;
        return bt::Status::RUNNING;
    });
    reg.action("wander").impl([] { return bt::Status::RUNNING; });

    auto tree = bt::SchemaLoader::load(yaml, reg);
    std::ignore = tree.tick();
    EXPECT_EQ(fireCalls, 0);

    enemyNear = true;
    std::ignore = tree.tick();
    EXPECT_EQ(fireCalls, 1);
}

TEST(Phase3_SchemaLoader, UnknownActionInRuntimeRegistryThrows) {
    const std::string yaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: ghost_action
)";
    bt::RuntimeRegistry reg(":memory:");
    EXPECT_THROW(bt::SchemaLoader::load(yaml, reg), bt::SchemaLoadError);
}
