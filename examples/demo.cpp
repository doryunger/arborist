// Live demo — NPC guard scenario with a real 4-priority Behavior Tree.
//
// Usage: ./bt_demo [port]   (default port: 8080)
//
// What it demonstrates:
//   1. RuntimeRegistry — declare action/condition contracts
//   2. SchemaLoader   — build a deep, nested BT from YAML (full tree visible in viewer)
//   3. DecisionEmitter + MonitorServer — serve the live tree viewer
//   4. Tick loop       — run the tree continuously with a simulated world
//   5. ScenarioRunner  — quick self-test before the live loop starts
//
// NPC guard priorities (highest to lowest):
//   1. emergency_retreat  — when health < 25 %
//   2. combat             — when enemy_visible
//        tactics: try ranged (has_ammo? → shoot) else melee_strike
//   3. investigate        — when heard_noise
//   4. patrol             — always (wander)
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

#include "bt/Blackboard.h"
#include "bt/ContractValidator.h"
#include "bt/DecisionEmitter.h"
#include "bt/MockEngine.h"
#include "bt/MonitorServer.h"
#include "bt/RuntimeRegistry.h"
#include "bt/ScenarioRunner.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaSerializer.h"
#include "bt/Status.h"
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
    int  health{100};
    int  ammo{20};
    bool enemyVisible{false};
    bool heardNoise{false};
    // Internal sim counters
    int enemyCycle{0};
    int noiseCycle{0};
};

// ── NPC behavior YAML ─────────────────────────────────────────────────────────

static const std::string kNpcYaml = R"(
schema_version: "1.0"
behaviors:
  - name: emergency_retreat
    when: health_critical
    tree:
      type: action
      name: retreat

  - name: combat
    when: enemy_visible
    tree:
      type: selector
      name: combat_tactics
      children:
        - type: sequence
          name: ranged_attack
          children:
            - type: condition
              name: has_ammo
            - type: action
              name: shoot
        - type: action
          name: melee_strike

  - name: investigate
    when: heard_noise
    tree:
      type: action
      name: investigate_sound

  - name: patrol
    tree:
      type: action
      name: wander
)";

// ── Self-test scenario ────────────────────────────────────────────────────────

static const std::string kSelfTestYaml = R"(
schema_version: "1.0"
behaviors:
  - name: emergency_retreat
    when: health_critical
    tree:
      type: action
      name: retreat
  - name: combat
    when: enemy_visible
    tree:
      type: action
      name: shoot
  - name: patrol
    tree:
      type: action
      name: wander
)";

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string healthBar(int health) {
    constexpr int kBarWidth = 20;
    int filled = std::max(0, std::min(kBarWidth, (health * kBarWidth) / 100));
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

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    signal(SIGINT, handleSignal);  // NOLINT(cert-err33-c)

    const int port = (argc > 1) ? std::stoi(argv[1]) : 8080;  // NOLINT(readability-magic-numbers)

    std::cout << "\n=== Arborist NPC Guard Demo ===\n";
    std::cout << "  Open http://localhost:" << port << " in your browser.\n\n";

    // ── 1. Registry ───────────────────────────────────────────────────────────
    std::cout << "[1/5] Registering contracts\n";

    WorldState world;

    bt::RuntimeRegistry registry(":memory:");

    registry.condition("health_critical")
        .intent("True when health drops below 25 %")
        .reads("health")
        .impl([&world] { return world.health < 25; });  // NOLINT(readability-magic-numbers)

    registry.condition("enemy_visible")
        .intent("True when an enemy is in line of sight")
        .reads("enemy_visible")
        .impl([&world] { return world.enemyVisible; });

    registry.condition("has_ammo")
        .intent("True when the NPC has at least one round of ammo")
        .reads("ammo")
        .impl([&world] { return world.ammo > 0; });

    registry.condition("heard_noise")
        .intent("True when the NPC heard a suspicious sound nearby")
        .reads("heard_noise")
        .impl([&world] { return world.heardNoise; });

    registry.action("retreat")
        .intent("Fall back to cover and recover health")
        .reads("health")
        .writes("health")
        .impl([&world] {
            world.health = std::min(100, world.health + 5);  // NOLINT(readability-magic-numbers)
            return bt::Status::RUNNING;
        });

    registry.action("shoot")
        .intent("Fire a ranged shot at the visible enemy")
        .reads("ammo")
        .writes("ammo")
        .impl([&world] {
            world.ammo = std::max(0, world.ammo - 1);
            return bt::Status::RUNNING;
        });

    registry.action("melee_strike")
        .intent("Close in and strike the enemy with a melee attack")
        .impl([] { return bt::Status::RUNNING; });

    registry.action("investigate_sound")
        .intent("Move toward the noise source to identify the threat")
        .reads("heard_noise")
        .impl([] { return bt::Status::RUNNING; });

    registry.action("wander")
        .intent("Patrol the area along a preset route")
        .impl([&world] {
            world.health = std::min(100, world.health + 1);  // NOLINT(readability-magic-numbers)
            world.ammo   = std::min(20,  world.ammo   + 1);  // NOLINT(readability-magic-numbers)
            return bt::Status::RUNNING;
        });

    std::cout << bt::SchemaSerializer::toYaml(registry.store()) << "\n";

    // ── 2. Self-test ──────────────────────────────────────────────────────────
    std::cout << "[2/5] Running self-test scenario\n";
    {
        bt::MockEngine engine;
        engine.setState("health_critical", false);
        engine.setState("enemy_visible",   false);
        engine.addCondition("health_critical", "health_critical");
        engine.addCondition("enemy_visible",   "enemy_visible");
        engine.addAction("retreat", bt::Status::RUNNING);
        engine.addAction("shoot",   bt::Status::RUNNING);
        engine.addAction("wander",  bt::Status::RUNNING);
        bt::RuntimeRegistry testReg(":memory:");
        engine.applyTo(testReg);

        bt::ScenarioRunner runner(bt::SchemaLoader::load(kSelfTestYaml, testReg));
        runner.atTick(1, [&engine] { /* tick 1: patrol (no threats) */ });
        runner.atTick(3, [&engine] { engine.setState("enemy_visible", true); });
        runner.atTick(6, [&engine] {
            engine.setState("enemy_visible",   false);
            engine.setState("health_critical", true);
        });
        runner.expect(1, "patrol");
        runner.expect(2, "patrol");
        runner.expect(3, "combat");
        runner.expect(5, "combat");
        runner.expect(6, "emergency_retreat");
        runner.expect(8, "emergency_retreat");

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

    // ── 3. Build live tree from YAML ──────────────────────────────────────────
    std::cout << "[3/5] Building live tree from YAML\n";

    bt::Blackboard board;
    board.registerSource<int>("health",        [&world] { return world.health; });
    board.registerSource<int>("ammo",          [&world] { return world.ammo; });
    board.registerSource<bool>("enemy_visible",[&world] { return world.enemyVisible; });
    board.registerSource<bool>("heard_noise",  [&world] { return world.heardNoise; });
    board.registerSource<bool>("health_critical",
                               [&world] { return world.health < 25; });  // NOLINT

    auto tree = bt::SchemaLoader::load(kNpcYaml, registry, std::move(board));
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
    std::cout << std::string(72, '-') << "\n";
    std::cout << std::left
              << std::setw(6)  << "tick"
              << std::setw(20) << "behavior"
              << std::setw(10) << "status"
              << std::setw(26) << "health"
              << std::setw(6)  << "ammo"
              << "world\n";
    std::cout << std::string(72, '-') << "\n";

    constexpr int kTickMs          = 300;
    constexpr int kEnemyCycleTicks = 40;   // enemy appears every N ticks
    constexpr int kEnemyDuration   = 20;   // stays for N ticks
    constexpr int kNoiseCycleTicks = 25;   // noise event offset within cycle
    constexpr int kNoiseDuration   = 8;

    std::size_t tick = 0;
    std::unordered_map<std::string, std::size_t> behaviorCounts;
    auto startTime   = std::chrono::steady_clock::now();
    long long totalTickUs = 0;

    while (!gStop) {
        ++tick;
        ++world.enemyCycle;
        ++world.noiseCycle;

        // Enemy spawn/despawn
        if (world.enemyCycle == kEnemyCycleTicks) {
            world.enemyVisible = true;
            std::cout << "  *** ENEMY SPOTTED (tick " << tick << ") ***\n";
        }
        if (world.enemyCycle == kEnemyCycleTicks + kEnemyDuration) {
            world.enemyVisible = false;
            world.enemyCycle   = 0;
            std::cout << "  *** ENEMY GONE (tick " << tick << ") ***\n";
        }

        // Noise events (offset within same cycle)
        if (world.noiseCycle == kNoiseCycleTicks) {
            world.heardNoise = true;
            std::cout << "  *** NOISE HEARD (tick " << tick << ") ***\n";
        }
        if (world.noiseCycle == kNoiseCycleTicks + kNoiseDuration) {
            world.heardNoise   = false;
            world.noiseCycle   = 0;
            std::cout << "  *** NOISE CLEARED (tick " << tick << ") ***\n";
        }

        auto tickStart = std::chrono::steady_clock::now();
        bt::Status status = tree.tick();
        auto tickEnd   = std::chrono::steady_clock::now();
        totalTickUs += std::chrono::duration_cast<std::chrono::microseconds>(tickEnd - tickStart).count();

        const std::string behavior = emitter.history().empty()
                                         ? "(none)"
                                         : emitter.history().back().behaviorName;
        if (!behavior.empty()) {
            behaviorCounts[behavior]++;
        }

        // Health floor — reset on death
        if (world.health <= 0) {
            world.health = 100;  // NOLINT
            std::cout << "  *** NPC DIED — resetting (tick " << tick << ") ***\n";
        }

        // Render world flags
        std::string flags;
        if (world.health < 25) { flags += "CRITICAL "; }         // NOLINT
        if (world.enemyVisible) { flags += "ENEMY "; }
        if (world.heardNoise)   { flags += "NOISE "; }
        if (flags.empty())      { flags = "calm"; }

        std::cout << std::left
                  << std::setw(6)  << tick
                  << std::setw(20) << (behavior.empty() ? "(none)" : behavior)
                  << std::setw(10) << statusStr(status)
                  << std::setw(26) << healthBar(world.health)
                  << std::setw(6)  << world.ammo
                  << flags << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
    }

    server.stop();

    // ── Stats ─────────────────────────────────────────────────────────────────
    auto elapsed    = std::chrono::steady_clock::now() - startTime;
    double elapsedSec = std::chrono::duration<double>(elapsed).count();

    std::cout << "\n" << std::string(72, '-') << "\n";
    std::cout << "  Ticks run    : " << tick << "\n";
    std::cout << "  Elapsed      : " << std::fixed << std::setprecision(1)
              << elapsedSec << "s\n";
    std::cout << "  Avg tick time: "
              << (tick > 0 ? totalTickUs / static_cast<long long>(tick) : 0)
              << " us\n";
    std::cout << "  Tick rate    : " << std::fixed << std::setprecision(1)
              << (elapsedSec > 0.0 ? static_cast<double>(tick) / elapsedSec : 0.0)
              << " ticks/s\n";
    std::cout << "\n  Behavior distribution:\n";
    for (const auto& [name, count] : behaviorCounts) {
        double pct = tick > 0
                         ? (static_cast<double>(count) * 100.0) / static_cast<double>(tick)
                         : 0.0;
        std::cout << "    " << std::setw(20) << name
                  << "  " << count << " ticks  ("
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << std::string(72, '-') << "\n";

    return 0;
}
