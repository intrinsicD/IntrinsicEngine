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
- Depends on retired [`PHYSICS-001`](../../done/PHYSICS-001-physics-world-state-and-runtime-sync.md) and retired [`PHYSICS-002`](../../done/PHYSICS-002-collision-broadphase-narrowphase-contract.md).
- `METHOD-001` is the correctness reference for rigid-body dynamics. This task must compare against the CPU reference where fixture coverage exists.
- Solver state, islands, warm-start caches, and sleep state are physics-world internals and must not leak into ECS components.

## Required changes
- [ ] Define island-building inputs/outputs and deterministic ordering.
- [ ] Add sleep-state policy and diagnostics for awake/asleep transitions.
- [ ] Add first constraint/contact solve diagnostics: iteration count, residual/penetration tolerance, non-convergence, energy drift, and degraded/fallback status.
- [ ] Preserve fixed-step determinism for repeated identical inputs.
- [ ] Compare simple fixtures against `METHOD-001` CPU reference outputs when available.
- [ ] Publish diagnostics through physics-owned records consumed by runtime without runtime imports in physics.

## Tests
- [ ] Add `unit;physics` tests for island grouping, sleep transition policy, and deterministic repeated stepping.
- [ ] Add contact/constraint toy cases with explicit convergence and non-convergence diagnostics.
- [ ] Add parity tests against `METHOD-001` reference fixtures when available.

## Docs
- [ ] Update `docs/architecture/physics.md` with solver/island/sleep ownership.
- [ ] Update `methods/physics/rigid_body_reference/README.md` if parity fixtures or diagnostics are shared.
- [ ] Add benchmark manifests only if reporting smoke/reference diagnostics, not performance wins.

## Acceptance criteria
- [ ] Physics owns islands, sleep, constraints, and solver diagnostics without leaking state to ECS/runtime.
- [ ] Deterministic CPU tests cover repeated stepping and diagnostic failure modes.
- [ ] Any reference-method comparison is explicit and tolerance-documented.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding optimized or GPU physics solver backends.
- Storing solver-owned state in ECS components.
- Reporting performance improvements without benchmark baselines.
