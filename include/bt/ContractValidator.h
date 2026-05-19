#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "bt/RegistryStore.h"
#include "bt/ScenarioStep.h"

namespace bt {

enum class ViolationType : std::uint8_t {
    READ_NOT_SATISFIED,  // declared read key was never present in the blackboard
    WRITE_NOT_OBSERVED,  // declared write key never changed while the behavior ran
    UNDECLARED_WRITE,    // blackboard key changed but is not in the declared writes
};

struct ContractViolation {
    ViolationType type;
    std::string behaviorName;
    std::string key;
    std::string message;
};

// Validates declared action contracts against observed runtime behavior.
//
// Reads: every key in an action's declared reads[] must appear in the
//        blackboard snapshot at least once while that behavior ran.
// Writes: every key in an action's declared writes[] must change in the
//         blackboard at least once while that behavior ran.
// Undeclared writes: any key that changed while a behavior ran but is
//                    not in that behavior's writes[] is flagged.
//
// Behavior→action name mapping defaults to identity (behavior name ==
// action name in the registry). Supply an explicit map when using
// YAML-loaded trees where the behavior name differs from the action name.
class ContractValidator {
public:
    explicit ContractValidator(
        const RegistryStore& store,
        std::unordered_map<std::string, std::string> behaviorToAction = {});

    [[nodiscard]] std::vector<ContractViolation> validate(const ScenarioResult& result) const;

private:
    const RegistryStore* store_;
    std::unordered_map<std::string, std::string> behaviorToAction_;

    [[nodiscard]] std::string resolveActionName(const std::string& behaviorName) const;
};

}  // namespace bt
