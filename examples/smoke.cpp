// End-to-end smoke: MockEngine → RuntimeRegistry → YAML → Simulator → MonitorServer
// Exercises the full pipeline without a real game engine.

#include <iostream>
#include <string>

#include "bt/MockEngine.h"
#include "bt/MonitorServer.h"
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"
#include "bt/SchemaSerializer.h"
#include "bt/Simulator.h"
#include "bt/Status.h"
#include "bt/TreeSerializer.h"

static const std::string kScenarioYaml = R"(
schema_version: "1.0"
behaviors:
  - name: attack
    when: enemy_near
    tree:
      type: action
      name: fire
  - name: patrol
    tree:
      type: action
      name: wander
)";

static std::string_view statusStr(bt::Status status) {
    switch (status) {
        case bt::Status::SUCCESS: return "SUCCESS";
        case bt::Status::FAILURE: return "FAILURE";
        case bt::Status::RUNNING: return "RUNNING";
    }
    return "?";
}

int main() {
    std::cout << "=== Arborist end-to-end smoke ===\n\n";

    // ── 1. Engine + Registry ──────────────────────────────────────────────────
    bt::RuntimeRegistry registry(":memory:");

    bt::MockEngine engine;
    engine.setState("enemy_near", false);
    engine.addCondition("enemy_near", "enemy_near");
    engine.addAction("fire",   bt::Status::SUCCESS);
    engine.addAction("wander", bt::Status::RUNNING);
    engine.applyTo(registry);

    registry.action("fire")
        .intent("Shoot the nearest enemy")
        .reads("enemy_near")
        .writes("enemy_near");

    registry.action("wander")
        .intent("Patrol the area when no threat is visible")
        .reads("enemy_near");

    registry.condition("enemy_near")
        .intent("True when an enemy is within detection range")
        .reads("enemy_near");

    std::cout << "[1] Registry contract (YAML):\n"
              << bt::SchemaSerializer::toYaml(registry.store()) << "\n";

    // ── 2. Load tree from YAML ────────────────────────────────────────────────
    auto tree = bt::SchemaLoader::load(kScenarioYaml, registry);

    std::cout << "[2] Tree structure (JSON):\n"
              << bt::TreeSerializer::toJson(tree.root()) << "\n\n";

    // ── 3. Simulate: patrol 3 ticks, enemy appears on tick 4, attack ─────────
    bt::Simulator sim(std::move(tree));
    sim.atTick(4, [&engine] {
        std::cout << "  [hook] tick 4: enemy spotted\n";
        engine.setState("enemy_near", true);
    });

    std::cout << "[3] Running 8 ticks:\n";
    auto result = sim.run(8);

    for (const auto& record : result.history) {
        std::cout << "  tick " << record.tickNumber
                  << "  behavior=" << (record.behaviorName.empty() ? "(none)" : record.behaviorName)
                  << "  status=" << statusStr(record.result) << "\n";
    }

    std::cout << "\n  Total ticks: " << result.ticksRun
              << "  Final: " << statusStr(result.finalStatus) << "\n\n";

    // ── 4. Verify expected behavior sequence ──────────────────────────────────
    std::cout << "[4] Assertions:\n";
    bool allPassed = true;

    auto check = [&allPassed](bool condition, const char* msg) {
        std::cout << "  " << (condition ? "PASS" : "FAIL") << "  " << msg << "\n";
        if (!condition) { allPassed = false; }
    };

    check(result.history[0].behaviorName == "patrol", "tick 1: patrol");
    check(result.history[1].behaviorName == "patrol", "tick 2: patrol");
    check(result.history[2].behaviorName == "patrol", "tick 3: patrol");
    check(result.history[3].behaviorName == "attack", "tick 4: attack after enemy appears");
    check(result.history[3].result == bt::Status::SUCCESS, "tick 4: attack succeeds");
    check(result.finalStatus == bt::Status::SUCCESS, "simulation ends on SUCCESS");
    check(result.ticksRun == 4, "stops after terminal tick");  // NOLINT(readability-magic-numbers)

    // ── 5. MonitorServer data layer ───────────────────────────────────────────
    std::cout << "\n[5] MonitorServer direct output:\n";

    // Use a fresh tree for the server (original was moved into simulator)
    auto serverTree = bt::SchemaLoader::load(kScenarioYaml, registry);
    bt::DecisionEmitter emitter;
    for (const auto& record : result.history) {
        // Replay history into a fresh emitter so the server can serve it
        bt::Blackboard board;
        emitter.record(record.tickNumber, record.behaviorName, record.result, board, record.activePath);
    }

    bt::MonitorServer server(serverTree, emitter);

    auto treeJson    = server.getTree();
    auto historyJson = server.getHistory();

    check(!treeJson.empty() && treeJson.front() == '{',
          "getTree() returns JSON object");
    check(!historyJson.empty() && historyJson.front() == '[',
          "getHistory() returns JSON array");
    check(historyJson.find("patrol") != std::string::npos,
          "getHistory() contains 'patrol'");
    check(historyJson.find("attack") != std::string::npos,
          "getHistory() contains 'attack'");

    // ── 6. MonitorServer HTTP ─────────────────────────────────────────────────
    std::cout << "\n[6] HTTP server:\n";
    server.start(19090);  // NOLINT(readability-magic-numbers)
    check(server.running(), "server running after start()");
    std::cout << "  Viewer available at http://localhost:19090\n";
    server.stop();
    check(!server.running(), "server stopped after stop()");

    std::cout << "\n=== " << (allPassed ? "ALL PASSED" : "SOME FAILED") << " ===\n";
    return allPassed ? 0 : 1;
}
