// Large-scale throughput benchmark for bt-framework.
//
// Synthesizes N agents with procedurally generated behavior trees, ticks each
// one for M frames, and reports throughput and per-tick latency.  No engine
// required — all conditions and actions are stubs.
//
// Usage:
//   ./bt_benchmark [agents] [ticks_per_agent]
//   defaults: agents=200  ticks_per_agent=3600  (~20 minutes of 3Hz game AI)

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "bt/BehaviorTree.h"
#include "bt/Blackboard.h"
#include "bt/DecisionEmitter.h"
#include "bt/PartitionConfig.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaParser.h"
#include "bt/Status.h"

// ── Procedural tree generator ─────────────────────────────────────────────────

namespace {

// Returns peak resident set size in kilobytes, or 0 if unavailable.
std::size_t peakRssKb() {
    std::ifstream statm("/proc/self/status");
    if (!statm.is_open()) {
        return 0;
    }
    std::string line;
    while (std::getline(statm, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            std::size_t kb{0};
            iss >> kb;
            return kb;
        }
    }
    return 0;
}

struct AgentTemplate {
    std::string yaml;
    bt::LoaderRegistry reg;
};

// Build a procedural YAML schema for an agent.
// numBehaviors: how many priority behaviors (last one is unconditional fallback)
// conditionsPerBehavior: extra condition nodes in each behavior's subtree
AgentTemplate buildAgentTemplate(std::mt19937& rng,
                                  std::size_t numBehaviors,
                                  std::size_t nodesPerBehavior) {
    static const std::vector<std::string> kCondNames = {
        "enemy_visible", "health_low", "ammo_low", "ally_near",
        "in_cover", "target_reachable", "noise_heard", "path_clear"
    };
    static const std::vector<std::string> kActionNames = {
        "attack", "flee", "reload", "patrol_waypoint", "take_cover",
        "call_backup", "search_area", "wait_in_place"
    };

    std::uniform_int_distribution<std::size_t> condPick(0, kCondNames.size() - 1);
    std::uniform_int_distribution<std::size_t> actPick(0, kActionNames.size() - 1);
    std::uniform_int_distribution<int> coin(0, 1);

    std::ostringstream yaml;
    yaml << "schema_version: \"1.0\"\n";
    yaml << "behaviors:\n";

    AgentTemplate tmpl;

    for (std::size_t bIdx = 0; bIdx < numBehaviors; ++bIdx) {
        const bool isFallback = (bIdx == numBehaviors - 1);
        const std::string bName = "behavior_" + std::to_string(bIdx);

        yaml << "  - name: " << bName << "\n";
        if (!isFallback) {
            const std::string& cond = kCondNames[condPick(rng)];
            yaml << "    condition: " << cond << "\n";
            tmpl.reg.conditions[cond] = [] { return false; };
        }

        yaml << "    tree:\n";
        if (nodesPerBehavior <= 1) {
            const std::string& act = kActionNames[actPick(rng)];
            yaml << "      type: action\n";
            yaml << "      name: " << act << "\n";
            tmpl.reg.actions[act] = [] { return bt::Status::SUCCESS; };
        } else {
            yaml << "      type: sequence\n";
            yaml << "      name: " << bName << "_seq\n";
            yaml << "      children:\n";
            const std::size_t childCount = std::max<std::size_t>(2, nodesPerBehavior);
            for (std::size_t cIdx = 0; cIdx < childCount; ++cIdx) {
                if (coin(rng) != 0 && cIdx < childCount - 1) {
                    const std::string& cond = kCondNames[condPick(rng)];
                    yaml << "        - type: condition\n";
                    yaml << "          name: " << cond << "\n";
                    tmpl.reg.conditions[cond] = [] { return true; };
                } else {
                    const std::string& act = kActionNames[actPick(rng)];
                    yaml << "        - type: action\n";
                    yaml << "          name: " << act << "\n";
                    tmpl.reg.actions[act] = [] { return bt::Status::SUCCESS; };
                }
            }
        }
    }

    tmpl.yaml = yaml.str();
    return tmpl;
}

// ── Benchmark scenarios ────────────────────────────────────────────────────────

struct Result {
    std::string name;
    std::size_t totalTicks{0};
    double      durationMs{0.0};
    double      ticksPerSec{0.0};
    double      usPerTick{0.0};
    std::size_t rssKb{0};
};

Result runScenario(std::string_view name,
                   std::size_t numAgents,
                   std::size_t ticksPerAgent,
                   std::size_t numBehaviors,
                   std::size_t nodesPerBehavior,
                   bool useEmitter,
                   bool captureBlackboard,
                   std::size_t emitterCapacity,
                   const bt::PartitionConfig& partCfg) {
    std::mt19937 rng(42);

    // Build agent templates once — cost not included in timing.
    std::vector<bt::BehaviorTree> agents;
    agents.reserve(numAgents);

    std::vector<std::unique_ptr<bt::DecisionEmitter>> emitters;
    if (useEmitter) {
        emitters.reserve(numAgents);
    }

    for (std::size_t i = 0; i < numAgents; ++i) {
        auto tmpl = buildAgentTemplate(rng, numBehaviors, nodesPerBehavior);
        try {
            auto doc  = bt::SchemaParser::parse(tmpl.yaml);
            auto tree = bt::SchemaLoader::load(doc, tmpl.reg, {}, partCfg);
            if (useEmitter) {
                auto em = std::make_unique<bt::DecisionEmitter>(emitterCapacity);
                em->setCaptureBlackboard(captureBlackboard);
                tree.setEmitter(em.get());
                emitters.push_back(std::move(em));
            }
            agents.push_back(std::move(tree));
        } catch (...) {
            // Skip any agent that fails to load (schema generation edge cases).
        }
    }

    const std::size_t rssBeforeKb = peakRssKb();
    const auto t0 = std::chrono::high_resolution_clock::now();

    std::size_t totalTicks = 0;
    for (std::size_t tick = 0; tick < ticksPerAgent; ++tick) {
        for (auto& agent : agents) {
            std::ignore = agent.tick();
            ++totalTicks;
        }
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    const std::size_t rssAfterKb = peakRssKb();

    const double durationMs =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) /
        1000.0;

    Result res;
    res.name         = std::string(name);
    res.totalTicks   = totalTicks;
    res.durationMs   = durationMs;
    res.ticksPerSec  = (durationMs > 0.0)
                         ? (static_cast<double>(totalTicks) / (durationMs / 1000.0))
                         : 0.0;
    res.usPerTick    = (totalTicks > 0)
                         ? (durationMs * 1000.0 / static_cast<double>(totalTicks))
                         : 0.0;
    res.rssKb        = (rssAfterKb > rssBeforeKb) ? (rssAfterKb - rssBeforeKb) : 0;
    return res;
}

void printResult(const Result& res) {
    std::cout << std::left << std::setw(44) << res.name
              << std::right
              << std::setw(10) << res.totalTicks << " ticks"
              << std::setw(12) << std::fixed << std::setprecision(1)
              << res.ticksPerSec / 1000.0 << " K/s"
              << std::setw(10) << std::fixed << std::setprecision(2)
              << res.usPerTick << " us/tick"
              << std::setw(8)  << res.rssKb << " KB\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t numAgents = (argc > 1) ? std::stoul(argv[1]) : 200U;
    const std::size_t ticksPerAgent = (argc > 2) ? std::stoul(argv[2]) : 3600U;

    std::cout << "\n=== bt-framework large-scale benchmark ===\n";
    std::cout << "  agents: " << numAgents
              << "  ticks_per_agent: " << ticksPerAgent
              << "  total_ticks: " << numAgents * ticksPerAgent << "\n\n";

    std::cout << std::left  << std::setw(44) << "Scenario"
              << std::right << std::setw(16) << "Total ticks"
              << std::setw(12) << "Throughput"
              << std::setw(14) << "Latency"
              << std::setw(8)  << "ΔRSS\n";
    std::cout << std::string(94, '-') << "\n";

    bt::PartitionConfig noPart;
    noPart.autoPartition = false;
    noPart.lazyThreshold = 0;

    bt::PartitionConfig eagerPart;
    eagerPart.autoPartition = true;
    eagerPart.maxNodesPerScope = 4;
    eagerPart.lazyThreshold = 0;

    bt::PartitionConfig lazyPart;
    lazyPart.autoPartition = false;
    lazyPart.lazyThreshold = 4;

    std::vector<Result> results;

    // Baseline: small tree, no emitter.
    results.push_back(runScenario(
        "small-tree  no-emitter",
        numAgents, ticksPerAgent, 3, 2, false, false, 0, noPart));

    // Small tree, ring-buffer emitter (capacity=20), no snapshot.
    results.push_back(runScenario(
        "small-tree  emitter(cap=20) no-snapshot",
        numAgents, ticksPerAgent, 3, 2, true, false, 20, noPart));

    // Small tree, ring-buffer emitter + blackboard snapshot.
    results.push_back(runScenario(
        "small-tree  emitter(cap=20) +snapshot",
        numAgents, ticksPerAgent, 3, 2, true, true, 20, noPart));

    // Medium tree, no emitter.
    results.push_back(runScenario(
        "medium-tree no-emitter",
        numAgents, ticksPerAgent, 5, 6, false, false, 0, noPart));

    // Medium tree, emitter no snapshot.
    results.push_back(runScenario(
        "medium-tree emitter(cap=20) no-snapshot",
        numAgents, ticksPerAgent, 5, 6, true, false, 20, noPart));

    // Medium tree, eager auto-partition.
    results.push_back(runScenario(
        "medium-tree eager-partition no-emitter",
        numAgents, ticksPerAgent, 5, 6, false, false, 0, eagerPart));

    // Medium tree, lazy partition (materialized first tick).
    results.push_back(runScenario(
        "medium-tree lazy-partition  no-emitter",
        numAgents, ticksPerAgent, 5, 6, false, false, 0, lazyPart));

    // Large tree, no emitter.
    results.push_back(runScenario(
        "large-tree  no-emitter",
        numAgents, ticksPerAgent, 8, 10, false, false, 0, noPart));

    // Large tree, emitter unbounded + full snapshot (worst case).
    results.push_back(runScenario(
        "large-tree  emitter(unbounded) +snapshot",
        numAgents, ticksPerAgent, 8, 10, true, true, 0, noPart));

    // Large tree, ring buffer cap=20 + no snapshot (production config).
    results.push_back(runScenario(
        "large-tree  emitter(cap=20)  no-snapshot",
        numAgents, ticksPerAgent, 8, 10, true, false, 20, noPart));

    for (const auto& res : results) {
        printResult(res);
    }

    std::cout << "\n";

    // Summary: ratio of worst vs best.
    if (!results.empty()) {
        const auto [slowIt, fastIt] = std::minmax_element(
            results.begin(), results.end(),
            [](const Result& a, const Result& b) {
                return a.ticksPerSec < b.ticksPerSec;
            });
        std::cout << "  Fastest: " << fastIt->name
                  << "  (" << std::fixed << std::setprecision(1)
                  << fastIt->ticksPerSec / 1000.0 << " K ticks/s)\n";
        std::cout << "  Slowest: " << slowIt->name
                  << "  (" << std::fixed << std::setprecision(1)
                  << slowIt->ticksPerSec / 1000.0 << " K ticks/s)\n";
        if (slowIt->ticksPerSec > 0.0) {
            const double ratio = fastIt->ticksPerSec / slowIt->ticksPerSec;
            std::cout << "  Best is " << std::fixed << std::setprecision(1)
                      << ratio << "x faster than worst\n";
        }
    }
    std::cout << "\n";
    return EXIT_SUCCESS;
}
