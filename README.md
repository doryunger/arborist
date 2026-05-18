# BT Framework

A game-engine-agnostic Behavior Tree runtime and management framework.

The framework owns BT definition, execution, monitoring, and testing. The engine is reduced to a state provider and command executor.

## Architecture

```
Engine                          Framework
────────────────              ──────────────────────────────
Renders world                   Owns BT definition (schema)
Reports sensor state  →         Runs BT runtime (tick loop)
Executes commands     ←         Manages blackboard (game state)
                                Monitors all decisions
                                Tests scenarios without engine
```

## Build

```bash
cmake -B build
cmake --build build
```

## Project Layout

```
include/bt/       Public headers
src/runtime/      Status, Node, Blackboard, BehaviorTree, all node types
src/builder/      BehaviorBuilder, StateRegistry, PriorityResolver, TreeAssembler
src/validation/   Startup validation layer
src/schema/       YAML parser and schema validator
tests/            Unit and integration tests
schemas/examples/ Example YAML behavior definitions
docs/             Design docs and plans
```

## Phases

- **Phase 1 + 2 (Core):** Runtime + Schema — built together
- **Phase 3:** Mock Engine + Simulator
- **Phase 4:** Monitor + Observability
- **Phase 5:** Test Harness
- **Phase 6:** Unity Adapter + Demo Game
