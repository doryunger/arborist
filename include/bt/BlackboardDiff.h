#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <vector>

namespace bt {

struct BlackboardDiff {
    std::vector<std::string> added;    // keys present in after but not before
    std::vector<std::string> removed;  // keys present in before but not after
    std::vector<std::string> changed;  // keys in both where the value differs
};

// Computes the diff between two blackboard snapshots.
// Value comparison is supported for: int, bool, float, double, std::string.
// For other types, if the type_info differs the key is reported as changed;
// if the type is the same but opaque, it is not reported as changed.
[[nodiscard]] BlackboardDiff diffBlackboards(
    const std::unordered_map<std::string, std::any>& before,
    const std::unordered_map<std::string, std::any>& after);

}  // namespace bt
