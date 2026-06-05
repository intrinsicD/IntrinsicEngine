# PHYSICS-002 — Collision broadphase/narrowphase contract

## Goal
- Add CPU-only physics collision broadphase/narrowphase contracts and diagnostics for first-phase primitive shapes using geometry-owned kernels without introducing runtime or ECS ownership into `src/physics`.

## Non-goals
- No impulse/constraint solver.
- No runtime contact event routing beyond data surfaces required by tests.
- No dynamic concave triangle mesh support.
- No GPU or optimized collision backend.

## Context
- Owner/layer: `physics`; allowed dependencies are `core` and `geometry` per [ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md).
- Depends on [`PHYSICS-001`](PHYSICS-001-physics-world-state-and-runtime-sync.md) for world/body descriptor surfaces and handle lifecycle.
- Uses geometry collision/math kernels as primitives, not as a physics world owner.
- First-phase dynamic-capable shapes are sphere, capsule, and box/OBB. Convex hull, static/kinematic triangle mesh, height field, SDF, and convex decomposition remain later tasks unless this task explicitly splits a child.

## Required changes
- [ ] Define physics collision shape records and cooked-shape diagnostics for sphere, capsule, and box/OBB.
- [ ] Add broadphase candidate generation with deterministic ordering and explicit invalid-shape rejection.
- [ ] Add narrowphase contact generation outputs with contact normal, penetration/depth, witness points, pair identity, and diagnostic reason.
- [ ] Route broadphase/narrowphase through geometry-owned kernels where applicable, wrapping outputs in physics-owned result records.
- [ ] Add collision filtering inputs and diagnostics without importing ECS or runtime filter state.
- [ ] Add explicit rejection diagnostics for unsupported dynamic concave mesh, non-uniform dynamic scale, non-finite transforms, and invalid primitive parameters.

## Tests
- [ ] Add `unit;physics` tests for sphere/sphere, sphere/capsule, sphere/box, capsule/box, box/box overlap and separation cases.
- [ ] Add deterministic ordering tests for broadphase candidate generation.
- [ ] Add degenerate input tests for non-finite values, zero/negative radii/extents, unsupported shape types, and non-uniform dynamic scale.
- [ ] Add parity tests against `METHOD-001` reference outputs when the method backend exposes matching contact fixtures.

## Docs
- [ ] Update `docs/architecture/physics.md` with collision shape and diagnostic contracts.
- [ ] Update method docs if parity fixtures are shared with `METHOD-001`.

## Acceptance criteria
- [ ] Physics collision contracts produce deterministic broadphase candidates and narrowphase contacts for first-phase primitives.
- [ ] Unsupported shapes and invalid authoring produce diagnostics instead of silent skips.
- [ ] `src/physics` remains independent of ECS/runtime/graphics/platform/app.

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
- Adding solver impulses, constraints, or integration policy beyond collision result generation.
- Importing runtime/ECS state into physics collision code.
- Treating geometry collision kernels as ownership of physics world/state.
