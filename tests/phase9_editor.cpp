#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "bt/EditorServer.h"
#include "bt/RegistryStore.h"
#include "bt/RegistrySpec.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 9 — Editor Server API
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

void populateStore(bt::RegistryStore& store) {
    store.upsertAction({"attack",        "Strike the current target",        {"enemy_pos"}, {"damage_dealt"}});
    store.upsertAction({"reload",        "Reload the active weapon",         {},            {"ammo_count"}});
    store.upsertAction({"walk_waypoint", "Move to the next patrol waypoint", {"waypoints"}, {}});
    store.upsertCondition({"enemy_visible", "True when an enemy is in line-of-sight", {"enemy_pos"}});
    store.upsertCondition({"ammo_low",      "True when ammo drops below threshold",   {"ammo_count"}});
    store.upsertStateKey("enemy_pos",    "vec3");
    store.upsertStateKey("ammo_count",   "int");
    store.upsertStateKey("damage_dealt", "int");
    store.upsertStateKey("waypoints",    "list");
}

static constexpr std::string_view kTestSchemaYaml = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    when: enemy_visible
    tree:
      type: sequence
      name: combat_seq
      children:
        - type: action
          name: attack
        - type: condition
          name: ammo_low
        - type: action
          name: reload
  - name: patrol
    tree:
      type: action
      name: walk_waypoint
)";

}  // namespace

// ── Actions JSON ───────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, ActionsJsonContainsRegisteredActions) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getActionsJson();
    EXPECT_NE(json.find("attack"),        std::string::npos);
    EXPECT_NE(json.find("reload"),        std::string::npos);
    EXPECT_NE(json.find("walk_waypoint"), std::string::npos);
}

TEST(Phase9_EditorServer, ActionsJsonIncludesIntent) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getActionsJson();
    EXPECT_NE(json.find("Strike the current target"), std::string::npos);
}

TEST(Phase9_EditorServer, ActionsJsonIncludesReadsAndWrites) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getActionsJson();
    EXPECT_NE(json.find("enemy_pos"),    std::string::npos);
    EXPECT_NE(json.find("damage_dealt"), std::string::npos);
}

TEST(Phase9_EditorServer, ActionsJsonEmptyWhenNoActions) {
    bt::RegistryStore emptyStore(":memory:");
    bt::EditorServer editor(emptyStore);
    EXPECT_EQ(editor.getActionsJson(), "[]");
}

// ── Conditions JSON ────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, ConditionsJsonContainsRegisteredConditions) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getConditionsJson();
    EXPECT_NE(json.find("enemy_visible"), std::string::npos);
    EXPECT_NE(json.find("ammo_low"),      std::string::npos);
}

TEST(Phase9_EditorServer, ConditionsJsonIncludesIntent) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getConditionsJson();
    EXPECT_NE(json.find("line-of-sight"), std::string::npos);
}

TEST(Phase9_EditorServer, ConditionsJsonEmptyWhenNone) {
    bt::RegistryStore emptyStore(":memory:");
    bt::EditorServer editor(emptyStore);
    EXPECT_EQ(editor.getConditionsJson(), "[]");
}

// ── Blackboard JSON ────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, BlackboardJsonContainsDeclaredKeys) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getBlackboardJson();
    EXPECT_NE(json.find("enemy_pos"),  std::string::npos);
    EXPECT_NE(json.find("ammo_count"), std::string::npos);
    EXPECT_NE(json.find("waypoints"),  std::string::npos);
}

TEST(Phase9_EditorServer, BlackboardJsonIncludesTypes) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getBlackboardJson();
    EXPECT_NE(json.find("vec3"), std::string::npos);
    EXPECT_NE(json.find("int"),  std::string::npos);
}

// ── Schema JSON (no file) ──────────────────────────────────────────────────────

TEST(Phase9_EditorServer, SchemaJsonWithNoFileHasEmptyYaml) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);  // no file path
    const std::string json = editor.getSchemaJson();
    EXPECT_NE(json.find("\"yaml\""), std::string::npos);
    EXPECT_NE(json.find("\"path\""), std::string::npos);
}

// ── Schema JSON (with file) ────────────────────────────────────────────────────

TEST(Phase9_EditorServer, SchemaJsonReadsFileContent) {
    const std::string tmpPath = "/tmp/bt_test_schema_read.yaml";
    {
        std::ofstream f(tmpPath);
        f << kTestSchemaYaml;
    }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getSchemaJson();
    EXPECT_NE(json.find("schema_version"), std::string::npos);
    EXPECT_NE(json.find("combat"),         std::string::npos);
    EXPECT_NE(json.find("patrol"),         std::string::npos);

    std::filesystem::remove(tmpPath);
}

// ── Save schema ────────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, SaveSchemaReturnsFalseWithNoPath) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    EXPECT_FALSE(editor.saveSchema("schema_version: \"1.0\"\nbehaviors: []"));
}

TEST(Phase9_EditorServer, SaveSchemaWritesFile) {
    const std::string tmpPath = "/tmp/bt_test_schema_save.yaml";
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);

    EXPECT_TRUE(editor.saveSchema(kTestSchemaYaml));

    std::ifstream f(tmpPath);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("schema_version"), std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, SaveAndReloadRoundTrip) {
    const std::string tmpPath = "/tmp/bt_test_schema_roundtrip.yaml";
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);

    ASSERT_TRUE(editor.saveSchema(kTestSchemaYaml));
    const std::string json = editor.getSchemaJson();
    EXPECT_NE(json.find("combat"),  std::string::npos);
    EXPECT_NE(json.find("patrol"),  std::string::npos);

    std::filesystem::remove(tmpPath);
}

// ── Analyze ────────────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, AnalyzeWithNoFileReturnsError) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    const std::string json = editor.getAnalyzeJson();
    EXPECT_NE(json.find("error"), std::string::npos);
}

TEST(Phase9_EditorServer, AnalyzeCleanSchemaReturnsMetrics) {
    const std::string tmpPath = "/tmp/bt_test_schema_analyze.yaml";
    {
        std::ofstream f(tmpPath);
        f << kTestSchemaYaml;
    }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getAnalyzeJson();

    EXPECT_NE(json.find("totalNodes"),       std::string::npos);
    EXPECT_NE(json.find("maxDepth"),         std::string::npos);
    EXPECT_NE(json.find("avgBranchingFactor"), std::string::npos);
    EXPECT_NE(json.find("issues"),           std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, AnalyzeDetectsMissingFallback) {
    const std::string tmpPath = "/tmp/bt_test_schema_nofallback.yaml";
    // Both behaviors have conditions — no fallback.
    static constexpr std::string_view kNoFallback = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    when: enemy_visible
    tree:
      type: action
      name: attack
  - name: search
    when: ammo_low
    tree:
      type: action
      name: reload
)";
    {
        std::ofstream f(tmpPath);
        f << kNoFallback;
    }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getAnalyzeJson();
    EXPECT_NE(json.find("unconditional"), std::string::npos);  // issue message text

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, AnalyzeDetectsNoFallbackWarningMessage) {
    const std::string tmpPath = "/tmp/bt_test_schema_warn.yaml";
    static constexpr std::string_view kNoFallback = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    when: enemy_visible
    tree:
      type: action
      name: walk_waypoint
)";
    {
        std::ofstream f(tmpPath);
        f << kNoFallback;
    }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getAnalyzeJson();
    // Should have a WARNING for no unconditional fallback.
    EXPECT_NE(json.find("WARNING"), std::string::npos);

    std::filesystem::remove(tmpPath);
}

// ── Tree JSON ──────────────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, TreeJsonWithNoFileReturnsError) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);  // no file path
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("error"), std::string::npos);
}

TEST(Phase9_EditorServer, TreeJsonContainsBehaviorNames) {
    const std::string tmpPath = "/tmp/bt_test_tree_behaviors.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("\"combat\""),  std::string::npos);
    EXPECT_NE(json.find("\"patrol\""),  std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonContainsNodeTypes) {
    const std::string tmpPath = "/tmp/bt_test_tree_types.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("sequence"), std::string::npos);
    EXPECT_NE(json.find("action"),   std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonContainsConditionGating) {
    const std::string tmpPath = "/tmp/bt_test_tree_condition.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("enemy_visible"), std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonHasChildrenArray) {
    const std::string tmpPath = "/tmp/bt_test_tree_children.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("\"children\""), std::string::npos);

    std::filesystem::remove(tmpPath);
}

// ── HTTP server integration ────────────────────────────────────────────────────

TEST(Phase9_EditorServer, ServerStartsAndServes) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    editor.start(18081);
    EXPECT_TRUE(editor.running());
    editor.stop();
    EXPECT_FALSE(editor.running());
}
