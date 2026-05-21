# Arborist Visual Editor

The standalone editor for authoring and analyzing behavior tree schemas. Use it to declare action/condition contracts, write the YAML schema, and run the complexity analyzer — all through a browser UI or REST API.

Unlike the [playground](../playground/README.md), the editor starts with an empty registry and persists everything you add across sessions. It is the tool you use for real project work.

## Quick start

```bash
cd editor
./run.sh
# Open http://localhost:8081
```

The script builds the project if needed, then launches the editor. Your registry and schema are saved in this directory.

## How it works

The editor has two persistence layers:

| Layer | File | What it stores |
|-------|------|----------------|
| Registry | `arborist_registry.db` (SQLite) | Action/condition contracts — names, intents, blackboard read/write declarations |
| Schema | `arborist_schema.yaml` | The behavior tree YAML — behaviors, priorities, tree structure |

Both files are created on first run and updated as you work. They are excluded from git by `.gitignore`.

## Workflow

1. **Register contracts** — declare every action and condition your game/app will implement, together with what blackboard keys they read and write. Use the editor UI or `POST` directly to the API.

2. **Write the schema** — compose behaviors in the YAML editor. Reference the registered action/condition names. The editor auto-completes from the registered contracts.

3. **Analyze** — hit `GET /api/analyze` to run the complexity analyzer. It catches unreachable behaviors, missing conditions, and structural issues before you ship.

4. **Export** — copy `arborist_schema.yaml` into your project and load it at runtime with `SchemaLoader::load()`.

## Options

```
./run.sh [--db <path>] [--schema <path>] [--port <port>]

  --db     <path>   Registry database  (default: arborist_registry.db)
  --schema <path>   Schema YAML file   (default: arborist_schema.yaml)
  --port   <port>   HTTP port          (default: 8081)
  --help            Print help
```

Examples:

```bash
./run.sh --port 9000
./run.sh --db ~/myproject/bt.db --schema ~/myproject/npc.yaml
```

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/` | Editor UI |
| `GET` | `/api/actions` | All registered actions (name, intent, reads, writes) |
| `PUT` | `/api/actions` | Add or update an action contract |
| `DELETE` | `/api/actions/:name` | Remove an action contract |
| `GET` | `/api/conditions` | All registered conditions (name, intent, reads) |
| `PUT` | `/api/conditions` | Add or update a condition contract |
| `DELETE` | `/api/conditions/:name` | Remove a condition contract |
| `GET` | `/api/blackboard` | All declared blackboard keys |
| `PUT` | `/api/blackboard` | Add or update a blackboard key declaration |
| `DELETE` | `/api/blackboard/:key` | Remove a blackboard key declaration |
| `GET` | `/api/schema` | Current schema YAML wrapped in JSON |
| `POST` | `/api/schema` | Save updated schema YAML to disk (triggers hot-reload if tree attached) |
| `GET` | `/api/analyze` | Run complexity analyzer — returns issues and metrics |
| `GET` | `/api/tickstate` | Latest tick record — active path, statuses, tick number |

## Integrating with a C++ project

Once your schema is authored, load it at runtime:

```cpp
#include "bt/RuntimeRegistry.h"
#include "bt/SchemaLoader.h"

bt::RuntimeRegistry reg;

// Register the same actions/conditions you declared in the editor
reg.action("patrol")
    .intent("Walk the patrol route")
    .impl([] { return bt::Status::RUNNING; });

// Load the YAML produced by the editor
auto tree = bt::SchemaLoader::load(reg, "arborist_schema.yaml");

// Tick each frame
while (running) {
    tree.tick();
}
```

The names in the schema must match the names you register in `RuntimeRegistry` — the editor's contract declarations enforce this at design time.

## Prerequisites

- CMake ≥ 3.20
- C++20 compiler (GCC 12+ or Clang 15+)
- `yaml-cpp`, `SQLite3`, `Threads` (see root [README](../README.md))

## Stopping

Press **Enter** in the terminal.
