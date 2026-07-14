---
id: PHYSICS-003
theme: C
depends_on: [PHYSICS-001, PHYSICS-002]
---
# PHYSICS-003 — Constraints, islands, sleep, and solver diagnostics

## Goal
- Add the first CPU-only rigid-body constraint/island/sleep solver diagnostics layer on top of the physics world and collision contacts.

## Non-goals
- No optimized CPU or GPU solver backend.
- No articulated-body, ragdoll, vehicle, cloth, fluid, FEM, or particle solver.
- No runtime/editor UI beyond consuming diagnostics already exposed by the physics/runtime bridge.
- No performance claims without benchmark baselines.

## Context
- Owner/layer: `physics`, with runtime only composing the fixed-step bridge.
- Depends on retired [`PHYSICS-001`](PHYSICS-001-physics-world-state-and-runtime-sync.md) and retired [`PHYSICS-002`](PHYSICS-002-collision-broadphase-narrowphase-contract.md).
- `METHOD-001` is the correctness reference for rigid-body dynamics. This task compares against the CPU reference where fixture coverage exists.
- Solver state, islands, warm-start caches, and sleep state are physics-world internals and must not leak into ECS components.
- Landed (2026-06-10) in `Extrinsic.Physics.World`: `SolveStep(StepInput, SolverSettings)` (sleep-aware integrate → contacts → islands → iterative linear contact solve → sleep policy), `BuildIslands(CollisionResult)` (deterministic union-find islands; static/kinematic anchors never merge islands; trigger contacts excluded), `IsBodyAsleep`/`WakeBody`, and the `SolveStepDiagnostics`/`IslandDiagnostics`/`SleepDiagnostics` records mirrored into `WorldDiagnostics::LastSolveStep`/`SolveStepsExecuted`. `Step()` remains the raw PHYSICS-001 integrator.
- Two correctness findings fixed under this task's parity/fixture work:
  - The world damping factor was `1/(1+c·dt)` while the canonical METHOD-001 reference uses `max(0, 1-c·dt)`; aligned to the reference so parity fixtures track it exactly (free-fall parity within `1e-4` over 60 steps).
  - The geometry kernel's sphere-box analytic and GJK/EPA fallback contact normals are B→A-oriented despite the documented A→B convention; the physics `ContactRecord` now enforces A→B by orienting against the shape-center offset (regression: `PhysicsWorld.ContactRecordNormalFollowsAToBConvention`). The kernel defect is tracked as follow-up [`BUG-025`](BUG-025-contact-manifold-normal-convention.md) (fixed 2026-06-10).
- Completed: 2026-06-10.
- PR/commit: this task retirement commit.

## Maturity

- Target and result: `CPUContracted`. The solver is CPU-only by design for this slice; no `Operational` backend follow-up is owed (optimized/GPU solver backends are explicitly out of scope and would be new method/physics tasks).

## Required changes
- [x] Define island-building inputs/outputs and deterministic ordering (`BuildIslands` + `IslandRecord` ordering contract: islands by smallest member index, members sorted, contact indices ascending).
- [x] Add sleep-state policy and diagnostics for awake/asleep transitions (low-motion accumulation, island-wide wake, `UpdateBody`/`WakeBody` wake, `SleepDiagnostics` counters).
- [x] Add first constraint/contact solve diagnostics: iteration count, residual/penetration tolerance, non-convergence, energy drift, and degraded/fallback status (`SolveStepDiagnostics`, `SolveStatus::{Converged,MaxIterationsReached,Degraded}`).
- [x] Preserve fixed-step determinism for repeated identical inputs (`PhysicsWorld.SolveStepIsDeterministicForRepeatedIdenticalInputs`, bitwise-equal evolution).
- [x] Compare simple fixtures against `METHOD-001` CPU reference outputs (`Test.PhysicsSolverParity.cpp`; damping aligned to the reference).
- [x] Publish diagnostics through physics-owned records consumed by runtime without runtime imports in physics (records live in `Extrinsic.Physics.World`; layering check clean).

## Tests
- [x] `unit;physics` tests for island grouping, sleep transition policy, and deterministic repeated stepping (`Test.Physics.World.cpp` PHYSICS-003 section).
- [x] Contact/constraint toy cases with explicit convergence and non-convergence diagnostics (`SolveStepSeparatesOverlappingSpheresAndConverges`, `SolveStepReportsNonConvergenceUnderStarvedIterationBudget`, `SolveStepRejectsInvalidSolverSettings`, `SolveStepEnergyDriftIsReportedAndBoundedForRestingContact`).
- [x] Parity tests against `METHOD-001` reference fixtures (`PhysicsSolverParity.FreeFallTracksReferenceIntegrator`, `PhysicsSolverParity.OverlappingSpherePairMatchesReferenceContactSolve`).

## Docs
- [x] Updated `docs/architecture/physics.md` with solver/island/sleep ownership, the damping alignment, the linear-only energy scope, and the BUG-025 normal-convention caveat.
- [x] Updated `methods/physics/rigid_body_reference/README.md` with the shared parity fixtures and tolerance.
- [x] No benchmark manifests added (no performance claims made).

## Acceptance criteria
- [x] Physics owns islands, sleep, constraints, and solver diagnostics without leaking state to ECS/runtime (`check_layering --strict` clean; sleep state in world slots).
- [x] Deterministic CPU tests cover repeated stepping and diagnostic failure modes.
- [x] Reference-method comparison is explicit and tolerance-documented (absolute `1e-4`, float-vs-double accumulation, documented in the parity test header and method README).

## Verification

Commands actually run for retirement (2026-06-10):

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60   # 41/41 passed
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60   # 2893/2893 passed
python3 tools/repo/check_layering.py --root src --strict    # clean
python3 tools/repo/check_test_layout.py --root . --strict   # clean
python3 tools/docs/check_doc_links.py --root .              # clean
```

## Forbidden changes
- Adding optimized or GPU physics solver backends.
- Storing solver-owned state in ECS components.
- Reporting performance improvements without benchmark baselines.
