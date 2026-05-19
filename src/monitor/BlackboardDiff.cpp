#include "bt/BlackboardDiff.h"

#include <typeinfo>

namespace bt {

namespace {

bool anyEqual(const std::any& lhs, const std::any& rhs) {
    if (lhs.type() != rhs.type()) { return false; }
    if (lhs.type() == typeid(int)) {
        return std::any_cast<int>(lhs) == std::any_cast<int>(rhs);
    }
    if (lhs.type() == typeid(bool)) {
        return std::any_cast<bool>(lhs) == std::any_cast<bool>(rhs);
    }
    if (lhs.type() == typeid(float)) {
        return std::any_cast<float>(lhs) == std::any_cast<float>(rhs);
    }
    if (lhs.type() == typeid(double)) {
        return std::any_cast<double>(lhs) == std::any_cast<double>(rhs);
    }
    if (lhs.type() == typeid(std::string)) {
        return std::any_cast<const std::string&>(lhs) == std::any_cast<const std::string&>(rhs);
    }
    // Unknown type with same type_info: assume equal to avoid false positives.
    return true;
}

}  // namespace

BlackboardDiff diffBlackboards(const std::unordered_map<std::string, std::any>& before,
                               const std::unordered_map<std::string, std::any>& after) {
    BlackboardDiff diff;

    for (const auto& [key, afterValue] : after) {
        auto found = before.find(key);
        if (found == before.end()) {
            diff.added.push_back(key);
        } else if (!anyEqual(found->second, afterValue)) {
            diff.changed.push_back(key);
        }
    }

    for (const auto& [key, _] : before) {
        if (!after.contains(key)) {
            diff.removed.push_back(key);
        }
    }

    return diff;
}

}  // namespace bt
