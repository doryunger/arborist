#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "bt/SchemaNode.h"

namespace bt {

// DB-driven automated test generator for behavior trees.
// Builds a fully mocked tree from a SchemaDoc (all actions return SUCCESS,
// all conditions are table-driven) then either enumerates every reachable
// behavior exhaustively or fuzzes with random condition assignments.
// No engine or RuntimeRegistry needed — works entirely from the schema.
class PathExplorer {
public:
    // One entry per distinct reachable behavior.
    // conditions shows a minimal assignment that activates it.
    struct CoveragePath {
        std::unordered_map<std::string, bool> conditions;
        std::string                            activatedBehavior;
        std::vector<std::string>               activePath;
    };

    struct FuzzResult {
        std::size_t           ticksRun{0};
        std::set<std::string> activatedBehaviors;
        std::set<std::string> neverActivated;
        std::size_t           ticksWithNoActivation{0};
    };

    // Enumerate one CoveragePath per reachable behavior via exhaustive
    // condition search. Safe for schemas with ≤ 20 distinct conditions.
    // For larger schemas use fuzz() instead.
    [[nodiscard]] static std::vector<CoveragePath> enumerate(const SchemaDoc& schema);

    // Randomly sample condition space for `ticks` iterations and collect
    // which behaviors were activated and which were never reached.
    [[nodiscard]] static FuzzResult fuzz(const SchemaDoc& schema,
                                          std::size_t ticks,
                                          std::uint64_t seed = 0);
};

}  // namespace bt
