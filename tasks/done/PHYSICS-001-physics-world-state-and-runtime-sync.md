# PHYSICS-001 — Physics world state and runtime fixed-step sync

## Status

- Completed: 2026-06-05.
- Maturity: `CPUContracted`.
- Commit/PR: this task retirement commit.
- Summary: added `src/physics` with `Extrinsic.Physics.World`, a CPU-only
  world/body lifecycle and deterministic unconstrained fixed-step contract, and
  added `Extrinsic.Runtime.PhysicsBridge`, which maps ECS `StableId` values to
  generation-checked physics body handles in a runtime-owned sidecar, syncs ECS
  collider/rigid-body authoring descriptors, steps the physics world on a
  fixed accumulator, and writes dynamic body transforms back to ECS with dirty
  markers.
- `Operational` engine-frame composition and solver/island/sleep behavior
  remain deferred. Collision contracts are now `CPUContracted` by
  [`PHYSICS-002`](PHYSICS-002-collision-broadphase-narrowphase-contract.md);
  constraints/islands/sleep diagnostics are owned by
  [`PHYSICS-003`](../backlog/physics/PHYSICS-003-constraints-islands-and-solver-diagnostics.md).

## Goal
- Add the first `src/physics` world/state module and runtime-owned fixed-step bridge that maps ECS physics authoring descriptors to physics-world handles without leaking handles back into ECS.

## Non-goals
- No broadphase/narrowphase implementation beyond placeholders required to validate lifecycle.
- No constraint solver, contact resolution, sleep/island solver, or optimized backend.
- No graphics, platform, app, or method-package imports from `src/physics`.
- No ECS component expansion beyond what retired [`HARDEN-064`](HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) already owns.

## Context
- Owner/layer: `physics` plus `runtime` bridge. `physics -> core, geometry`; `runtime` composes ECS and physics.
- Upstream decision: [`ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md) / [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md).
- This task should start only after `HARDEN-064` defines the ECS collider/rigid-body authoring descriptors and `METHOD-001` has enough CPU reference contract to validate at least unconstrained deterministic stepping.
- Runtime owns stable-entity-to-physics-handle sidecars, fixed-step scheduling, descriptor sync, transform writeback, and event routing.

## Required changes
- [x] Add a minimal `src/physics` module library and `Extrinsic.Physics.*` public module surface for world creation, body handles, body descriptors, step inputs, step diagnostics, and deterministic lifecycle.
- [x] Update CMake with an `ExtrinsicPhysics` target using `intrinsic_add_module_library(...)` and link only allowed dependencies.
- [x] Add physics-world create/destroy/add/remove/update descriptor APIs with generation-checked handles and no ECS/runtime types in the public physics module.
- [x] Add runtime bridge state that maps ECS stable identity to physics handles and owns descriptor synchronization.
- [x] Add fixed-step runtime scheduling that steps physics after ECS authoring updates and before physics-to-ECS writeback.
- [x] Add physics-to-ECS transform writeback policy for dynamic bodies, with static/kinematic behavior documented and diagnosed.
- [x] Add diagnostics for invalid descriptors, stale handles, missing authoring components, sync counts, writeback counts, and fixed-step counts.

## Tests
- [x] Add `unit;physics` tests for world lifecycle, handle generation, descriptor update, deterministic step count, and stale handle rejection.
- [x] Add `integration;runtime;ecs;physics` tests for ECS authoring sync, stable-identity handle reuse/removal, fixed-step execution order, and transform writeback.
- [x] Add a layering regression if any new module prefix or CMake target edge is missed by tooling. The existing strict layering scanner now covers `src/physics`.

## Docs
- [x] Update `docs/architecture/physics.md` with the implemented world/state surface.
- [x] Update `docs/architecture/runtime.md` with the concrete bridge and fixed-step ordering.
- [x] Regenerate `docs/api/generated/module_inventory.md` after adding modules.

## Acceptance criteria
- [x] `src/physics` exists with a CPU-only world/state contract and no forbidden imports.
- [x] Runtime can synchronize ECS descriptors into physics handles and write simulated transforms back through a deterministic fixed-step path.
- [x] ECS components store no physics-world handles or runtime sidecars.
- [x] Tests prove lifecycle, ordering, and diagnostics through CPU-only seams.

## Verification

Completed in this slice:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
cmake --build --preset ci --target IntrinsicPhysicsWorldTests IntrinsicRuntimePhysicsBridgeTests
ctest --test-dir build/ci -R 'PhysicsWorld|RuntimePhysicsBridge' --output-on-failure --timeout 60
ctest --test-dir build/ci --output-on-failure -L 'physics|runtime|ecs' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
```

Results:
- Focused physics world/runtime bridge CTest subset passed: 10/10.
- `IntrinsicTests` built successfully with the `ci` preset; build output also
  contained pre-existing unrelated warnings in graphics test sources.
- Touched label gate passed: 584/584 tests for `physics|runtime|ecs` excluding
  `gpu|vulkan|slow|flaky-quarantine`.
- Default CPU-supported correctness gate passed: 2785/2785 tests excluding
  `gpu|vulkan|slow|flaky-quarantine`.
- Strict layering, test layout, task policy, task state links, docs link,
  docs-sync, PR-contract, and diff whitespace checks passed.
- Root hygiene passed with the existing warning-mode `.agents/` notice.

## Forbidden changes
- Storing physics handles, proxies, contacts, islands, or solver indices in canonical ECS components.
- Letting `src/physics` import ECS, runtime, graphics/RHI, platform, app, assets services, or method packages.
- Implementing contact solving or claiming operational physics behavior beyond lifecycle/sync unless covered by a scoped follow-up and tests.
