# HARDEN-067 — Add ECS world-bounds propagation system

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
- [ ] Define a `WorldBounds` propagation API/system under `src/ecs/Systems/` or a clearly named component helper module.
- [ ] Recompute `Culling::World::Bounds` from `Culling::Local::Bounds` and `Transform::WorldMatrix` for entities with valid inputs.
- [ ] Decide whether propagation is driven by `Transform::WorldUpdatedTag`, local-bounds dirty tags, or a full scan; document the selected policy.
- [ ] Add finite-value and missing-component diagnostics or counters that remain CPU-only.
- [ ] Register the system with `Core::FrameGraph` if it is a scheduled ECS system, ordering it after `TransformHierarchy::PassName`.

## Tests
- [ ] Add `tests/unit/ecs/Test.ECS.BoundsPropagation.cpp` covering local AABB/sphere to world OBB/sphere recompute.
- [ ] Cover translation, rotation, non-uniform scale policy, missing local bounds, missing world matrix, clean/dirty selection policy, and finite-value diagnostics.
- [ ] If a FrameGraph registration helper is added, cover ordering after `TransformHierarchy`.
- [ ] Keep tests CPU-only and labeled `unit;ecs`.

## Docs
- [ ] Update `src/ecs/Systems/README.md` with bounds propagation ordering and tag policy.
- [ ] Update `src/ecs/README.md` if the public module surface changes.
- [ ] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if this changes ECS readiness or retirement blockers.
- [ ] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved.

## Acceptance criteria
- [ ] ECS can keep world culling bounds synchronized with promoted transform hierarchy updates without importing graphics/runtime/RHI.
- [ ] Bounds propagation ordering and dirty/clean behavior are documented and tested.
- [ ] Runtime/render extraction can treat world bounds as current after the promoted ECS fixed-step bundle has run.

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
