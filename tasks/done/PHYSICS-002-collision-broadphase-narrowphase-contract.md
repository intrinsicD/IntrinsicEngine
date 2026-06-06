# PHYSICS-002 — Collision broadphase/narrowphase contract

- Status: completed / retired.
- Completion date: 2026-06-06.
- Maturity: `CPUContracted`.

## Goal
- Add CPU-only physics collision broadphase/narrowphase contracts and diagnostics for first-phase primitive shapes using geometry-owned kernels without introducing runtime or ECS ownership into `src/physics`.

## Non-goals
- No impulse/constraint solver.
- No runtime contact event routing beyond data surfaces required by tests.
- No dynamic concave triangle mesh support.
- No GPU or optimized collision backend.

## Context
- Owner/layer: `physics`; allowed dependencies are `core` and `geometry` per [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md).
- Depends on retired [`PHYSICS-001`](PHYSICS-001-physics-world-state-and-runtime-sync.md) for world/body descriptor surfaces and handle lifecycle.
- Uses geometry primitive/contact kernels as implementation details, wrapping outputs in physics-owned records.
- First-phase dynamic-capable shapes are sphere, capsule, and box/OBB. Convex hull, static/kinematic triangle mesh, height field, SDF, and convex decomposition remain later tasks unless a follow-up explicitly opens them.
- This slice intentionally uses deterministic all-pairs candidate enumeration as the CPU contract. Spatial acceleration is a future optimization task and must preserve the same candidate/contact ordering semantics or document a stronger contract.

## Required changes
- [x] Define physics collision shape records and cooked-shape diagnostics for sphere, capsule, and box/OBB.
- [x] Add broadphase candidate generation with deterministic ordering and explicit invalid-shape rejection.
- [x] Add narrowphase contact generation outputs with contact normal, penetration/depth, witness points, pair identity, and diagnostic reason.
- [x] Route broadphase/narrowphase through geometry-owned kernels where applicable, wrapping outputs in physics-owned result records.
- [x] Add collision filtering inputs and diagnostics without importing ECS or runtime filter state.
- [x] Add explicit rejection diagnostics for unsupported dynamic concave mesh, non-uniform dynamic scale, non-finite transforms, and invalid primitive parameters.

## Tests
- [x] Add `unit;physics` tests for sphere/sphere, sphere/capsule, sphere/box, capsule/box, box/box overlap and separation cases.
- [x] Add deterministic ordering tests for broadphase candidate generation.
- [x] Add degenerate input tests for non-finite values, zero/negative radii/extents, unsupported shape types, and non-uniform dynamic scale.
- [x] Keep `METHOD-001` parity out of this slice; no shared contact-fixture harness exists yet, so future parity comparison must be opened by a separate method/physics follow-up before claiming reference parity.

## Docs
- [x] Update `docs/architecture/physics.md` with collision shape and diagnostic contracts.
- [x] No method-doc update required; this slice did not introduce shared `METHOD-001` parity fixtures.

## Acceptance criteria
- [x] Physics collision contracts produce deterministic broadphase candidates and narrowphase contacts for first-phase primitives.
- [x] Unsupported shapes and invalid authoring produce diagnostics instead of silent skips.
- [x] `src/physics` remains independent of ECS/runtime/graphics/platform/app.

## Verification
```bash
cmake --build --preset ci --target IntrinsicPhysicsWorldTests
ctest --test-dir build/ci --output-on-failure -R '^PhysicsWorld\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Completion
- Completed: 2026-06-06.
- Commit reference: this task-retirement commit.
- Notes: `Physics.World` now exposes `ComputeCollisionContacts()` returning physics-owned candidate/contact/diagnostic records. The slice is CPU-only and does not add solver impulses, event routing, optimized broadphase, or GPU work.

## Forbidden changes
- Adding solver impulses, constraints, or integration policy beyond collision result generation.
- Importing runtime/ECS state into physics collision code.
- Treating geometry collision kernels as ownership of physics world/state.
