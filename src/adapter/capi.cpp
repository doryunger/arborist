#include "bt/capi.h"

#include <memory>
#include <string>

#include "bt/Blackboard.h"
#include "bt/BehaviorTree.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaParser.h"
#include "bt/Status.h"

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local std::string tlLastError;

void setError(const std::string& msg) noexcept {
    try { tlLastError = msg; } catch (...) {}
}

void clearError() noexcept {
    try { tlLastError.clear(); } catch (...) {}
}

// Bundles LoaderRegistry and Blackboard so they travel together through the
// registry handle and are both available at bt_tree_load() time.
struct CRegistry {
    CRegistry() = default;
    ~CRegistry() = default;
    CRegistry(const CRegistry&) = delete;
    CRegistry& operator=(const CRegistry&) = delete;
    CRegistry(CRegistry&&) = delete;
    CRegistry& operator=(CRegistry&&) = delete;

    bt::LoaderRegistry loaderReg;
    bt::Blackboard     blackboard;
};

bt::Status capiStatusToCpp(BtCStatus status) noexcept {
    switch (status) {
        case BT_SUCCESS: return bt::Status::SUCCESS;
        case BT_FAILURE: return bt::Status::FAILURE;
        default:         return bt::Status::RUNNING;
    }
}

BtCStatus cppStatusToCapi(bt::Status status) noexcept {
    switch (status) {
        case bt::Status::SUCCESS: return BT_SUCCESS;
        case bt::Status::FAILURE: return BT_FAILURE;
        case bt::Status::RUNNING: return BT_RUNNING;
    }
    return BT_FAILURE;
}

}  // namespace

// NOLINTBEGIN(readability-identifier-naming,cppcoreguidelines-owning-memory)
extern "C" {

bt_handle_t bt_registry_create() {
    clearError();
    try {
        return new CRegistry{};
    } catch (const std::exception& exc) {
        setError(exc.what());
        return nullptr;
    }
}

void bt_registry_destroy(bt_handle_t reg) {
    delete static_cast<CRegistry*>(reg);
}

void bt_registry_add_action(bt_handle_t reg, const char* name,
                              bt_action_fn_t func, void* ctx) {
    if (reg == nullptr || name == nullptr || func == nullptr) { return; }
    auto& cReg = *static_cast<CRegistry*>(reg);
    cReg.loaderReg.actions[name] = [func, ctx] {
        return capiStatusToCpp(func(ctx));
    };
}

void bt_registry_add_condition(bt_handle_t reg, const char* name,
                                bt_condition_fn_t func, void* ctx) {
    if (reg == nullptr || name == nullptr || func == nullptr) { return; }
    auto& cReg = *static_cast<CRegistry*>(reg);
    cReg.loaderReg.conditions[name] = [func, ctx] {
        return func(ctx) != 0;
    };
}

void bt_registry_add_double_source(bt_handle_t reg, const char* key,
                                    bt_double_source_fn_t func, void* ctx) {
    if (reg == nullptr || key == nullptr || func == nullptr) { return; }
    auto& cReg = *static_cast<CRegistry*>(reg);
    cReg.blackboard.registerSource<double>(key, [func, ctx] {
        return func(ctx);
    });
}

void bt_registry_add_bool_source(bt_handle_t reg, const char* key,
                                  bt_bool_source_fn_t func, void* ctx) {
    if (reg == nullptr || key == nullptr || func == nullptr) { return; }
    auto& cReg = *static_cast<CRegistry*>(reg);
    cReg.blackboard.registerSource<bool>(key, [func, ctx] {
        return func(ctx) != 0;
    });
}

bt_handle_t bt_tree_load(bt_handle_t reg, const char* yaml) {
    clearError();
    if (reg == nullptr || yaml == nullptr) {
        setError("null argument passed to bt_tree_load");
        return nullptr;
    }
    try {
        auto& cReg = *static_cast<CRegistry*>(reg);
        auto doc = bt::SchemaParser::parse(yaml);
        auto tree = std::make_unique<bt::BehaviorTree>(
            bt::SchemaLoader::load(doc, cReg.loaderReg, cReg.blackboard));
        return tree.release();
    } catch (const std::exception& exc) {
        setError(exc.what());
        return nullptr;
    }
}

void bt_tree_destroy(bt_handle_t tree) {
    delete static_cast<bt::BehaviorTree*>(tree);
}

BtCStatus bt_tree_tick(bt_handle_t tree) {
    if (tree == nullptr) {
        setError("null tree handle passed to bt_tree_tick");
        return BT_FAILURE;
    }
    try {
        return cppStatusToCapi(static_cast<bt::BehaviorTree*>(tree)->tick());
    } catch (const std::exception& exc) {
        setError(exc.what());
        return BT_FAILURE;
    }
}

double bt_tree_get_double(bt_handle_t tree, const char* key) {
    if (tree == nullptr || key == nullptr) { return 0.0; }
    try {
        return static_cast<bt::BehaviorTree*>(tree)->blackboard().get<double>(key);
    } catch (...) {
        return 0.0;
    }
}

int bt_tree_get_bool(bt_handle_t tree, const char* key) {
    if (tree == nullptr || key == nullptr) { return 0; }
    try {
        return static_cast<bt::BehaviorTree*>(tree)->blackboard().get<bool>(key) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

const char* bt_last_error() {
    return tlLastError.c_str();
}

}  // extern "C"
// NOLINTEND(readability-identifier-naming,cppcoreguidelines-owning-memory)
