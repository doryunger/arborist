// Live demo — run the full framework pipeline end-to-end with a browser viewer.
//
// Usage: ./bt_demo [port]   (default port: 8080)
//
// What it demonstrates:
//   1. RuntimeRegistry — declare action/condition contracts (reads, writes, intent)
//   2. BehaviorBuilder + TreeAssembler — build a tree with a live Blackboard
//   3. DecisionEmitter + MonitorServer — serve the live tree viewer
//   4. Tick loop — run the tree continuously, inject state changes, track performance
//   5. ScenarioRunner + ContractValidator — quick self-test before the live loop starts
//
// Open http://localhost:8080 in a browser to watch the tree in real time.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include "bt/BehaviorBuilder.h"
#include "bt/Blackboard.h"
#include "bt/ContractValidator.h"
#include "bt/MockEngine.h"
#include "bt/DecisionEmitter.h"
#include "bt/MonitorServer.h"
#include "bt/RuntimeRegistry.h"
#include "bt/ScenarioRunner.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaSerializer.h"
#include "bt/Status.h"
#include "bt/TreeAssembler.h"
#include "bt/TreeSerializer.h"

// ── Signal handling ───────────────────────────────────────────────────────────

namespace {
std::atomic<bool> gStop{false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}

static void handleSignal(int /*sig*/) {
    gStop = true;
}

// ── World state ───────────────────────────────────────────────────────────────

struct WorldState {
    bool enemyNear{false};
    int  health{100};
    int  enemySpawnCycle{0};  // counts ticks since last spawn-cycle start
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string healthBar(int health) {
    constexpr int kBarWidth = 20;
    int filled = (health * kBarWidth) / 100;
    filled = std::max(0, std::min(kBarWidth, filled));
    std::string bar = "[";
    for (int idx = 0; idx < kBarWidth; ++idx) {
        bar += (idx < filled) ? '#' : '-';
    }
    bar += "] ";
    bar += std::to_string(health);
    return bar;
}

static std::string_view statusStr(bt::Status status) {
    switch (status) {
        case bt::Status::SUCCESS: return "SUCCESS";
        case bt::Status::FAILURE: return "FAILURE";
        case bt::Status::RUNNING: return "RUNNING";
    }
    return "?";
}

// ── Scenario YAML (used for self-test) ───────────────────────────────────────

static const std::string kScenarioYaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: attack
  - name: patrol
    tree:
      type: action
      name: patrol
)";

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    signal(SIGINT, handleSignal);  // NOLINT(cert-err33-c)

    const int port = (argc > 1) ? std::stoi(argv[1]) : 8080;  // NOLINT(readability-magic-numbers)

    std::cout << "\n=== Arborist Live Demo ===\n";
    std::cout << "  Open http://localhost:" << port << " in your browser.\n\n";

    // ── 1. Registry ───────────────────────────────────────────────────────────
    std::cout << "[1/5] Registering contracts\n";

    bt::RuntimeRegistry registry(":memory:");

    registry.action("attack")
        .intent("Engage the nearest enemy until it is defeated")
        .reads("enemy_near")
        .writes("enemy_near")
        .impl([] { return bt::Status::RUNNING; });

    registry.action("patrol")
        .intent("Roam the area and recover health when no threat is visible")
        .reads("health")
        .impl([] { return bt::Status::RUNNING; });

    registry.condition("enemy_near")
        .intent("True when an enemy is within detection range")
        .reads("enemy_near")
        .impl([] { return false; });

    std::cout << bt::SchemaSerializer::toYaml(registry.store()) << "\n";

    // ── 2. Self-test with ScenarioRunner + ContractValidator ──────────────────
    std::cout << "[2/5] Running self-test scenario\n";
    {
        bt::MockEngine engine;
        engine.setState("enemy_near", false);
        engine.addCondition("enemy_near", "enemy_near");
        engine.addAction("patrol", bt::Status::RUNNING);
        engine.addAction("attack", bt::Status::RUNNING);
        bt::RuntimeRegistry testReg(":memory:");
        engine.applyTo(testReg);

        bt::ScenarioRunner runner(bt::SchemaLoader::load(kScenarioYaml, testReg));
        runner.atTick(5, [&engine] { engine.setState("enemy_near", true); });
        runner.expect(1, "patrol");
        runner.expect(4, "patrol");
        runner.expect(5, "attack");
        runner.expect(8, "attack");

        auto result = runner.run(10);
        for (const auto& step : result.stepResults) {
            std::cout << "  " << (step.passed ? "PASS" : "FAIL")
                      << "  tick " << step.tick
                      << "  expected=" << step.expectedBehavior
                      << "  actual=" << step.actualBehavior << "\n";
        }
        if (!result.allPassed) {
            std::cout << "  Self-test FAILED — aborting.\n";
            return 1;
        }
        std::cout << "  All checks passed.\n\n";
    }

    // ── 3. Build live tree with a Blackboard wired to WorldState ─────────────
    std::cout << "[3/5] Building live tree\n";

    WorldState world;

    bt::Blackboard board;
    board.registerSource<bool>("enemy_near", [&world] { return world.enemyNear; });
    board.registerSource<int>("health",      [&world] { return world.health; });

    bt::BehaviorBuilder builder;
    builder
        .behavior("attack")
            .when([&world] { return world.enemyNear; })
            .onEnter([] { std::cout << "  [event] >> entered attack\n"; })
            .onTick([] { return bt::Status::RUNNING; })
            .onExit([] { std::cout << "  [event] << left attack\n"; })
        .behavior("patrol")
            .onTick([] { return bt::Status::RUNNING; });

    auto tree = bt::TreeAssembler::assemble(builder.entries(), std::move(board));
    std::cout << "  " << bt::TreeSerializer::toJson(tree.root()) << "\n\n";

    // ── 4. Monitor server ─────────────────────────────────────────────────────
    std::cout << "[4/5] Starting monitor server\n";

    bt::DecisionEmitter emitter;
    tree.setEmitter(&emitter);

    bt::MonitorServer server(tree, emitter);
    server.start(port);
    std::cout << "  Viewer: http://localhost:" << port << "\n\n";

    // ── 5. Tick loop ──────────────────────────────────────────────────────────
    std::cout << "[5/5] Running tick loop — press Ctrl+C to stop\n";
    std::cout << std::string(70, '-') << "\n";
    std::cout << std::left
              << std::setw(6)  << "tick"
              << std::setw(10) << "behavior"
              << std::setw(10) << "status"
              << std::setw(26) << "health"
              << "enemy\n";
    std::cout << std::string(70, '-') << "\n";

    constexpr int kTickMs        = 200;  // 5 ticks per second
    constexpr int kEnemyCycle    = 30;   // enemy appears every N ticks
    constexpr int kEnemyDuration = 12;   // enemy stays for N ticks
    constexpr int kDmgPerTick    = 4;    // health lost per tick while attacking
    constexpr int kRegenPerTick  = 2;    // health gained per tick while patrolling
    constexpr int kMaxHealth     = 100;

    std::size_t tick = 0;
    std::unordered_map<std::string, std::size_t> behaviorCounts;
    auto startTime = std::chrono::steady_clock::now();
    long long totalTickUs = 0;

    while (!gStop) {
        ++tick;
        ++world.enemySpawnCycle;

        // World engine: enemy spawns and despawns on a fixed cycle
        if (world.enemySpawnCycle == kEnemyCycle) {
            world.enemyNear = true;
            std::cout << "  *** ENEMY APPEARED (tick " << tick << ") ***\n";
        }
        if (world.enemySpawnCycle == kEnemyCycle + kEnemyDuration) {
            world.enemyNear = false;
            world.enemySpawnCycle = 0;
            std::cout << "  *** ENEMY DEFEATED (tick " << tick << ") ***\n";
        }

        auto tickStart = std::chrono::steady_clock::now();
        bt::Status status = tree.tick();
        auto tickEnd = std::chrono::steady_clock::now();
        totalTickUs += std::chrono::duration_cast<std::chrono::microseconds>(tickEnd - tickStart).count();

        const std::string& behavior = emitter.history().back().behaviorName;
        behaviorCounts[behavior]++;

        // World engine: apply effects based on active behavior
        if (behavior == "attack") {
            world.health = std::max(0, world.health - kDmgPerTick);
        } else {
            world.health = std::min(kMaxHealth, world.health + kRegenPerTick);
        }
        if (world.health <= 0) {
            world.health = kMaxHealth;
            std::cout << "  *** HEALTH DEPLETED — resetting (tick " << tick << ") ***\n";
        }

        std::cout << std::left
                  << std::setw(6)  << tick
                  << std::setw(10) << (behavior.empty() ? "(none)" : behavior)
                  << std::setw(10) << statusStr(status)
                  << std::setw(26) << healthBar(world.health)
                  << (world.enemyNear ? "YES" : "no") << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }

    server.stop();

    // ── Stats ─────────────────────────────────────────────────────────────────
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    double elapsedSec = std::chrono::duration<double>(elapsed).count();

    std::cout << "\n" << std::string(70, '-') << "\n";
    std::cout << "  Ticks run    : " << tick << "\n";
    std::cout << "  Elapsed      : " << std::fixed << std::setprecision(1)
              << elapsedSec << "s\n";
    std::cout << "  Avg tick time: " << (tick > 0 ? totalTickUs / static_cast<long long>(tick) : 0)
              << " us\n";
    std::cout << "  Tick rate    : " << std::fixed << std::setprecision(1)
              << (elapsedSec > 0.0 ? static_cast<double>(tick) / elapsedSec : 0.0)
              << " ticks/s\n";
    std::cout << "\n  Behavior distribution:\n";
    for (const auto& [name, count] : behaviorCounts) {
        double pct = tick > 0 ? (static_cast<double>(count) * 100.0) / static_cast<double>(tick) : 0.0;
        std::cout << "    " << std::setw(12) << name
                  << "  " << count << " ticks  ("
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << std::string(70, '-') << "\n";

    return 0;
}
