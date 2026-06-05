# PHYSICS-001 — Physics world state and runtime fixed-step sync

## Goal
- Add the first `src/physics` world/state module and runtime-owned fixed-step bridge that maps ECS physics authoring descriptors to physics-world handles without leaking handles back into ECS.

## Non-goals
- No broadphase/narrowphase implementation beyond placeholders required to validate lifecycle.
- No constraint solver, contact resolution, sleep/island solver, or optimized backend.
- No graphics, platform, app, or method-package imports from `src/physics`.
- No ECS component expansion beyond what [`HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) already owns.

## Context
- Owner/layer: `physics` plus `runtime` bridge. `physics -> core, geometry`; `runtime` composes ECS and physics.
- Upstream decision: [`ARCH-001`](../../done/ARCH-001-physics-layer-ownership-and-ecs-integration.md) / [ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md).
- This task should start only after `HARDEN-064` defines the ECS collider/rigid-body authoring descriptors and `METHOD-001` has enough CPU reference contract to validate at least unconstrained deterministic stepping.
- Runtime owns stable-entity-to-physics-handle sidecars, fixed-step scheduling, descriptor sync, transform writeback, and event routing.

## Required changes
- [ ] Add a minimal `src/physics` module library and `Extrinsic.Physics.*` public module surface for world creation, body handles, body descriptors, step inputs, step diagnostics, and deterministic lifecycle.
- [ ] Update CMake with an `ExtrinsicPhysics` target using `intrinsic_add_module_library(...)` and link only allowed dependencies.
- [ ] Add physics-world create/destroy/add/remove/update descriptor APIs with generation-checked handles and no ECS/runtime types in the public physics module.
- [ ] Add runtime bridge state that maps ECS stable identity to physics handles and owns descriptor synchronization.
- [ ] Add fixed-step runtime scheduling that steps physics after ECS authoring updates and before physics-to-ECS writeback.
- [ ] Add physics-to-ECS transform writeback policy for dynamic bodies, with static/kinematic behavior documented and diagnosed.
- [ ] Add diagnostics for invalid descriptors, stale handles, missing authoring components, sync counts, writeback counts, and fixed-step counts.

## Tests
- [ ] Add `unit;physics` tests for world lifecycle, handle generation, descriptor update, deterministic step count, and stale handle rejection.
- [ ] Add `integration;runtime;ecs;physics` tests for ECS authoring sync, stable-identity handle reuse/removal, fixed-step execution order, and transform writeback.
- [ ] Add a layering regression if any new module prefix or CMake target edge is missed by tooling.

## Docs
- [ ] Update `docs/architecture/physics.md` with the implemented world/state surface.
- [ ] Update `docs/architecture/runtime.md` with the concrete bridge and fixed-step ordering.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after adding modules.

## Acceptance criteria
- [ ] `src/physics` exists with a CPU-only world/state contract and no forbidden imports.
- [ ] Runtime can synchronize ECS descriptors into physics handles and write simulated transforms back through a deterministic fixed-step path.
- [ ] ECS components store no physics-world handles or runtime sidecars.
- [ ] Tests prove lifecycle, ordering, and diagnostics through CPU-only seams.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'physics|runtime|ecs' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Storing physics handles, proxies, contacts, islands, or solver indices in canonical ECS components.
- Letting `src/physics` import ECS, runtime, graphics/RHI, platform, app, assets services, or method packages.
- Implementing contact solving or claiming operational physics behavior beyond lifecycle/sync unless covered by a scoped follow-up and tests.
