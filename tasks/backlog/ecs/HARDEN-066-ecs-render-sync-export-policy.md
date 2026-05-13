# HARDEN-066 — Define ECS render-sync/export dirty-tag policy

## Goal
- Decide whether `Extrinsic.ECS.System.RenderSync` becomes a CPU-only ECS export/tag-forwarding seam or is formally retired, and define the `WorldUpdatedTag` to `DirtyTransform` handoff policy.

## Non-goals
- No graphics/RHI uploads, GPU residency allocation, renderer sidecars, or bindless/resource handles in ECS.
- No changes to runtime render extraction beyond the minimum needed to consume the selected ECS-side contract.
- No scene serialization or physics work.

## Context
- Owner/layer: `ecs` for CPU-only component/tag policy, with runtime as the consumer/clearing owner when graphics sidecars are involved.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- `Extrinsic.ECS.System.RenderSync` currently exists as an empty placeholder.
- `Extrinsic.ECS.System.TransformHierarchy` emits `Components::Transform::WorldUpdatedTag` and deliberately does not stamp `Components::DirtyTags::DirtyTransform`.
- `Runtime.RenderExtraction` currently consumes ECS/renderable state and owns graphics sidecars, so any ECS export policy must remain CPU-only and data-oriented.

## Required changes
- [ ] Decide and document one of the following: implement `RenderSync` as a CPU-only ECS pass, replace it with narrower helper functions, or retire/remove the placeholder through a separate mechanical cleanup task.
- [ ] Define exactly who stamps `DirtyTags::DirtyTransform` from `Transform::WorldUpdatedTag` and who clears both tags.
- [ ] If `RenderSync` remains, implement only CPU-only aggregation/tag-forwarding over ECS components and expose a minimal tested API.
- [ ] If runtime owns the entire handoff, update docs to state that `RenderSync` is intentionally absent/retired and runtime extraction interprets `WorldUpdatedTag` directly.
- [ ] Add structural safeguards if new APIs are added so ECS still cannot import graphics/RHI/runtime/platform/app modules.

## Tests
- [ ] Add `tests/unit/ecs/Test.ECS.RenderSync.cpp` if an ECS pass/helper is implemented.
- [ ] Cover dirty-transform stamping, consumer-cleared tags, clean entities, and entities without world matrices.
- [ ] If runtime directly consumes `WorldUpdatedTag`, add/update runtime integration tests proving extraction observes transform updates and clears only runtime-owned state.
- [ ] Keep ECS tests CPU-only and labeled `unit;ecs`; runtime tests should use existing `runtime` labels.

## Docs
- [ ] Update `src/ecs/Systems/README.md` with the factual `RenderSync` decision.
- [ ] Update `src/ecs/README.md` if the public system module surface changes.
- [ ] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if the render-sync retirement blocker changes.
- [ ] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved.

## Acceptance criteria
- [ ] The repository has one documented owner for transform GPU-sync dirty handoff from ECS world updates to runtime/graphics extraction.
- [ ] `RenderSync` is either a tested CPU-only ECS seam or has an explicit retirement/removal follow-up.
- [ ] No graphics/RHI/runtime/platform/app dependency is introduced into `src/ecs`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci -L 'ecs|runtime|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU handles, bindless indices, graphics components, runtime sidecars, or renderer API calls to canonical ECS systems/components.
- Making ECS responsible for graphics residency, uploads, or render extraction.
- Deleting legacy or placeholder modules without a dedicated mechanical cleanup step if removal is selected.
