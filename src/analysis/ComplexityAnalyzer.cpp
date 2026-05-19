#include "bt/ComplexityAnalyzer.h"

#include <cstddef>
#include <string>

#include "bt/CompositeNode.h"
#include "bt/Parallel.h"

namespace bt {

namespace {

struct WalkState {
    std::size_t totalNodes{0};
    std::size_t maxDepth{0};
    std::size_t maxWidth{0};
    std::size_t compositesVisited{0};
    std::size_t totalChildrenOfComposites{0};
    std::vector<ComplexityAnalyzer::Issue>& issues;
    const ComplexityAnalyzer::Thresholds& thresholds;
};

void walkNode(const Node& node, std::size_t depth, const std::string& path,
              WalkState& state) {
    ++state.totalNodes;
    if (depth > state.maxDepth) { state.maxDepth = depth; }

    auto kids     = node.children();
    auto kidCount = kids.size();

    if (kidCount > state.maxWidth) { state.maxWidth = kidCount; }

    const bool isComposite = (dynamic_cast<const CompositeNode*>(&node) != nullptr);

    if (isComposite) {
        ++state.compositesVisited;
        state.totalChildrenOfComposites += kidCount;

        if (kidCount == 0) {
            state.issues.push_back({
                ComplexityAnalyzer::Issue::Code::EMPTY_COMPOSITE,
                ComplexityAnalyzer::Issue::Severity::ERROR,
                path,
                "Composite node '" + std::string(node.name()) + "' has no children"
            });
        } else if (kidCount == 1) {
            state.issues.push_back({
                ComplexityAnalyzer::Issue::Code::SINGLE_CHILD_COMPOSITE,
                ComplexityAnalyzer::Issue::Severity::WARNING,
                path,
                "Composite node '" + std::string(node.name()) + "' has only one child"
            });
        }

        const auto* par = dynamic_cast<const Parallel*>(&node);
        if (par != nullptr) {
            const auto& policy = par->policy();
            if (policy.threshold() > kidCount) {
                state.issues.push_back({
                    ComplexityAnalyzer::Issue::Code::PARALLEL_THRESHOLD_UNREACHABLE,
                    ComplexityAnalyzer::Issue::Severity::ERROR,
                    path,
                    "Parallel '" + std::string(node.name()) + "' threshold " +
                    std::to_string(policy.threshold()) + " exceeds child count " +
                    std::to_string(kidCount)
                });
            }
        }
    }

    for (const auto& child : kids) {
        walkNode(*child, depth + 1,
                 path + "/" + std::string(child->name()), state);
    }
}

}  // namespace

bool ComplexityAnalyzer::Report::hasErrors() const noexcept {
    for (const auto& issue : issues) {
        if (issue.isError()) { return true; }
    }
    return false;
}

bool ComplexityAnalyzer::Report::clean() const noexcept {
    return issues.empty();
}

std::string ComplexityAnalyzer::Report::summary() const {
    std::string out = "Tree analysis: ";
    out += std::to_string(totalNodes) + " nodes, max depth ";
    out += std::to_string(maxDepth)   + ", max width ";
    out += std::to_string(maxWidth);
    if (issues.empty()) {
        out += " — clean";
    } else {
        std::size_t errors   = 0;
        std::size_t warnings = 0;
        for (const auto& issue : issues) {
            if (issue.isError()) { ++errors; } else { ++warnings; }
        }
        out += " — ";
        out += std::to_string(errors)   + " error(s), ";
        out += std::to_string(warnings) + " warning(s)";
    }
    return out;
}

ComplexityAnalyzer::Report ComplexityAnalyzer::analyze(const BehaviorTree& tree) {
    return analyze(tree, Thresholds{});
}

ComplexityAnalyzer::Report ComplexityAnalyzer::analyze(const BehaviorTree& tree,
                                                         Thresholds thresholds) {
    Report report;
    WalkState state{
        .issues     = report.issues,
        .thresholds = thresholds,
    };

    walkNode(tree.root(), 0, std::string(tree.root().name()), state);

    report.totalNodes = state.totalNodes;
    report.maxDepth   = state.maxDepth;
    report.maxWidth   = state.maxWidth;
    if (state.compositesVisited > 0) {
        report.avgBranchingFactor =
            static_cast<double>(state.totalChildrenOfComposites) /
            static_cast<double>(state.compositesVisited);
    }

    // Aggregate threshold checks (emitted once, not per-node)
    if (report.maxDepth > thresholds.maxDepth) {
        report.issues.push_back({
            Issue::Code::DEPTH_EXCEEDED,
            Issue::Severity::WARNING,
            std::string(tree.root().name()),
            "Max depth " + std::to_string(report.maxDepth) +
            " exceeds threshold " + std::to_string(thresholds.maxDepth)
        });
    }
    if (report.maxWidth > thresholds.maxWidth) {
        report.issues.push_back({
            Issue::Code::WIDTH_EXCEEDED,
            Issue::Severity::WARNING,
            std::string(tree.root().name()),
            "Max width " + std::to_string(report.maxWidth) +
            " exceeds threshold " + std::to_string(thresholds.maxWidth)
        });
    }
    if (report.totalNodes > thresholds.maxTotalNodes) {
        report.issues.push_back({
            Issue::Code::NODE_COUNT_EXCEEDED,
            Issue::Severity::WARNING,
            std::string(tree.root().name()),
            "Total node count " + std::to_string(report.totalNodes) +
            " exceeds threshold " + std::to_string(thresholds.maxTotalNodes)
        });
    }

    // Priority shadowing: a behavior with no condition (always valid) shadows
    // every behavior that follows it in the priority list.
    const auto& metas = tree.behaviors();
    for (std::size_t idx = 0; idx < metas.size(); ++idx) {
        if (!metas[idx].condition && idx + 1 < metas.size()) {
            report.issues.push_back({
                Issue::Code::PRIORITY_SHADOW,
                Issue::Severity::ERROR,
                "root",
                "Behavior '" + metas[idx].name + "' has no condition and shadows " +
                std::to_string(metas.size() - idx - 1) + " behavior(s) that follow it"
            });
            break;  // one report is enough — remaining are all shadowed by the same cause
        }
    }

    // No fallback: all behaviors have conditions → tree may return FAILURE
    // when none are true.
    if (!metas.empty()) {
        bool hasFallback = false;
        for (const auto& meta : metas) {
            if (!meta.condition) { hasFallback = true; break; }
        }
        if (!hasFallback) {
            report.issues.push_back({
                Issue::Code::NO_FALLBACK_BEHAVIOR,
                Issue::Severity::WARNING,
                "root",
                "No unconditional behavior: tree returns FAILURE when no condition is true"
            });
        }
    }

    return report;
}

}  // namespace bt
