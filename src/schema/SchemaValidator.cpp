#include "bt/SchemaValidator.h"

#include <unordered_set>

namespace bt {

namespace {

void collectRefs(const SchemaNode& node, std::vector<std::string>& actions,
                  std::vector<std::string>& conditions) {
    if (node.type == SchemaNodeType::ACTION) {
        actions.push_back(node.name);
    } else if (node.type == SchemaNodeType::CONDITION) {
        conditions.push_back(node.name);
    }
    for (const auto& child : node.children) {
        collectRefs(*child, actions, conditions);
    }
}

void checkDuplicates(const SchemaDoc& doc, std::vector<SchemaIssue>& issues) {
    std::unordered_set<std::string> seen;
    for (const auto& behavior : doc.behaviors) {
        if (seen.contains(behavior.name)) {
            issues.emplace_back(SchemaIssue::Severity::ERROR,
                                 "duplicate behavior name: " + behavior.name);
        }
        seen.insert(behavior.name);
    }
}

void checkMissingTrees(const SchemaDoc& doc, std::vector<SchemaIssue>& issues) {
    for (const auto& behavior : doc.behaviors) {
        if (!behavior.tree) {
            issues.emplace_back(SchemaIssue::Severity::ERROR,
                                 "behavior '" + behavior.name + "' has no tree defined");
        }
    }
}

void checkDefaultBehavior(const SchemaDoc& doc, std::vector<SchemaIssue>& issues) {
    for (const auto& behavior : doc.behaviors) {
        if (behavior.condition.empty()) {
            return;
        }
    }
    issues.emplace_back(SchemaIssue::Severity::WARNING,
                         "no default behavior (a behavior without a 'when' condition)");
}

void checkBehaviorRefs(const BehaviorSchema& behavior, const SchemaRegistry& registry,
                        std::vector<SchemaIssue>& issues) {
    if (!behavior.condition.empty() && !registry.conditions.contains(behavior.condition)) {
        issues.emplace_back(SchemaIssue::Severity::ERROR,
                             "unknown condition '" + behavior.condition + "' in behavior '" +
                                 behavior.name + "'");
    }
    if (!behavior.tree) {
        return;
    }
    std::vector<std::string> actionRefs;
    std::vector<std::string> conditionRefs;
    collectRefs(*behavior.tree, actionRefs, conditionRefs);

    for (const auto& action : actionRefs) {
        if (!registry.actions.contains(action)) {
            issues.emplace_back(SchemaIssue::Severity::ERROR,
                                 "unknown action '" + action + "' in behavior '" +
                                     behavior.name + "'");
        }
    }
    for (const auto& cond : conditionRefs) {
        if (!registry.conditions.contains(cond)) {
            issues.emplace_back(SchemaIssue::Severity::ERROR,
                                 "unknown condition '" + cond + "' in behavior '" +
                                     behavior.name + "'");
        }
    }
}

void checkRegistryRefs(const SchemaDoc& doc, const SchemaRegistry& registry,
                        std::vector<SchemaIssue>& issues) {
    for (const auto& behavior : doc.behaviors) {
        checkBehaviorRefs(behavior, registry, issues);
    }
}

}  // namespace

std::vector<SchemaIssue> SchemaValidator::validate(const SchemaDoc& doc) {
    std::vector<SchemaIssue> issues;
    if (doc.behaviors.empty()) {
        issues.emplace_back(SchemaIssue::Severity::ERROR, "schema has no behaviors defined");
        return issues;
    }
    checkDuplicates(doc, issues);
    checkMissingTrees(doc, issues);
    checkDefaultBehavior(doc, issues);
    return issues;
}

std::vector<SchemaIssue> SchemaValidator::validate(const SchemaDoc& doc,
                                                    const SchemaRegistry& registry) {
    auto issues = validate(doc);
    checkRegistryRefs(doc, registry, issues);
    return issues;
}

}  // namespace bt
