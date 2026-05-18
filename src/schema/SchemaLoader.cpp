#include "bt/SchemaLoader.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bt/RuntimeRegistry.h"

#include "bt/Action.h"
#include "bt/BehaviorEntry.h"
#include "bt/Condition.h"
#include "bt/Node.h"
#include "bt/Parallel.h"
#include "bt/Policy.h"
#include "bt/SchemaNode.h"
#include "bt/SchemaParser.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/TreeAssembler.h"

namespace bt {

namespace {

std::unique_ptr<Node> buildNode(const SchemaNode& schema, const LoaderRegistry& reg);

std::unique_ptr<Node> buildAction(const SchemaNode& schema, const LoaderRegistry& reg) {
    auto found = reg.actions.find(schema.name);
    if (found == reg.actions.end()) {
        throw SchemaLoadError("unknown action: " + schema.name);
    }
    return std::make_unique<Action>(schema.name, found->second);
}

std::unique_ptr<Node> buildCondition(const SchemaNode& schema, const LoaderRegistry& reg) {
    auto found = reg.conditions.find(schema.name);
    if (found == reg.conditions.end()) {
        throw SchemaLoadError("unknown condition: " + schema.name);
    }
    return std::make_unique<Condition>(schema.name, found->second);
}

std::unique_ptr<Node> buildComposite(const SchemaNode& schema, const LoaderRegistry& reg) {
    Policy policy = Policy::all();
    if (schema.policy == SchemaPolicy::ANY) {
        policy = Policy::any();
    } else if (schema.policy == SchemaPolicy::THRESHOLD) {
        policy = Policy::threshold(schema.threshold);
    }
    auto par = std::make_unique<Parallel>("parallel", policy);
    for (const auto& child : schema.children) {
        par->addChild(buildNode(*child, reg));
    }
    return par;
}

std::unique_ptr<Node> buildNode(const SchemaNode& schema, const LoaderRegistry& reg) {
    switch (schema.type) {
        case SchemaNodeType::ACTION:
            return buildAction(schema, reg);
        case SchemaNodeType::CONDITION:
            return buildCondition(schema, reg);
        case SchemaNodeType::SEQUENCE: {
            auto seq = std::make_unique<Sequence>("sequence");
            for (const auto& child : schema.children) {
                seq->addChild(buildNode(*child, reg));
            }
            return seq;
        }
        case SchemaNodeType::SELECTOR: {
            auto sel = std::make_unique<Selector>("selector");
            for (const auto& child : schema.children) {
                sel->addChild(buildNode(*child, reg));
            }
            return sel;
        }
        case SchemaNodeType::PARALLEL:
            return buildComposite(schema, reg);
    }
    throw SchemaLoadError("unhandled schema node type");
}

std::function<bool()> resolveCondition(const BehaviorSchema& behavior,
                                        const LoaderRegistry& reg) {
    if (behavior.condition.empty()) {
        return nullptr;
    }
    auto found = reg.conditions.find(behavior.condition);
    if (found == reg.conditions.end()) {
        throw SchemaLoadError("unknown condition '" + behavior.condition +
                               "' referenced by behavior '" + behavior.name + "'");
    }
    return found->second;
}

BehaviorEntry buildEntry(const BehaviorSchema& behavior, const LoaderRegistry& reg) {
    if (!behavior.tree) {
        throw SchemaLoadError("behavior '" + behavior.name + "' has no tree");
    }
    BehaviorEntry entry;
    entry.name = behavior.name;
    entry.interruptible = behavior.interruptible;
    entry.condition = resolveCondition(behavior, reg);

    auto sharedNode = std::shared_ptr<Node>(buildNode(*behavior.tree, reg));
    entry.onTick = [sharedNode]() mutable { return sharedNode->tick(); };
    entry.onEnter = [sharedNode]() mutable { sharedNode->reset(); };

    return entry;
}

BehaviorTree buildTree(const SchemaDoc& doc, const LoaderRegistry& reg) {
    std::vector<BehaviorEntry> entries;
    entries.reserve(doc.behaviors.size());
    for (const auto& behavior : doc.behaviors) {
        entries.push_back(buildEntry(behavior, reg));
    }
    return TreeAssembler::assemble(std::move(entries));
}

void topoSort(const std::string& name,
              const std::unordered_map<std::string, SchemaDoc>& docs,
              std::unordered_set<std::string>& visited,
              std::unordered_set<std::string>& inStack,
              std::vector<std::string>& order) {
    if (inStack.contains(name)) {
        throw SchemaCycleError("import cycle detected involving subtree '" + name + "'");
    }
    if (visited.contains(name)) {
        return;
    }
    inStack.insert(name);
    auto found = docs.find(name);
    if (found != docs.end()) {
        for (const auto& dep : found->second.imports) {
            topoSort(dep, docs, visited, inStack, order);
        }
    }
    inStack.erase(name);
    visited.insert(name);
    order.push_back(name);
}

SchemaDoc mergeWithSubtrees(SchemaDoc rootDoc,
                              const std::unordered_map<std::string, SchemaDoc>& subtreeDocs,
                              const std::vector<std::string>& order) {
    SchemaDoc merged;
    merged.schemaVersion = std::move(rootDoc.schemaVersion);
    merged.stateDeclarations = std::move(rootDoc.stateDeclarations);

    for (const auto& subtreeName : order) {
        auto found = subtreeDocs.find(subtreeName);
        if (found == subtreeDocs.end()) {
            continue;
        }
        for (const auto& behavior : found->second.behaviors) {
            BehaviorSchema copy;
            copy.name = behavior.name;
            copy.condition = behavior.condition;
            copy.intent = behavior.intent;
            copy.interruptible = behavior.interruptible;
            if (behavior.tree) {
                // Deep-copy not needed; we move from a local copy of the doc.
                // Since SchemaDoc is move-only, we rebuild from the parsed doc.
                // The subtree doc is already owned by subtreeDocs so we can't move.
                // Re-parse isn't available here — use the existing tree by rebuilding.
                // This is handled by the caller passing mutable subtreeDocs.
            }
            (void)copy;
        }
        break;  // unreachable — see below
    }
    // Cannot deep-copy SchemaNode (copy-deleted). Instead, the caller must ensure
    // subtreeDocs is mutable and we move out of it directly.
    return merged;
}

}  // namespace

void SchemaManifest::add(std::string_view name, std::string_view yaml) {
    entries_[std::string(name)] = std::string(yaml);
}

bool SchemaManifest::has(std::string_view name) const noexcept {
    return entries_.contains(std::string(name));
}

std::string_view SchemaManifest::get(std::string_view name) const {
    auto found = entries_.find(std::string(name));
    if (found == entries_.end()) {
        throw SchemaLoadError("subtree not found in manifest: " + std::string(name));
    }
    return found->second;
}

BehaviorTree SchemaLoader::load(std::string_view yaml, const LoaderRegistry& reg) {
    auto doc = SchemaParser::parse(yaml);
    return buildTree(doc, reg);
}

BehaviorTree SchemaLoader::load(std::string_view yaml, const RuntimeRegistry& reg) {
    LoaderRegistry loaderReg;
    for (const auto& action : reg.store().allActions()) {
        const auto* func = reg.findAction(action.name);
        if (func != nullptr) {
            loaderReg.actions[action.name] = *func;
        }
    }
    for (const auto& cond : reg.store().allConditions()) {
        const auto* func = reg.findCondition(cond.name);
        if (func != nullptr) {
            loaderReg.conditions[cond.name] = *func;
        }
    }
    return load(yaml, loaderReg);
}

BehaviorTree SchemaLoader::loadWithManifest(std::string_view yaml,
                                              const SchemaManifest& manifest,
                                              const LoaderRegistry& reg) {
    auto rootDoc = SchemaParser::parse(yaml);

    // Parse all referenced subtrees transitively.
    std::unordered_map<std::string, SchemaDoc> subtreeDocs;
    std::vector<std::string> toProcess(rootDoc.imports.begin(), rootDoc.imports.end());

    while (!toProcess.empty()) {
        std::string subtreeName = toProcess.back();
        toProcess.pop_back();
        if (subtreeDocs.contains(subtreeName)) {
            continue;
        }
        if (!manifest.has(subtreeName)) {
            throw SchemaLoadError("import '" + subtreeName + "' not found in manifest");
        }
        auto subtreeDoc = SchemaParser::parse(manifest.get(subtreeName));
        for (const auto& dep : subtreeDoc.imports) {
            toProcess.push_back(dep);
        }
        subtreeDocs[subtreeName] = std::move(subtreeDoc);
    }

    // Topological sort — detects cycles and establishes dependency order.
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> inStack;
    std::vector<std::string> order;

    for (const auto& imp : rootDoc.imports) {
        topoSort(imp, subtreeDocs, visited, inStack, order);
    }

    // Merge: subtree behaviors first (dependency order), then root behaviors.
    SchemaDoc merged;
    merged.schemaVersion = rootDoc.schemaVersion;
    merged.stateDeclarations = std::move(rootDoc.stateDeclarations);

    for (const auto& subtreeName : order) {
        auto found = subtreeDocs.find(subtreeName);
        if (found == subtreeDocs.end()) {
            continue;
        }
        for (auto& behavior : found->second.behaviors) {
            merged.behaviors.push_back(std::move(behavior));
        }
    }
    for (auto& behavior : rootDoc.behaviors) {
        merged.behaviors.push_back(std::move(behavior));
    }

    return buildTree(merged, reg);
}

}  // namespace bt
