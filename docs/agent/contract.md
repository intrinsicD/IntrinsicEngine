# Agent Contract (Expanded)

This document expands the concise contract in `/AGENTS.md`. If this file and `/AGENTS.md` ever disagree, `/AGENTS.md` is authoritative.

## Mission

Deliver a modular, high-performance, scientifically rigorous engine for graphics and geometry processing while preserving:

- buildability,
- testability,
- layer ownership,
- documentation synchronization,
- and reviewability.

## Architecture invariants

Required dependency boundaries:

- `core` -> nothing
- `geometry` -> `core`
- `assets` -> `core`
- `ecs` -> `core`; geometry handles/types only when explicitly required
- `graphics/rhi` -> `core`
- `graphics/*` -> `core`, asset IDs, `graphics/rhi`, geometry GPU views; no live ECS knowledge
- `runtime` -> lower layers; owns composition/wiring
- `app` -> `runtime` only
- `methods` -> public method API + declared backend integration only
- `benchmarks` -> public method APIs only
- `tests` -> explicit test seams only

## Coding and change-scope rules

- Use C++23.
- Preserve module names during mechanical moves.
- Do not mix mechanical moves and semantic refactors in one task.
- Avoid introducing new engine features during reorganization.
- Keep patches scoped to one task unless explicitly batched.

## Method implementation protocol

1. Intake paper and define method contract.
2. Implement CPU reference backend.
3. Add correctness tests.
4. Add benchmark harness/manifests.
5. Add optimized CPU backend.
6. Add GPU backend after reference parity.
7. Document numerical limitations and diagnostics.

## Testing and verification protocol

- Run strongest relevant verification subset for touched scope.
- Add/update tests for behavior changes.
- Keep pass rate stable or improved unless temporary shim is explicitly documented.
- Use explicit test categories: `unit`, `contract`, `integration`, `regression`, `gpu`, `benchmark`.

## Documentation sync protocol

When structure, policy, or behavior changes:

- update relevant docs and task records in the same PR,
- update links for moved files,
- regenerate inventories/manifests when required,
- keep docs factual and current-state.

## CI expectations

- PR checks remain green for touched areas.
- Structural checks can start in warning mode and later tighten.
- Workflows remain split by purpose and readable.

## Temporary migration exceptions

Exceptions are allowed only if:

- recorded in `tasks/active/0000-repo-reorganization-tracker.md`,
- linked to a removal task ID,
- time-bounded,
- and isolated so they do not create new promoted-layer violations.
