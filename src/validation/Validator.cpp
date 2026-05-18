#include "bt/Validator.h"

#include <unordered_set>

namespace bt {

std::vector<Validator::Warning> Validator::validate(const std::vector<BehaviorEntry>& entries) {
    std::vector<Warning> warnings;

    if (entries.empty()) {
        warnings.push_back({Warning::Level::ERROR, "", "No behaviors registered"});
        return warnings;
    }

    std::unordered_set<std::string> seen;
    bool hasDefault = false;

    for (const auto& entry : entries) {
        if (!seen.insert(entry.name).second) {
            warnings.push_back(
                {Warning::Level::ERROR, entry.name, "Duplicate behavior name"});
        }

        if (!entry.onTick) {
            warnings.push_back(
                {Warning::Level::ERROR, entry.name, "Missing onTick callback"});
        }

        if (!entry.condition) {
            hasDefault = true;
        }
    }

    if (!hasDefault) {
        warnings.push_back({Warning::Level::WARNING, "",
                             "No default behavior (a behavior with no condition). "
                             "The tree may return FAILURE when no condition is met."});
    }

    return warnings;
}

}  // namespace bt
