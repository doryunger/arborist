#pragma once

// C API bridge — extern "C" integration surface for engine adapters.
//
// This header is the only file a Unity NativePlugin, Unreal plugin, or Godot
// GDExtension needs to include.  All framework functionality is accessed via
// opaque bt_handle_t handles; no C++ types are exposed.
//
// Adapter contract (three points):
//   1. Register blackboard sources   — bt_registry_add_*_source()
//   2. Register action/condition impl — bt_registry_add_action/condition()
//   3. Drive the tick loop           — bt_tree_tick() each frame / update
//
// Error handling: functions that can fail return nullptr / BT_FAILURE and
// store a description retrievable via bt_last_error().  The string is valid
// until the next C API call on the same thread.
//
// Thread safety: each registry and tree handle must be used from one thread
// at a time.  Sharing handles across threads requires external locking.

// This header uses C naming conventions (typedef, snake_case) so it remains
// includable from C as well as C++.  Suppress the C++ style checks here.
// NOLINTBEGIN(readability-identifier-naming,modernize-use-using,performance-enum-size,readability-identifier-length)

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle — points to a heap-allocated C++ object.
typedef void* bt_handle_t;

// Mirrors bt::Status.
typedef enum {
    BT_SUCCESS = 0,
    BT_FAILURE = 1,
    BT_RUNNING  = 2
} BtCStatus;

// ── Function pointer types ────────────────────────────────────────────────────

// Action implementation: receives caller-supplied context, returns status.
typedef BtCStatus (*bt_action_fn_t)(void* ctx);

// Condition implementation: returns 1 for true, 0 for false.
typedef int (*bt_condition_fn_t)(void* ctx);

// Blackboard source returning a double value.
typedef double (*bt_double_source_fn_t)(void* ctx);

// Blackboard source returning a bool value (non-zero = true).
typedef int (*bt_bool_source_fn_t)(void* ctx);

// ── Registry ──────────────────────────────────────────────────────────────────

// Create a new registry.  Must be destroyed with bt_registry_destroy().
bt_handle_t bt_registry_create(void);

// Destroy a registry.  Safe to call with nullptr.
void bt_registry_destroy(bt_handle_t reg);

// Register an action implementation.  func is called each time the action
// node is ticked; ctx is forwarded to func unchanged.
void bt_registry_add_action(bt_handle_t reg, const char* name,
                              bt_action_fn_t func, void* ctx);

// Register a condition implementation used for behavior 'when:' gates.
// func returns non-zero when the behavior is eligible.
void bt_registry_add_condition(bt_handle_t reg, const char* name,
                                bt_condition_fn_t func, void* ctx);

// Register a double-typed blackboard source.  func is called on every tick
// to refresh the value stored at 'key'.
void bt_registry_add_double_source(bt_handle_t reg, const char* key,
                                    bt_double_source_fn_t func, void* ctx);

// Register a bool-typed blackboard source.  Non-zero return = true.
void bt_registry_add_bool_source(bt_handle_t reg, const char* key,
                                  bt_bool_source_fn_t func, void* ctx);

// ── Tree ──────────────────────────────────────────────────────────────────────

// Parse yaml and build a BehaviorTree using the registered implementations.
// Returns nullptr on error; call bt_last_error() for details.
// The registry may be reused to build multiple independent trees.
bt_handle_t bt_tree_load(bt_handle_t reg, const char* yaml);

// Destroy a tree.  Safe to call with nullptr.
void bt_tree_destroy(bt_handle_t tree);

// Advance the tree by one tick.  Returns BT_FAILURE if tree is nullptr.
BtCStatus bt_tree_tick(bt_handle_t tree);

// ── Blackboard read-back ──────────────────────────────────────────────────────
// Values reflect the state captured during the last bt_tree_tick() call.
// Returns 0 / 0.0 if the key does not exist or tree is nullptr.

double bt_tree_get_double(bt_handle_t tree, const char* key);
int    bt_tree_get_bool(bt_handle_t tree, const char* key);

// ── Error reporting ───────────────────────────────────────────────────────────

// Returns the last error message set on this thread, or "" if none.
const char* bt_last_error(void);

#ifdef __cplusplus
}
#endif

// NOLINTEND(readability-identifier-naming,modernize-use-using,performance-enum-size,readability-identifier-length)
