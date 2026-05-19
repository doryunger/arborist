#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "bt/BehaviorTree.h"

namespace bt {

// Walks the live Node tree and BehaviorMeta list after load, detecting
// structural logic issues and computing complexity metrics.
// Run at startup in debug builds or on demand via BehaviorTree::analyze().
class ComplexityAnalyzer {
public:
    struct Issue {
        enum class Code : std::uint8_t {
            EMPTY_COMPOSITE,
            SINGLE_CHILD_COMPOSITE,
            PARALLEL_THRESHOLD_UNREACHABLE,
            NO_FALLBACK_BEHAVIOR,
            PRIORITY_SHADOW,
            DEPTH_EXCEEDED,
            WIDTH_EXCEEDED,
            NODE_COUNT_EXCEEDED,
        };
        enum class Severity : std::uint8_t { WARNING, ERROR };

        Code        code;
        Severity    severity;
        std::string nodePath;
        std::string message;

        [[nodiscard]] bool isError() const noexcept { return severity == Severity::ERROR; }
    };

    struct Thresholds {
        std::size_t maxDepth{8};
        std::size_t maxWidth{6};
        std::size_t maxTotalNodes{100};
    };

    struct Report {
        std::size_t totalNodes{0};
        std::size_t maxDepth{0};
        std::size_t maxWidth{0};
        double      avgBranchingFactor{0.0};
        std::vector<Issue> issues;

        [[nodiscard]] bool hasErrors() const noexcept;
        [[nodiscard]] bool clean()     const noexcept;
        [[nodiscard]] std::string summary() const;
    };

    [[nodiscard]] static Report analyze(const BehaviorTree& tree, Thresholds thresholds);
    [[nodiscard]] static Report analyze(const BehaviorTree& tree);
};

}  // namespace bt
