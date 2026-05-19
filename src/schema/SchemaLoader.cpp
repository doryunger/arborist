#include "bt/SchemaLoader.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "bt/Blackboard.h"
#include "bt/RuntimeRegistry.h"

#include "bt/Action.h"
#include "bt/Condition.h"
#include "bt/LazySubtree.h"
#include "bt/Node.h"
#include "bt/Parallel.h"
#include "bt/PartitionConfig.h"
#include "bt/Policy.h"
#include "bt/SchemaNode.h"
#include "bt/SchemaParser.h"
#include "bt/Selector.h"
#include "bt/Sequence.h"
#include "bt/SubtreeScope.h"
#include "bt/TreeUtils.h"

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
    const std::string nodeName = schema.name.empty() ? "parallel" : schema.name;
    auto par = std::make_unique<Parallel>(nodeName, policy);
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
            const std::string nodeName = schema.name.empty() ? "sequence" : schema.name;
            auto seq = std::make_unique<Sequence>(nodeName);
            for (const auto& child : schema.children) {
                seq->addChild(buildNode(*child, reg));
            }
            return seq;
        }
        case SchemaNodeType::SELECTOR: {
            const std::string nodeName = schema.name.empty() ? "selector" : schema.name;
            auto sel = std::make_unique<Selector>(nodeName);
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

BehaviorTree buildTree(const SchemaDoc& doc, const LoaderRegistry& reg,
                       Blackboard blackboard = {},
                       const PartitionConfig& partition = {}) {
    auto root = std::make_unique<Selector>("root");
    std::vector<BehaviorMeta> metas;
    metas.reserve(doc.behaviors.size());

    for (const auto& behavior : doc.behaviors) {
        if (!behavior.tree) {
            throw SchemaLoadError("behavior '" + behavior.name + "' has no tree");
        }
        auto condition = resolveCondition(behavior, reg);
        auto seq = std::make_unique<Sequence>(behavior.name);
        if (condition) {
            seq->addChild(std::make_unique<Condition>(behavior.name + "_condition", condition));
        }
        const std::size_t subtreeNodeCount =
            (partition.lazyThreshold > 0 || partition.autoPartition)
                ? countSchemaNodes(*behavior.tree)
                : 0U;

        std::unique_ptr<Node> subtree;
        if (partition.lazyThreshold > 0 && subtreeNodeCount > partition.lazyThreshold) {
            // Defer instantiation: capture a shared deep clone + registry copy.
            // shared_ptr is required because std::function demands a copyable functor.
            auto cloned = std::shared_ptr<SchemaNode>(behavior.tree->deepClone().release());
            subtree = std::make_unique<LazySubtree>(
                behavior.name + "_lazy",
                [schema = std::move(cloned), regCopy = reg]() {
                    return buildNode(*schema, regCopy);
                });
        } else {
            subtree = buildNode(*behavior.tree, reg);
            if (partition.autoPartition && subtreeNodeCount > partition.maxNodesPerScope) {
                subtree = std::make_unique<SubtreeScope>(behavior.name + "_scope",
                                                         std::move(subtree));
            }
        }
        seq->addChild(std::move(subtree));
        root->addChild(std::move(seq));
        metas.push_back(BehaviorMeta{.name          = behavior.name,
                                      .condition     = condition,
                                      .interruptible = behavior.interruptible});
    }

    return BehaviorTree(std::move(root), std::move(blackboard), std::move(metas));
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

BehaviorTree SchemaLoader::load(std::string_view yaml, const RuntimeRegistry& reg,
                                 Blackboard blackboard) {
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
    auto doc = SchemaParser::parse(yaml);
    return buildTree(doc, loaderReg, std::move(blackboard));
}

BehaviorTree SchemaLoader::load(std::string_view yaml, const RuntimeRegistry& reg,
                                 Blackboard blackboard, const PartitionConfig& partition) {
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
    auto doc = SchemaParser::parse(yaml);
    return buildTree(doc, loaderReg, std::move(blackboard), partition);
}

BehaviorTree SchemaLoader::load(const SchemaDoc& doc, const LoaderRegistry& reg,
                                 Blackboard blackboard) {
    return buildTree(doc, reg, std::move(blackboard));
}

BehaviorTree SchemaLoader::load(const SchemaDoc& doc, const LoaderRegistry& reg,
                                 Blackboard blackboard, const PartitionConfig& partition) {
    return buildTree(doc, reg, std::move(blackboard), partition);
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
