# Arborist — Development Rules

## Workflow

Every step follows this sequence — no exceptions:

1. **Plan** — describe what we're building and why it comes next
2. **Test first** — write or extend the test before writing the implementation
3. **Implement** — write the code to make the test pass
4. **Verify** — build and run tests, confirm green before moving on

Never move to the next step until the current step is green.

## Test file discipline

- One test file per phase: `tests/phaseN_<name>.cpp`
- Each phase file grows as steps are added — never split across files
- Existing test cases are never removed
- Test names follow the pattern `PhaseN_Concept.WhatItVerifies`
- Type-trait guarantees go in `static_assert` at file scope, not in TEST() bodies
- Always cover: happy path, edge cases, and polymorphic dispatch via base pointer
- `phase0_setup.cpp` — build pipeline smoke test (permanent infrastructure)
- `phase1_runtime.cpp` — all Phase 1 runtime tests (Status → BehaviorTree)
- `phase2_schema.cpp` — all Phase 2 schema tests (added when Phase 2 begins)

## Step sizing

Steps are small and focused — one concept at a time. If a step feels large, split it.
Good step size: something that can be planned, tested, and implemented in a single session.

## Design decisions

See [docs/BT_FRAMEWORK_PLAN.md](docs/BT_FRAMEWORK_PLAN.md) for the full plan and all resolved design decisions.

Key rules derived from the plan:
- `RUNNING` resumption must never re-evaluate from root — resume the active node
- Parallel policy is always configurable per node (`ALL` / `ANY` / `THRESHOLD(n)`)
- Validation runs before the first tick, never during
- YAML conditions reference named C++ conditions only — no inline logic in schema

## C++ standard and style

This project targets **C++20**. Always use the most modern and safest syntax available.

- Prefer `enum class` over plain `enum`
- Prefer `std::string_view` over `const std::string&` for read-only strings
- Prefer `std::unique_ptr` / `std::shared_ptr` over raw owning pointers
- Prefer `[[nodiscard]]` on functions whose return value must not be ignored
- Use `= delete` explicitly for unwanted special members
- Use `noexcept` where the function cannot throw
- Use `static_assert` for compile-time invariants
- Use `dynamic_cast` for base-to-derived casts, never `static_cast`
- Use `std::uint8_t`, `std::int32_t` etc. for sized integer types
- Never use C-style casts (`(Type)value`) — use `static_cast`, `dynamic_cast`, or `std::bit_cast`
- Never use raw arrays — use `std::array` or `std::vector`
- Never use `NULL` — use `nullptr`
- Never use `#define` for constants — use `constexpr`
- Avoid deprecated standard library features — check cppreference for deprecation status

clang-tidy enforces these rules at build time with `WarningsAsErrors`.

## Quality standard

**Do what is right, not what is easy.** This is a non-negotiable rule.

A feature is not done when it compiles and passes a happy-path test. It is done when:
- Edge cases and failure paths are handled
- It integrates correctly with every component that depends on it
- It has been tested at the boundary (null inputs, empty state, concurrent access where applicable)
- It does not leave a known gap that a user will fall into

If a component has a known gap, that gap must be tracked and closed — not worked around, not deferred indefinitely, not documented as "future work" and forgotten. Half-baked components make the whole framework untrustworthy.

Before marking any phase complete, explicitly ask: *what would break this in production?* If the answer is non-trivial, the phase is not complete.

## What not to do

- Do not add features beyond what the current step requires
- Do not refactor previous steps while implementing a new one
- Do not skip the test — if it's hard to test, the design is probably wrong
- Do not declare a component "done" when it only handles the happy path
- Do not build two components that must work together as if they are independent
