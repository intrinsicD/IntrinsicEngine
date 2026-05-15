# HARDEN-067 — Add ECS world-bounds propagation system

## Status

- Status: done.
- Completed: 2026-05-15 on branch `claude/setup-agentic-workflow-GtS0t`.
- Verification (this session):
  - `cmake --preset ci` (Clang 20.1.2, Ninja).
  - `cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests IntrinsicRuntimeContractTests IntrinsicCoreTests IntrinsicGeometryTests`.
  - `ctest --test-dir build/ci -L 'ecs|contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — 146/146 passed.
  - `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` — every built fixture green; the only "Not Run" rows are graphics/runtime test umbrellas that were not built in this focused-scope session.
  - `python3 tools/repo/check_layering.py --root src --strict` — clean.
  - `python3 tools/repo/check_test_layout.py --root . --strict` — clean.
  - `python3 tools/docs/check_doc_links.py --root .` — clean for touched scope.
  - `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` — refreshed (430 → 431 modules).

## Goal
- Add a CPU-only ECS system that recomputes world culling bounds from local bounds and world transforms after transform propagation.

## Non-goals
- No graphics frustum/occlusion culling implementation.
- No GPU buffer updates, RHI handles, renderer sidecars, or graphics imports in ECS.
- No collider/physics broadphase integration.
- No geometry-source population work; that belongs to `HARDEN-065`.

## Context
- Owner/layer: `ecs`; allowed dependencies are `core` and explicitly justified CPU-only `geometry` types.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- Promoted components already include `ECS.Component.Culling.Local`, `ECS.Component.Culling.World`, and `ECS.Component.Transform.WorldMatrix`.
- Runtime/render extraction can consume `WorldBoundingOBB` / `WorldBoundingSphere`, but no promoted ECS system currently keeps those fields in sync with transforms.

## Required changes
- [x] Define a `WorldBounds` propagation API/system under `src/ecs/Systems/` or a clearly named component helper module. (`Extrinsic.ECS.System.BoundsPropagation` at `src/ecs/Systems/ECS.System.BoundsPropagation.{cppm,cpp}`.)
- [x] Recompute `Culling::World::Bounds` from `Culling::Local::Bounds` and `Transform::WorldMatrix` for entities with valid inputs.
- [x] Decide whether propagation is driven by `Transform::WorldUpdatedTag`, local-bounds dirty tags, or a full scan; document the selected policy. (Driven by `WorldUpdatedTag`; documented in `src/ecs/Systems/README.md`.)
- [x] Add finite-value and missing-component diagnostics or counters that remain CPU-only. (`BoundsPropagation::Stats` overload `OnUpdate(registry&, Stats&)`.)
- [x] Register the system with `Core::FrameGraph` if it is a scheduled ECS system, ordering it after `TransformHierarchy::PassName`. (`RegisterSystem` declares `WaitFor("TransformUpdate")` and `Signal("WorldBoundsUpdate")`.)

### Prerequisite refactor (committed alongside this task)
- [x] Move `Culling::Bounds` in `ECS.Component.Culling.Local`/`ECS.Component.Culling.World` into `Culling::Local::Bounds` and `Culling::World::Bounds` subnamespaces. The original namespace shared the unqualified `Bounds` name across both modules, which prevented any TU from importing both modules together — including the new propagation system. Updated the single consumer in `Runtime.RenderExtraction.cpp`.

## Tests
- [x] Add `tests/unit/ecs/Test.ECS.BoundsPropagation.cpp` covering local AABB/sphere to world OBB/sphere recompute.
- [x] Cover translation, rotation, non-uniform scale policy, missing local bounds, missing world matrix, clean/dirty selection policy, and finite-value diagnostics.
- [x] If a FrameGraph registration helper is added, cover ordering after `TransformHierarchy`. (Verified via the layered execution plan returned by `FrameGraph::GetExecutionLayers()`.)
- [x] Keep tests CPU-only and labeled `unit;ecs`.

## Docs
- [x] Update `src/ecs/Systems/README.md` with bounds propagation ordering and tag policy.
- [x] Update `src/ecs/README.md` if the public module surface changes.
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if this changes ECS readiness or retirement blockers.
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved.

## Acceptance criteria
- [x] ECS can keep world culling bounds synchronized with promoted transform hierarchy updates without importing graphics/runtime/RHI.
- [x] Bounds propagation ordering and dirty/clean behavior are documented and tested.
- [x] Runtime/render extraction can treat world bounds as current after the promoted ECS fixed-step bundle has run. (Activation by a runtime fixed-step bundle remains deferred to `RUNTIME-091`; this task is restricted to providing the system and its registration helper.)

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Importing graphics, RHI, runtime, platform, or app modules into ECS.
- Adding graphics culling, GPU residency, or broadphase physics state to canonical ECS.
- Inferring collider/physics bounds behavior from render/culling bounds.
- Combining this with geometry-source population or render-sync policy work.
