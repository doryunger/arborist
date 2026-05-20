#include <gtest/gtest.h>

#include <string>
#include <tuple>

#include "bt/capi.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Phase 11 — Engine Adapters: C API bridge
// ═══════════════════════════════════════════════════════════════════════════════

// YAML helpers — minimal single-behavior and two-behavior schemas.

namespace {

constexpr const char* kSimpleYaml =
    "schema_version: \"1.0\"\n"
    "behaviors:\n"
    "  - name: beh\n"
    "    tree:\n"
    "      type: action\n"
    "      name: act\n";

// Two behaviors: attack (gated by enemy_visible condition) and patrol (fallback).
constexpr const char* kTwoBehYaml =
    "schema_version: \"1.0\"\n"
    "behaviors:\n"
    "  - name: attack\n"
    "    when: enemy_visible\n"
    "    tree:\n"
    "      type: action\n"
    "      name: attack_act\n"
    "  - name: patrol\n"
    "    tree:\n"
    "      type: action\n"
    "      name: patrol_act\n";

constexpr const char* kBadYaml = "not: valid: yaml: [[[";

}  // namespace

// ───────────────────────────────────────────────────────────────────────────────
// Registry lifecycle
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Registry, CreateAndDestroyRegistry) {
    bt_handle_t reg = bt_registry_create();
    ASSERT_NE(reg, nullptr);
    bt_registry_destroy(reg);
}

TEST(Phase11_Registry, DestroyNullRegistryIsSafe) {
    bt_registry_destroy(nullptr);  // must not crash
}

// ───────────────────────────────────────────────────────────────────────────────
// Action registration
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Registry, ActionSuccessTickReturnsSuccess) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    EXPECT_EQ(bt_tree_tick(tree), BT_SUCCESS);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Registry, ActionFailureTickReturnsFailure) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_FAILURE; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    EXPECT_EQ(bt_tree_tick(tree), BT_FAILURE);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Registry, ActionRunningTickReturnsRunning) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_RUNNING; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    EXPECT_EQ(bt_tree_tick(tree), BT_RUNNING);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Registry, ActionContextIsPassedThrough) {
    int callCount = 0;
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* ctx) -> BtCStatus {
            ++(*static_cast<int*>(ctx));
            return BT_SUCCESS;
        },
        &callCount);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    std::ignore = bt_tree_tick(tree);
    std::ignore = bt_tree_tick(tree);
    EXPECT_EQ(callCount, 2);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

// ───────────────────────────────────────────────────────────────────────────────
// Condition registration
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Registry, ConditionGatesBehavior) {
    bool enemyVisible = false;
    std::string lastBehavior;

    bt_handle_t reg = bt_registry_create();

    bt_registry_add_condition(reg, "enemy_visible",
        [](void* ctx) -> int { return *static_cast<bool*>(ctx) ? 1 : 0; },
        &enemyVisible);

    bt_registry_add_action(reg, "attack_act",
        [](void* ctx) -> BtCStatus {
            *static_cast<std::string*>(ctx) = "attack";
            return BT_SUCCESS;
        },
        &lastBehavior);

    bt_registry_add_action(reg, "patrol_act",
        [](void* ctx) -> BtCStatus {
            *static_cast<std::string*>(ctx) = "patrol";
            return BT_SUCCESS;
        },
        &lastBehavior);

    bt_handle_t tree = bt_tree_load(reg, kTwoBehYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    // Enemy not visible → patrol runs
    std::ignore = bt_tree_tick(tree);
    EXPECT_EQ(lastBehavior, "patrol");

    // Enemy visible → attack runs
    enemyVisible = true;
    std::ignore = bt_tree_tick(tree);
    EXPECT_EQ(lastBehavior, "attack");

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

// ───────────────────────────────────────────────────────────────────────────────
// Blackboard sources
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Registry, DoubleSourceReadableAfterTick) {
    double health = 75.0;
    bt_handle_t reg = bt_registry_create();

    bt_registry_add_double_source(reg, "health",
        [](void* ctx) -> double { return *static_cast<double*>(ctx); },
        &health);

    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    std::ignore = bt_tree_tick(tree);

    EXPECT_DOUBLE_EQ(bt_tree_get_double(tree, "health"), 75.0);

    health = 30.0;
    std::ignore = bt_tree_tick(tree);
    EXPECT_DOUBLE_EQ(bt_tree_get_double(tree, "health"), 30.0);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Registry, BoolSourceReadableAfterTick) {
    int flagValue = 1;
    bt_handle_t reg = bt_registry_create();

    bt_registry_add_bool_source(reg, "flag",
        [](void* ctx) -> int { return *static_cast<int*>(ctx); },
        &flagValue);

    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr) << bt_last_error();

    std::ignore = bt_tree_tick(tree);
    EXPECT_NE(bt_tree_get_bool(tree, "flag"), 0);

    flagValue = 0;
    std::ignore = bt_tree_tick(tree);
    EXPECT_EQ(bt_tree_get_bool(tree, "flag"), 0);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

// ───────────────────────────────────────────────────────────────────────────────
// Tree lifecycle and error handling
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Tree, BadYamlReturnsNullAndSetsError) {
    bt_handle_t reg = bt_registry_create();
    bt_handle_t tree = bt_tree_load(reg, kBadYaml);

    EXPECT_EQ(tree, nullptr);
    EXPECT_NE(std::string(bt_last_error()).empty(), true);

    bt_registry_destroy(reg);
}

TEST(Phase11_Tree, NullRegistryReturnsNull) {
    bt_handle_t tree = bt_tree_load(nullptr, kSimpleYaml);
    EXPECT_EQ(tree, nullptr);
}

TEST(Phase11_Tree, NullYamlReturnsNull) {
    bt_handle_t reg = bt_registry_create();
    bt_handle_t tree = bt_tree_load(reg, nullptr);
    EXPECT_EQ(tree, nullptr);
    bt_registry_destroy(reg);
}

TEST(Phase11_Tree, NullTreeTickReturnsFail) {
    EXPECT_EQ(bt_tree_tick(nullptr), BT_FAILURE);
}

TEST(Phase11_Tree, DestroyNullTreeIsSafe) {
    bt_tree_destroy(nullptr);  // must not crash
}

TEST(Phase11_Tree, MultipleTreesFromSameRegistry) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t treeA = bt_tree_load(reg, kSimpleYaml);
    bt_handle_t treeB = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(treeA, nullptr);
    ASSERT_NE(treeB, nullptr);
    EXPECT_NE(treeA, treeB);

    EXPECT_EQ(bt_tree_tick(treeA), BT_SUCCESS);
    EXPECT_EQ(bt_tree_tick(treeB), BT_SUCCESS);

    bt_tree_destroy(treeA);
    bt_tree_destroy(treeB);
    bt_registry_destroy(reg);
}

// ───────────────────────────────────────────────────────────────────────────────
// Blackboard read guards
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Blackboard, GetDoubleOnMissingKeyReturnsZero) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    EXPECT_DOUBLE_EQ(bt_tree_get_double(tree, "no_such_key"), 0.0);
    EXPECT_EQ(bt_tree_get_bool(tree, "no_such_key"), 0);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Blackboard, GetOnNullTreeReturnsDefault) {
    EXPECT_DOUBLE_EQ(bt_tree_get_double(nullptr, "key"), 0.0);
    EXPECT_EQ(bt_tree_get_bool(nullptr, "key"), 0);
}

// ───────────────────────────────────────────────────────────────────────────────
// Monitor server
// ───────────────────────────────────────────────────────────────────────────────

TEST(Phase11_Monitor, StartAndStopMonitor) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    EXPECT_EQ(bt_monitor_start(tree, 18086), BT_SUCCESS);
    EXPECT_EQ(bt_tree_tick(tree), BT_SUCCESS);
    bt_monitor_stop(tree);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Monitor, StartIsIdempotent) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    EXPECT_EQ(bt_monitor_start(tree, 18087), BT_SUCCESS);
    EXPECT_EQ(bt_monitor_start(tree, 18087), BT_SUCCESS);  // second call is a no-op
    bt_monitor_stop(tree);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Monitor, StopWithoutStartIsSafe) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    bt_monitor_stop(tree);  // never started — must not crash

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Monitor, NullTreeIsSafe) {
    EXPECT_EQ(bt_monitor_start(nullptr, 18088), BT_FAILURE);
    bt_monitor_stop(nullptr);  // must not crash
}

TEST(Phase11_Monitor, TicksAfterMonitorStartFeedHistory) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    EXPECT_EQ(bt_monitor_start(tree, 18089), BT_SUCCESS);
    // Three ticks — the monitor should accumulate them without error.
    for (int idx = 0; idx < 3; ++idx) {
        EXPECT_EQ(bt_tree_tick(tree), BT_SUCCESS);
    }
    bt_monitor_stop(tree);

    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}

TEST(Phase11_Monitor, DestroyWithRunningMonitorIsSafe) {
    bt_handle_t reg = bt_registry_create();
    bt_registry_add_action(reg, "act",
        [](void* /*ctx*/) -> BtCStatus { return BT_SUCCESS; }, nullptr);

    bt_handle_t tree = bt_tree_load(reg, kSimpleYaml);
    ASSERT_NE(tree, nullptr);

    EXPECT_EQ(bt_monitor_start(tree, 18090), BT_SUCCESS);
    std::ignore = bt_tree_tick(tree);
    // Destroy without explicitly stopping — CTree destructor must stop cleanly.
    bt_tree_destroy(tree);
    bt_registry_destroy(reg);
}
