# Arborist Playground

An interactive editor playground pre-loaded with a rich City Security Guard NPC scenario. It lets you explore, edit, and analyze a behavior tree schema directly in a browser — no simulation running, no ticking loop. It is the fastest way to manually QA the visual editor and to show new users what the framework can do.

## What it is

The playground starts the Arborist visual editor (`EditorServer`) with:

- **33 actions**, **11 conditions**, **10 blackboard keys** — all pre-registered with intents and read/write declarations
- **8 behaviors** in priority order, covering sequences, selectors, parallel branches (`any` / `all` policies), and inline condition nodes
- A schema file on disk (`bt_playground_schema.yaml`) that the editor loads and saves
- A SQLite registry (`bt_playground_registry.db`) that persists contract data across sessions

## Scenario: City Security Guard NPC

| Priority | Behavior | Gate condition | Description |
|----------|----------|---------------|-------------|
| 1 | `surrender` | `is_overwhelmed` | Drops weapon, raises hands, kneels — full 5-step sequence |
| 2 | `seek_medical_aid` | `is_critically_injured` | Selector: medkit → bandage → call medic |
| 3 | `combat_engage` | `has_target_in_range` | Selector: shoot from cover → reload and shoot → grenade + backup |
| 4 | `pursue_hostile` | `hostile_detected` | Scan, then parallel pursuit or radio sighting, then call backup |
| 5 | `respond_to_alert` | `alert_active` | Acknowledge, move+report in parallel, clear area, report |
| 6 | `escort_vip` | `vip_assigned` | Locate VIP, then protective parallel branch, then status report |
| 7 | `patrol_zone` | *(unconditional)* | Stamina check, waypoint navigation, perimeter sweep |
| 8 | `debug_never_reached` | *(unconditional)* | Intentionally unreachable — triggers `PRIORITY_SHADOW` analyzer warning |

**Blackboard keys:** `health`, `enemy_distance`, `ammo_count`, `alert_level`, `stamina`, `vip_id`, `allies_nearby`, `threat_count`, `medkit_count`, `patrol_waypoint`

## Prerequisites

- CMake ≥ 3.20
- A C++20 compiler (GCC 12+ or Clang 15+)
- `yaml-cpp`, `SQLite3`, and `Threads` available to CMake (see the root [README](../README.md) for setup)

## Running

```bash
cd playground
./run.sh            # starts on port 8081
./run.sh 9000       # custom port
```

The script builds the project if needed, then launches the editor. Open the printed URL in a browser.

### Manual build

```bash
cmake -S .. -B ../build -DCMAKE_BUILD_TYPE=Release
cmake --build ../build --target bt_playground -j$(nproc)

# Run from this directory so schema/db files land here
cd playground
../build/playground/bt_playground
```

## Files created at runtime

| File | Description |
|------|-------------|
| `bt_playground_schema.yaml` | The active schema. Edit it in the browser or directly; changes are saved here. |
| `bt_playground_registry.db` | SQLite database storing action/condition contracts. Persists across sessions. |

Both files are listed in `.gitignore` and will not be committed.

## Things to try

| Task | How |
|------|-----|
| Browse the tree structure | Open the editor UI and expand each behavior |
| See the complexity analysis | `GET /api/analyze` — look for the `PRIORITY_SHADOW` warning on `debug_never_reached` |
| Edit the schema | Modify the YAML in the editor and `POST /api/schema` to save |
| Add a new action | Use the editor's contract panel or `PUT` to `/api/actions` |
| Inspect blackboard keys | `GET /api/blackboard` — all 10 keys with types |
| Reset to the original schema | Delete `bt_playground_schema.yaml` and restart |
| Reset the registry | Delete `bt_playground_registry.db` and restart |

## REST API quick reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Visual editor UI |
| `GET` | `/api/actions` | All registered actions (name, intent, reads, writes) |
| `PUT` | `/api/actions` | Add or update an action contract |
| `DELETE` | `/api/actions/:name` | Remove an action contract |
| `GET` | `/api/conditions` | All registered conditions |
| `PUT` | `/api/conditions` | Add or update a condition contract |
| `DELETE` | `/api/conditions/:name` | Remove a condition contract |
| `GET` | `/api/blackboard` | All declared blackboard keys |
| `PUT` | `/api/blackboard` | Add or update a blackboard key declaration |
| `DELETE` | `/api/blackboard/:key` | Remove a blackboard key declaration |
| `GET` | `/api/schema` | Current schema YAML wrapped in JSON |
| `POST` | `/api/schema` | Save updated schema YAML to disk |
| `GET` | `/api/analyze` | Run complexity analyzer — returns issues + metrics |
| `GET` | `/api/tickstate` | Latest tick record — active path, statuses, tick number |

## Stopping

Press **Enter** in the terminal where the playground is running.
