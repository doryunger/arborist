# Arborist — Development Rules

## Workflow

Every step follows this sequence — no exceptions:

1. **Plan** — describe what we're building and why it comes next
2. **Test first** — write or extend the test before writing the implementation
3. **Implement** — write the code to make the test pass
4. **Verify** — build and run tests, confirm green before moving on

Never move to the next step until the current step is green.

## Test file discipline

- One evolving test file: `tests/test_arborist.cpp`
- Each step adds new test cases — existing cases are never removed
- Tests are cumulative: step N tests cover steps 1..N
- Test names follow the pattern `StepN_WhatItVerifies`

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

## What not to do

- Do not add features beyond what the current step requires
- Do not refactor previous steps while implementing a new one
- Do not skip the test — if it's hard to test, the design is probably wrong
