#include "bt/ContractValidator.h"

#include <algorithm>
#include <unordered_set>

#include "bt/BlackboardDiff.h"

namespace bt {

namespace {

std::string violationMessage(ViolationType type,
                              const std::string& behaviorName,
                              const std::string& key,
                              const std::string& intent) {
    std::string msg;
    switch (type) {
        case ViolationType::READ_NOT_SATISFIED:
            msg = "'" + behaviorName + "' declares reads(\"" + key +
                  "\") but the key was never present in the blackboard";
            break;
        case ViolationType::WRITE_NOT_OBSERVED:
            msg = "'" + behaviorName + "' declares writes(\"" + key +
                  "\") but the key never changed while the behavior ran";
            break;
        case ViolationType::UNDECLARED_WRITE:
            msg = "'" + behaviorName + "' changed key \"" + key +
                  "\" but it is not listed in the declared writes";
            break;
    }
    if (!intent.empty()) {
        msg += " [intent: " + intent + "]";
    }
    return msg;
}

}  // namespace

ContractValidator::ContractValidator(const RegistryStore& store,
                                     std::unordered_map<std::string, std::string> behaviorToAction)
    : store_(&store), behaviorToAction_(std::move(behaviorToAction)) {}

std::string ContractValidator::resolveActionName(const std::string& behaviorName) const {
    auto found = behaviorToAction_.find(behaviorName);
    if (found != behaviorToAction_.end()) {
        return found->second;
    }
    return behaviorName;
}

namespace {

std::unordered_set<std::string> collectSeenKeys(
    const std::vector<const TickRecord*>& records) {
    std::unordered_set<std::string> seenKeys;
    for (const auto* record : records) {
        for (const auto& [key, ignored] : record->blackboardSnapshot) {
            seenKeys.insert(key);
        }
    }
    return seenKeys;
}

std::unordered_set<std::string> collectChangedKeys(
    const std::vector<const TickRecord*>& records) {
    const auto& firstSnapshot = records.front()->blackboardSnapshot;
    const auto& lastSnapshot  = records.back()->blackboardSnapshot;
    auto diff = diffBlackboards(firstSnapshot, lastSnapshot);
    std::unordered_set<std::string> changedKeys(diff.changed.begin(), diff.changed.end());
    changedKeys.insert(diff.added.begin(), diff.added.end());
    return changedKeys;
}

void checkReads(const std::string& behaviorName,
                const ActionSpec& spec,
                const std::unordered_set<std::string>& seenKeys,
                std::vector<ContractViolation>& out) {
    for (const auto& key : spec.reads) {
        if (!seenKeys.contains(key)) {
            out.push_back({ViolationType::READ_NOT_SATISFIED, behaviorName, key,
                           violationMessage(ViolationType::READ_NOT_SATISFIED,
                                            behaviorName, key, spec.intent)});
        }
    }
}

void checkWrites(const std::string& behaviorName,
                 const ActionSpec& spec,
                 const std::unordered_set<std::string>& changedKeys,
                 std::vector<ContractViolation>& out) {
    std::unordered_set<std::string> declaredWrites(spec.writes.begin(), spec.writes.end());

    for (const auto& key : spec.writes) {
        if (!changedKeys.contains(key)) {
            out.push_back({ViolationType::WRITE_NOT_OBSERVED, behaviorName, key,
                           violationMessage(ViolationType::WRITE_NOT_OBSERVED,
                                            behaviorName, key, spec.intent)});
        }
    }

    for (const auto& key : changedKeys) {
        if (!declaredWrites.contains(key)) {
            out.push_back({ViolationType::UNDECLARED_WRITE, behaviorName, key,
                           violationMessage(ViolationType::UNDECLARED_WRITE,
                                            behaviorName, key, spec.intent)});
        }
    }
}

}  // namespace

std::vector<ContractViolation> ContractValidator::validate(const ScenarioResult& result) const {
    std::vector<ContractViolation> violations;

    std::unordered_map<std::string, std::vector<const TickRecord*>> byBehavior;
    for (const auto& record : result.history) {
        if (!record.behaviorName.empty()) {
            byBehavior[record.behaviorName].push_back(&record);
        }
    }

    for (const auto& [behaviorName, records] : byBehavior) {
        auto spec = store_->findAction(resolveActionName(behaviorName));
        if (!spec) {
            continue;
        }
        checkReads(behaviorName, *spec, collectSeenKeys(records), violations);
        checkWrites(behaviorName, *spec, collectChangedKeys(records), violations);
    }

    return violations;
}

}  // namespace bt
