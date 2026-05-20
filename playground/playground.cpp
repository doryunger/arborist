// City Security Guard NPC — Arborist editor playground
//
// Starts the visual editor pre-loaded with a rich 8-behavior scenario so you
// can explore the tree structure, edit the schema, and run the complexity
// analyzer interactively.
//
// Usage:  ./bt_playground [port]   (default: 8081)
//
// Files created in the current directory:
//   bt_playground_schema.yaml  — editable schema (persists changes)
//   bt_playground_registry.db  — SQLite registry (persists contracts)
//
// Scenario: City Security Guard NPC
//   8 behaviors (priority order):
//     1. surrender          — when: is_overwhelmed (no ammo + overwhelmed)
//     2. seek_medical_aid   — when: is_critically_injured (health < 25)
//     3. combat_engage      — when: has_target_in_range (close enemy + ammo)
//     4. pursue_hostile     — when: hostile_detected (threat_count > 0)
//     5. respond_to_alert   — when: alert_active (alert_level > 50)
//     6. escort_vip         — when: vip_assigned (vip_id > 0)
//     7. patrol_zone        — (unconditional fallback)
//     8. debug_never_reached— (intentionally unreachable → PRIORITY_SHADOW)
//   33 actions, 11 conditions, 10 blackboard keys

#include <csignal>
#include <iostream>
#include <string>

#include "bt/EditorServer.h"
#include "bt/RuntimeRegistry.h"

// ── Schema YAML ───────────────────────────────────────────────────────────────

static const std::string kSchemaYaml = R"(schema_version: "1.0"
behaviors:

  # ── 1. Surrender ─────────────────────────────────────────────────────────────
  # Triggered when the NPC runs out of ammo and is overwhelmed by threats.
  # A full sequence: every step must succeed to complete the surrender.
  - name: surrender
    when: is_overwhelmed
    tree:
      type: sequence
      children:
        - type: action
          name: holster_weapon
        - type: action
          name: drop_weapon
        - type: action
          name: raise_hands
        - type: action
          name: kneel_down
        - type: action
          name: signal_surrender

  # ── 2. Seek Medical Aid ───────────────────────────────────────────────────────
  # Selector tries self-treatment first (medkit), then bandage, then calls medic.
  - name: seek_medical_aid
    when: is_critically_injured
    tree:
      type: selector
      children:
        - type: sequence
          children:
            - type: condition
              name: medkit_available
            - type: action
              name: find_nearest_medkit
            - type: action
              name: move_to_medkit
            - type: action
              name: use_medkit
        - type: sequence
          children:
            - type: action
              name: bandage_wound
        - type: action
          name: call_for_medic

  # ── 3. Combat Engage ──────────────────────────────────────────────────────────
  # Tries: shoot from cover → reload and shoot → grenade + backup.
  - name: combat_engage
    when: has_target_in_range
    tree:
      type: selector
      children:
        - type: sequence
          children:
            - type: condition
              name: has_ammo
            - type: action
              name: take_cover
            - type: action
              name: aim_at_target
            - type: action
              name: fire_weapon
        - type: sequence
          children:
            - type: condition
              name: has_cover_nearby
            - type: action
              name: reload_weapon
            - type: action
              name: aim_at_target
            - type: action
              name: fire_weapon
        - type: sequence
          children:
            - type: action
              name: throw_grenade
            - type: action
              name: call_backup

  # ── 4. Pursue Hostile ─────────────────────────────────────────────────────────
  # Scan, then (move-to-last-known OR radio-sighting) in parallel, then backup.
  - name: pursue_hostile
    when: hostile_detected
    tree:
      type: sequence
      children:
        - type: action
          name: scan_area
        - type: parallel
          policy: any
          children:
            - type: sequence
              children:
                - type: action
                  name: move_to_last_known
                - type: action
                  name: check_blind_spots
            - type: action
              name: radio_sighting
        - type: action
          name: call_backup

  # ── 5. Respond to Alert ───────────────────────────────────────────────────────
  # Acknowledge, then move+report simultaneously, then clear area, then report.
  - name: respond_to_alert
    when: alert_active
    tree:
      type: sequence
      children:
        - type: action
          name: acknowledge_alert
        - type: parallel
          policy: all
          children:
            - type: action
              name: move_to_alert_zone
            - type: action
              name: report_status
        - type: action
          name: clear_area
        - type: action
          name: report_status

  # ── 6. Escort VIP ─────────────────────────────────────────────────────────────
  # Locate VIP, then run protective + movement branches in parallel until either
  # completes, then file a status report.
  - name: escort_vip
    when: vip_assigned
    tree:
      type: sequence
      children:
        - type: action
          name: locate_vip
        - type: parallel
          policy: any
          children:
            - type: sequence
              children:
                - type: condition
                  name: vip_in_danger
                - type: action
                  name: shield_vip
                - type: action
                  name: call_backup
            - type: sequence
              children:
                - type: action
                  name: scan_for_threats
                - type: action
                  name: move_alongside_vip
        - type: action
          name: report_vip_status

  # ── 7. Patrol Zone (unconditional fallback) ───────────────────────────────────
  # Only patrols when stamina is sufficient; tries perimeter check, falls back
  # to logging, then advances to the next waypoint.
  - name: patrol_zone
    tree:
      type: sequence
      children:
        - type: condition
          name: stamina_ok
        - type: action
          name: move_to_waypoint
        - type: selector
          children:
            - type: action
              name: check_perimeter
            - type: action
              name: log_observation
        - type: action
          name: advance_waypoint

  # ── 8. Debug — intentionally unreachable ─────────────────────────────────────
  # This behavior lives after an unconditional behavior (patrol_zone above) and
  # can never be selected. The complexity analyzer will flag it as PRIORITY_SHADOW.
  - name: debug_never_reached
    tree:
      type: action
      name: log_observation
)";

// ── Registry population ───────────────────────────────────────────────────────

static void populateRegistry(bt::RuntimeRegistry& reg) {
    // ── Blackboard keys ───────────────────────────────────────────────────────
    reg.state("health",          "double");
    reg.state("enemy_distance",  "double");
    reg.state("ammo_count",      "double");
    reg.state("alert_level",     "double");
    reg.state("stamina",         "double");
    reg.state("vip_id",          "double");
    reg.state("allies_nearby",   "double");
    reg.state("threat_count",    "double");
    reg.state("medkit_count",    "double");
    reg.state("patrol_waypoint", "double");

    // ── Conditions — behavior gates ───────────────────────────────────────────
    reg.condition("is_overwhelmed")
        .intent("True when ammo is depleted, health is critical, and threat count is high")
        .reads("health").reads("ammo_count").reads("threat_count")
        .impl([] { return false; });

    reg.condition("is_critically_injured")
        .intent("True when health falls below 25 — triggers emergency medical response")
        .reads("health")
        .impl([] { return false; });

    reg.condition("has_target_in_range")
        .intent("True when a hostile is within effective weapon range and ammo is available")
        .reads("enemy_distance").reads("ammo_count")
        .impl([] { return false; });

    reg.condition("hostile_detected")
        .intent("True when at least one hostile unit has been detected on sensors")
        .reads("threat_count")
        .impl([] { return false; });

    reg.condition("alert_active")
        .intent("True when the tactical alert level exceeds the response threshold (>50)")
        .reads("alert_level")
        .impl([] { return false; });

    reg.condition("vip_assigned")
        .intent("True when the NPC has an active VIP escort assignment")
        .reads("vip_id")
        .impl([] { return false; });

    // ── Conditions — inline tree nodes ────────────────────────────────────────
    reg.condition("medkit_available")
        .intent("True when at least one medkit is present in the NPC inventory")
        .reads("medkit_count")
        .impl([] { return false; });

    reg.condition("has_ammo")
        .intent("True when the primary weapon has one or more rounds chambered")
        .reads("ammo_count")
        .impl([] { return false; });

    reg.condition("has_cover_nearby")
        .intent("True when a valid cover position exists within sprinting distance")
        .reads("enemy_distance")
        .impl([] { return false; });

    reg.condition("vip_in_danger")
        .intent("True when a hostile is within threat proximity of the escorted VIP")
        .reads("vip_id").reads("enemy_distance").reads("threat_count")
        .impl([] { return false; });

    reg.condition("stamina_ok")
        .intent("True when stamina is above the minimum threshold for active patrol")
        .reads("stamina")
        .impl([] { return false; });

    // ── Actions — surrender group ─────────────────────────────────────────────
    reg.action("holster_weapon")
        .intent("Holster the sidearm safely before initiating full surrender")
        .writes("ammo_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("drop_weapon")
        .intent("Drop the primary weapon to the ground to signal non-aggression")
        .writes("ammo_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("raise_hands")
        .intent("Raise both hands above head to signal non-threatening intent")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("kneel_down")
        .intent("Kneel with hands raised to complete the standard surrender posture")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("signal_surrender")
        .intent("Broadcast verbal and radio surrender signal on all channels")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — medical group ───────────────────────────────────────────────
    reg.action("find_nearest_medkit")
        .intent("Locate the closest medkit from the tactical supply overlay")
        .reads("medkit_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("move_to_medkit")
        .intent("Navigate to the identified medkit position")
        .reads("health").reads("stamina")
        .writes("stamina")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("use_medkit")
        .intent("Apply medkit to restore health points")
        .reads("medkit_count").reads("health")
        .writes("health").writes("medkit_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("bandage_wound")
        .intent("Apply a field bandage to slow health drain when no medkit is available")
        .reads("health")
        .writes("health")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("call_for_medic")
        .intent("Broadcast a medic request on the squad channel")
        .reads("health")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — combat group ────────────────────────────────────────────────
    reg.action("take_cover")
        .intent("Sprint to the nearest cover position before engaging")
        .reads("enemy_distance").reads("stamina")
        .writes("stamina")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("aim_at_target")
        .intent("Acquire and lock weapon sights on the current target")
        .reads("enemy_distance")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("fire_weapon")
        .intent("Discharge weapon at the acquired target")
        .reads("ammo_count")
        .writes("ammo_count")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("reload_weapon")
        .intent("Reload the magazine from carried reserve ammunition")
        .reads("ammo_count")
        .writes("ammo_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("throw_grenade")
        .intent("Throw a fragmentation grenade at clustered hostile positions")
        .reads("enemy_distance").reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("call_backup")
        .intent("Request reinforcement units to current position via radio")
        .reads("allies_nearby").reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — pursuit group ───────────────────────────────────────────────
    reg.action("scan_area")
        .intent("Sweep surroundings with active sensor suite to locate hostiles")
        .reads("threat_count")
        .writes("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("move_to_last_known")
        .intent("Navigate to the last known hostile position marker")
        .reads("enemy_distance")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("check_blind_spots")
        .intent("Investigate flanking routes and concealment areas near pursuit path")
        .reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("radio_sighting")
        .intent("Report hostile sighting with GPS coordinates to command and allies")
        .reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — alert response group ───────────────────────────────────────
    reg.action("acknowledge_alert")
        .intent("Confirm alert receipt and create incident log entry")
        .reads("alert_level")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("move_to_alert_zone")
        .intent("Navigate to the zone that generated the tactical alert")
        .reads("alert_level").reads("stamina")
        .writes("stamina")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("clear_area")
        .intent("Systematically search and clear the alerted zone of threats")
        .reads("alert_level").reads("threat_count")
        .writes("alert_level").writes("threat_count")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("report_status")
        .intent("Transmit current situation report including health, ammo, and threat count")
        .reads("health").reads("ammo_count").reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — VIP escort group ────────────────────────────────────────────
    reg.action("locate_vip")
        .intent("Pinpoint current VIP position on the tactical map overlay")
        .reads("vip_id")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("move_alongside_vip")
        .intent("Move in close-protection formation to maintain proximity to VIP")
        .reads("vip_id").reads("stamina")
        .writes("stamina")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("scan_for_threats")
        .intent("Actively sweep surroundings for threats that could endanger the VIP")
        .reads("threat_count").reads("enemy_distance")
        .writes("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("shield_vip")
        .intent("Physically interpose body between VIP and identified threat vector")
        .reads("vip_id").reads("enemy_distance")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("report_vip_status")
        .intent("Transmit VIP safety status and current escort position to command")
        .reads("vip_id").reads("health")
        .impl([] { return bt::Status::SUCCESS; });

    // ── Actions — patrol group ────────────────────────────────────────────────
    reg.action("move_to_waypoint")
        .intent("Navigate to the next assigned waypoint on the patrol route")
        .reads("patrol_waypoint").reads("stamina")
        .writes("stamina")
        .impl([] { return bt::Status::RUNNING; });

    reg.action("check_perimeter")
        .intent("Inspect the perimeter around the current waypoint for anomalies")
        .reads("threat_count")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("log_observation")
        .intent("Record timestamped observations and sensor readings to patrol log")
        .impl([] { return bt::Status::SUCCESS; });

    reg.action("advance_waypoint")
        .intent("Increment the patrol waypoint index to continue the route")
        .reads("patrol_waypoint")
        .writes("patrol_waypoint")
        .impl([] { return bt::Status::SUCCESS; });
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const int port = (argc > 1) ? std::stoi(argv[1]) : 8081;  // NOLINT(readability-magic-numbers)

    const std::string dbPath     = "bt_playground_registry.db";
    const std::string schemaPath = "bt_playground_schema.yaml";

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Arborist Visual Editor — Playground                 ║\n";
    std::cout << "║         City Security Guard NPC scenario                    ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // ── 1. Registry ───────────────────────────────────────────────────────────
    std::cout << "[1/3] Populating registry (" << dbPath << ")\n";
    bt::RuntimeRegistry reg(dbPath);
    populateRegistry(reg);

    const auto& store = reg.store();
    std::cout << "      " << store.allActions().size()    << " actions\n";
    std::cout << "      " << store.allConditions().size() << " conditions\n";
    std::cout << "      " << store.allStateKeys().size()  << " blackboard keys\n\n";

    // ── 2. Schema ─────────────────────────────────────────────────────────────
    std::cout << "[2/3] Writing schema (" << schemaPath << ")\n";
    {
        // Write the schema to disk so the editor can load and save it.
        // We use a const_cast-free approach: EditorServer::saveSchema does the write.
        bt::EditorServer tempServer(const_cast<bt::RegistryStore&>(store), schemaPath);
        if (!tempServer.saveSchema(kSchemaYaml)) {
            std::cerr << "      WARNING: could not write schema file — editor will start empty\n";
        } else {
            std::cout << "      8 behaviors written\n\n";
        }
    }

    // ── 3. Editor server ──────────────────────────────────────────────────────
    std::cout << "[3/3] Starting editor server on port " << port << "\n\n";

    bt::EditorServer editor(const_cast<bt::RegistryStore&>(store), schemaPath);
    editor.start(port);

    std::cout << "┌──────────────────────────────────────────────────────────────┐\n";
    std::cout << "│  Editor ready:   http://localhost:" << port << "                     │\n";
    std::cout << "│                                                              │\n";
    std::cout << "│  What to try:                                                │\n";
    std::cout << "│   • Browse the 8 behaviors in the tree panel                 │\n";
    std::cout << "│   • Click /api/analyze to see the PRIORITY_SHADOW warning    │\n";
    std::cout << "│   • Edit the schema YAML and POST to /api/schema             │\n";
    std::cout << "│   • Add a new action in the editor and watch it appear       │\n";
    std::cout << "│     in /api/actions immediately                              │\n";
    std::cout << "│                                                              │\n";
    std::cout << "│  Press Enter to stop.                                        │\n";
    std::cout << "└──────────────────────────────────────────────────────────────┘\n\n";

    std::cin.get();

    editor.stop();
    std::cout << "\nPlayground stopped.\n";
    return 0;
}
