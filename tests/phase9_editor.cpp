#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <httplib.h>

#include "bt/BehaviorTree.h"
#include "bt/EditorServer.h"
#include "bt/RegistryStore.h"
#include "bt/RegistrySpec.h"
#include "bt/SchemaLoader.h"

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

TEST(Phase9_EditorServer, TreeJsonIncludesNodeIntent) {
    static constexpr std::string_view kIntentYaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack_behavior
    tree:
      type: action
      name: attack
      intent: Strike the current target
)";
    const std::string tmpPath = "/tmp/bt_test_tree_intent.yaml";
    { std::ofstream f(tmpPath); f << kIntentYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("Strike the current target"), std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonParallelIncludesPolicy) {
    static constexpr std::string_view kParallelYaml = R"(
schema_version: "1.0"
behaviors:
  - name: multitask
    tree:
      type: parallel
      name: multi_root
      policy: any
      children:
        - type: action
          name: walk_waypoint
        - type: action
          name: attack
)";
    const std::string tmpPath = "/tmp/bt_test_tree_parallel.yaml";
    { std::ofstream f(tmpPath); f << kParallelYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("\"policy\""), std::string::npos);
    EXPECT_NE(json.find("\"any\""),    std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonNodeIdsAreSequential) {
    const std::string tmpPath = "/tmp/bt_test_tree_ids.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    // kTestSchemaYaml has 5 nodes: combat_seq, attack, ammo_low, reload, walk_waypoint
    EXPECT_NE(json.find("\"id\":0"), std::string::npos);
    EXPECT_NE(json.find("\"id\":4"), std::string::npos);
    EXPECT_EQ(json.find("\"id\":5"), std::string::npos);

    std::filesystem::remove(tmpPath);
}

// ── Contract authoring ────────────────────────────────────────────────────────

TEST(Phase9_EditorServer, UpsertActionAddsToStore) {
    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.putAction("shoot", "Fire the weapon", {"target_pos"}, {"ammo_count"});
    const std::string json = editor.getActionsJson();
    EXPECT_NE(json.find("shoot"),           std::string::npos);
    EXPECT_NE(json.find("Fire the weapon"), std::string::npos);
}

TEST(Phase9_EditorServer, UpsertConditionAddsToStore) {
    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.putCondition("has_ammo", "True when ammo > 0", {"ammo_count"});
    const std::string json = editor.getConditionsJson();
    EXPECT_NE(json.find("has_ammo"), std::string::npos);
}

TEST(Phase9_EditorServer, UpsertStateKeyAddsToStore) {
    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.putStateKey("health", "float");
    const std::string json = editor.getBlackboardJson();
    EXPECT_NE(json.find("health"), std::string::npos);
    EXPECT_NE(json.find("float"),  std::string::npos);
}

TEST(Phase9_EditorServer, RemoveActionDeletesFromStore) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    editor.removeAction("attack");
    const std::string json = editor.getActionsJson();
    EXPECT_EQ(json.find("\"attack\""), std::string::npos);
    EXPECT_NE(json.find("reload"),     std::string::npos);
}

TEST(Phase9_EditorServer, RemoveConditionDeletesFromStore) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    editor.removeCondition("enemy_visible");
    const std::string json = editor.getConditionsJson();
    EXPECT_EQ(json.find("\"enemy_visible\""), std::string::npos);
    EXPECT_NE(json.find("ammo_low"),          std::string::npos);
}

TEST(Phase9_EditorServer, RemoveStateKeyDeletesFromStore) {
    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store);
    editor.removeStateKey("enemy_pos");
    const std::string json = editor.getBlackboardJson();
    EXPECT_EQ(json.find("\"enemy_pos\""), std::string::npos);
    EXPECT_NE(json.find("ammo_count"),    std::string::npos);
}

// ── Node paths (Phase 9E) ──────────────────────────────────────────────────────

TEST(Phase9_EditorServer, TreeJsonNodePathIsPresent) {
    const std::string tmpPath = "/tmp/bt_test_tree_path.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    EXPECT_NE(json.find("\"path\""), std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonRootNodePathIsNodeName) {
    const std::string tmpPath = "/tmp/bt_test_tree_rootpath.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    // combat_seq is the root of the "combat" behavior — its path is just its name
    EXPECT_NE(json.find("\"path\":\"combat_seq\""), std::string::npos);

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, TreeJsonChildPathIncludesParentName) {
    const std::string tmpPath = "/tmp/bt_test_tree_childpath.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    const std::string json = editor.getTreeJson();
    // "attack" is a child of "combat_seq"
    EXPECT_NE(json.find("combat_seq/attack"), std::string::npos);

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

TEST(Phase9_EditorServer, HttpIntegration_AllEndpoints) {
    const std::string tmpPath = "/tmp/bt_test_http_integration.yaml";
    { std::ofstream f(tmpPath); f << kTestSchemaYaml; }

    bt::RegistryStore store(":memory:");
    populateStore(store);
    bt::EditorServer editor(store, tmpPath);
    editor.start(18084);

    httplib::Client client("localhost", 18084);
    client.set_connection_timeout(2);

    // ── read endpoints ─────────────────────────────────────────────────────────
    {
        auto res = client.Get("/");
        ASSERT_NE(res, nullptr) << "GET / failed";
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("Arborist"), std::string::npos);
    }
    {
        auto res = client.Get("/api/actions");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("attack"), std::string::npos);
    }
    {
        auto res = client.Get("/api/conditions");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("enemy_visible"), std::string::npos);
    }
    {
        auto res = client.Get("/api/blackboard");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("enemy_pos"), std::string::npos);
    }
    {
        auto res = client.Get("/api/schema");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("schema_version"), std::string::npos);
    }
    {
        auto res = client.Get("/api/analyze");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("totalNodes"), std::string::npos);
    }
    {
        auto res = client.Get("/api/tree");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("behaviors"), std::string::npos);
        EXPECT_NE(res->body.find("\"path\""),  std::string::npos);
    }

    // ── contract mutation ──────────────────────────────────────────────────────
    {
        const std::string body = R"({"name":"http_action","intent":"test","reads":["k1"],"writes":["k2"]})";
        auto res = client.Put("/api/actions", body, "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
        EXPECT_NE(res->body.find("ok"), std::string::npos);
    }
    {
        auto res = client.Get("/api/actions");
        ASSERT_NE(res, nullptr);
        EXPECT_NE(res->body.find("http_action"), std::string::npos);
    }
    {
        auto res = client.Delete("/api/actions/http_action");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }
    {
        auto res = client.Get("/api/actions");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->body.find("http_action"), std::string::npos);
    }
    {
        const std::string body = R"({"name":"http_cond","intent":"test","reads":[]})";
        auto res = client.Put("/api/conditions", body, "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }
    {
        auto res = client.Delete("/api/conditions/http_cond");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }
    {
        const std::string body = R"({"key":"http_key","type":"float"})";
        auto res = client.Put("/api/blackboard", body, "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }
    {
        auto res = client.Delete("/api/blackboard/http_key");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }

    // ── schema save + reload ───────────────────────────────────────────────────
    {
        const std::string newYaml =
            "schema_version: \"1.0\"\nbehaviors:\n"
            "  - name: patrol\n    tree:\n      type: action\n      name: walk_waypoint\n";
        const std::string body = "{\"yaml\":\"" +
            [&]{ std::string escaped; for (char chr : newYaml) {
                if (chr == '"') { escaped += "\\\""; }
                else if (chr == '\n') { escaped += "\\n"; }
                else { escaped += chr; }
            } return escaped; }() + "\"}";
        auto res = client.Post("/api/schema", body, "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 200);
    }
    {
        auto res = client.Get("/api/schema");
        ASSERT_NE(res, nullptr);
        EXPECT_NE(res->body.find("patrol"), std::string::npos);
    }

    // ── bad request validation ─────────────────────────────────────────────────
    {
        auto res = client.Put("/api/actions", R"({"intent":"no name"})", "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 400);
    }
    {
        auto res = client.Post("/api/schema", R"({"yaml":"invalid: yaml: :::"})", "application/json");
        ASSERT_NE(res, nullptr);
        EXPECT_EQ(res->status, 422);
    }

    editor.stop();
    std::filesystem::remove(tmpPath);
}

// ── Hot-reload: attachTree (Gap 6) ────────────────────────────────────────────

namespace {

static constexpr std::string_view kHotReloadPatrolYaml = R"(
schema_version: "1.0"
behaviors:
  - name: patrol
    tree:
      type: action
      name: walk_waypoint
)";

static constexpr std::string_view kHotReloadCombatYaml = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    tree:
      type: action
      name: attack
)";

bt::LoaderRegistry makeHotReloadRegistry() {
    bt::LoaderRegistry reg;
    reg.actions["walk_waypoint"] = [] { return bt::Status::RUNNING; };
    reg.actions["attack"]        = [] { return bt::Status::RUNNING; };
    return reg;
}

}  // namespace

TEST(Phase9_EditorServer, AttachTreeChangesActiveBehaviorAfterSave) {
    const std::string tmpPath = "/tmp/bt_test_reload_attach.yaml";
    { std::ofstream f(tmpPath); f << kHotReloadPatrolYaml; }

    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    ASSERT_EQ(tree.behaviors().size(), 1u);
    ASSERT_EQ(tree.behaviors()[0].name, "patrol");

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store, tmpPath);
    editor.attachTree(&tree, reg);

    ASSERT_TRUE(editor.saveSchema(kHotReloadCombatYaml));

    ASSERT_EQ(tree.behaviors().size(), 1u);
    EXPECT_EQ(tree.behaviors()[0].name, "combat");

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, SaveWithNoAttachedTreeDoesNotCrash) {
    const std::string tmpPath = "/tmp/bt_test_reload_noattach.yaml";
    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store, tmpPath);
    EXPECT_TRUE(editor.saveSchema(kHotReloadPatrolYaml));
    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, AttachTreeReloadFailsGracefullyOnUnknownAction) {
    const std::string tmpPath = "/tmp/bt_test_reload_unknown.yaml";
    { std::ofstream f(tmpPath); f << kHotReloadPatrolYaml; }

    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store, tmpPath);
    editor.attachTree(&tree, reg);

    // YAML references "fire_laser" which is not registered — SchemaLoader will throw.
    // File must be saved; tree must remain unchanged (no crash).
    static constexpr std::string_view kUnknownYaml = R"(
schema_version: "1.0"
behaviors:
  - name: combat
    tree:
      type: action
      name: fire_laser
)";

    EXPECT_TRUE(editor.saveSchema(kUnknownYaml));
    // Tree was not reloaded — original behavior still in place.
    ASSERT_EQ(tree.behaviors().size(), 1u);
    EXPECT_EQ(tree.behaviors()[0].name, "patrol");

    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, AttachTreeReloadsOnHttpPost) {
    const std::string tmpPath = "/tmp/bt_test_reload_http.yaml";
    { std::ofstream f(tmpPath); f << kHotReloadPatrolYaml; }

    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store, tmpPath);
    editor.attachTree(&tree, reg);
    editor.start(18086);

    httplib::Client client("localhost", 18086);
    client.set_connection_timeout(2);

    std::string escaped;
    for (char c : std::string(kHotReloadCombatYaml)) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\n') escaped += "\\n";
        else escaped += c;
    }
    const std::string body = "{\"yaml\":\"" + escaped + "\"}";
    auto res = client.Post("/api/schema", body, "application/json");
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 200);

    ASSERT_EQ(tree.behaviors().size(), 1u);
    EXPECT_EQ(tree.behaviors()[0].name, "combat");

    editor.stop();
    std::filesystem::remove(tmpPath);
}

TEST(Phase9_EditorServer, AttachTreeReloadedSchemaTicksCorrectly) {
    const std::string tmpPath = "/tmp/bt_test_reload_tick.yaml";
    { std::ofstream f(tmpPath); f << kHotReloadPatrolYaml; }

    int patrolTicks = 0;
    int combatTicks = 0;
    bt::LoaderRegistry reg;
    reg.actions["walk_waypoint"] = [&patrolTicks] { ++patrolTicks; return bt::Status::RUNNING; };
    reg.actions["attack"]        = [&combatTicks] { ++combatTicks; return bt::Status::RUNNING; };

    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.tick();
    EXPECT_EQ(patrolTicks, 1);
    EXPECT_EQ(combatTicks, 0);

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store, tmpPath);
    editor.attachTree(&tree, reg);
    ASSERT_TRUE(editor.saveSchema(kHotReloadCombatYaml));

    tree.tick();
    EXPECT_EQ(patrolTicks, 1);  // no new patrol ticks
    EXPECT_EQ(combatTicks, 1);  // combat ran

    std::filesystem::remove(tmpPath);
}

// ── Live tick overlay: attachEmitter / getTickStateJson (Gap 7) ───────────────

TEST(Phase9_EditorServer, TickStateJsonWithNoEmitterReturnsEmptyState) {
    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    const std::string json = editor.getTickStateJson();
    // Must be valid JSON with expected fields, no data.
    EXPECT_NE(json.find("\"tick\""),       std::string::npos);
    EXPECT_NE(json.find("\"behavior\""),   std::string::npos);
    EXPECT_NE(json.find("\"activePath\""), std::string::npos);
    EXPECT_NE(json.find("\"0\"") + json.find(":0"),  std::string::npos);  // tick is 0
}

TEST(Phase9_EditorServer, TickStateJsonReflectsLatestTickRecord) {
    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.setEmitter(&emitter);
    tree.tick();

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.attachEmitter(&emitter);

    const std::string json = editor.getTickStateJson();
    EXPECT_NE(json.find("\"tick\":1"),    std::string::npos);
    EXPECT_NE(json.find("patrol"),        std::string::npos);
}

TEST(Phase9_EditorServer, TickStateJsonContainsActivePath) {
    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.setEmitter(&emitter);
    tree.tick();

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.attachEmitter(&emitter);

    const std::string json = editor.getTickStateJson();
    EXPECT_NE(json.find("\"activePath\""), std::string::npos);
    // The patrol tree has at least one node: walk_waypoint
    EXPECT_NE(json.find("walk_waypoint"), std::string::npos);
}

TEST(Phase9_EditorServer, TickStateJsonUpdatesOnSubsequentTicks) {
    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.setEmitter(&emitter);

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.attachEmitter(&emitter);

    tree.tick();
    const std::string json1 = editor.getTickStateJson();
    EXPECT_NE(json1.find("\"tick\":1"), std::string::npos);

    tree.tick();
    const std::string json2 = editor.getTickStateJson();
    EXPECT_NE(json2.find("\"tick\":2"), std::string::npos);
}

TEST(Phase9_EditorServer, TickStateJsonActivePathHasStatus) {
    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.setEmitter(&emitter);
    tree.tick();

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.attachEmitter(&emitter);

    const std::string json = editor.getTickStateJson();
    // Each active-path entry must have a "status" field.
    EXPECT_NE(json.find("\"status\""), std::string::npos);
}

TEST(Phase9_EditorServer, HttpTickstateEndpointServes) {
    bt::LoaderRegistry reg = makeHotReloadRegistry();
    bt::DecisionEmitter emitter;
    bt::BehaviorTree tree = bt::SchemaLoader::load(kHotReloadPatrolYaml, reg);
    tree.setEmitter(&emitter);
    tree.tick();

    bt::RegistryStore store(":memory:");
    bt::EditorServer editor(store);
    editor.attachEmitter(&emitter);
    editor.start(18087);

    httplib::Client client("localhost", 18087);
    client.set_connection_timeout(2);
    auto res = client.Get("/api/tickstate");
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->status, 200);
    EXPECT_NE(res->body.find("patrol"),       std::string::npos);
    EXPECT_NE(res->body.find("activePath"),   std::string::npos);
    EXPECT_NE(res->body.find("walk_waypoint"), std::string::npos);

    editor.stop();
}
