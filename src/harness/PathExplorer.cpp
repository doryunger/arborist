#include "bt/PathExplorer.h"

#include <algorithm>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "bt/DecisionEmitter.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaNode.h"
#include "bt/Status.h"

namespace bt {

namespace {

void collectFromNode(const SchemaNode& node,
                     std::set<std::string>& conditions,
                     std::set<std::string>& actions) {
    switch (node.type) {
        case SchemaNodeType::ACTION:
            if (!node.name.empty()) { actions.insert(node.name); }
            break;
        case SchemaNodeType::CONDITION:
            if (!node.name.empty()) { conditions.insert(node.name); }
            break;
        default:
            break;
    }
    for (const auto& child : node.children) {
        collectFromNode(*child, conditions, actions);
    }
}

struct MockSources {
    std::vector<std::string>                              conditionNames;
    std::vector<std::string>                              actionNames;
    std::shared_ptr<std::unordered_map<std::string, bool>> condTable;
};

MockSources buildMockSources(const SchemaDoc& schema) {
    std::set<std::string> condSet;
    std::set<std::string> actionSet;

    for (const auto& behavior : schema.behaviors) {
        if (!behavior.condition.empty()) {
            condSet.insert(behavior.condition);
        }
        if (behavior.tree) {
            collectFromNode(*behavior.tree, condSet, actionSet);
        }
    }

    MockSources src;
    src.conditionNames = {condSet.begin(), condSet.end()};
    src.actionNames    = {actionSet.begin(), actionSet.end()};
    src.condTable      = std::make_shared<std::unordered_map<std::string, bool>>();
    for (const auto& name : src.conditionNames) {
        src.condTable->emplace(name, false);
    }
    return src;
}

LoaderRegistry buildMockRegistry(const MockSources& src) {
    LoaderRegistry reg;
    for (const auto& name : src.conditionNames) {
        reg.conditions[name] = [table = src.condTable, name]() {
            auto found = table->find(name);
            return found != table->end() && found->second;
        };
    }
    for (const auto& name : src.actionNames) {
        reg.actions[name] = [] { return Status::SUCCESS; };
    }
    return reg;
}

}  // namespace

std::vector<PathExplorer::CoveragePath> PathExplorer::enumerate(const SchemaDoc& schema) {
    auto src = buildMockSources(schema);
    auto reg = buildMockRegistry(src);

    const std::size_t numConditions = src.conditionNames.size();

    // Cap exhaustive search at 2^20 to keep runtime bounded.
    static constexpr std::size_t kMaxCombinations = 1ULL << 20U;
    const std::size_t combinations =
        (numConditions <= 20U) ? (1ULL << numConditions) : kMaxCombinations;

    BehaviorTree tree = SchemaLoader::load(schema, reg);
    DecisionEmitter emitter;
    tree.setEmitter(&emitter);

    std::vector<CoveragePath> paths;
    std::set<std::string> seen;

    for (std::size_t combo = 0; combo < combinations; ++combo) {
        for (std::size_t bit = 0; bit < numConditions; ++bit) {
            (*src.condTable)[src.conditionNames[bit]] = ((combo >> bit) & 1U) != 0U;
        }
        tree.reset();
        emitter.clear();
        tree.tick();

        const auto& history = emitter.history();
        if (history.empty()) { continue; }
        const auto& record = history.back();
        if (record.behaviorName.empty()) { continue; }
        if (seen.contains(record.behaviorName)) { continue; }

        seen.insert(record.behaviorName);
        CoveragePath path;
        path.activatedBehavior = record.behaviorName;
        for (std::size_t bit = 0; bit < numConditions; ++bit) {
            path.conditions[src.conditionNames[bit]] = ((combo >> bit) & 1U) != 0U;
        }
        for (const auto& node : record.activePath) {
            path.activePath.push_back(node.name);
        }
        paths.push_back(std::move(path));
    }

    return paths;
}

PathExplorer::FuzzResult PathExplorer::fuzz(const SchemaDoc& schema,
                                              std::size_t ticks,
                                              std::uint64_t seed) {
    auto src = buildMockSources(schema);
    auto reg = buildMockRegistry(src);

    BehaviorTree tree = SchemaLoader::load(schema, reg);
    DecisionEmitter emitter;
    tree.setEmitter(&emitter);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> coin(0, 1);

    // Collect all expected behavior names for "neverActivated" reporting.
    std::set<std::string> allBehaviors;
    for (const auto& behavior : schema.behaviors) {
        allBehaviors.insert(behavior.name);
    }

    FuzzResult result;
    result.ticksRun = ticks;

    for (std::size_t tick = 0; tick < ticks; ++tick) {
        for (auto& [name, val] : *src.condTable) {
            val = (coin(rng) == 1);
        }
        tree.reset();
        emitter.clear();
        tree.tick();

        const auto& history = emitter.history();
        if (!history.empty()) {
            const auto& record = history.back();
            if (!record.behaviorName.empty()) {
                result.activatedBehaviors.insert(record.behaviorName);
            } else {
                ++result.ticksWithNoActivation;
            }
        }
    }

    for (const auto& name : allBehaviors) {
        if (!result.activatedBehaviors.contains(name)) {
            result.neverActivated.insert(name);
        }
    }

    return result;
}

}  // namespace bt
