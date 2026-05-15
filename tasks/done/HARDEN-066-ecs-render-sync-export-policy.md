# HARDEN-066 — Define ECS render-sync/export dirty-tag policy

## Status

- Status: done.
- Owner/agent: Claude on `claude/setup-agentic-workflow-sI5wG`.
- Branch: `claude/setup-agentic-workflow-sI5wG`.
- Started: 2026-05-15.
- Completed: 2026-05-15.
- Implementation commit: pending merge of branch `claude/setup-agentic-workflow-sI5wG`.
- Landed slice: implemented `Extrinsic.ECS.System.RenderSync` as a CPU-only tag-forwarding pass that translates `Components::Transform::WorldUpdatedTag` into `Components::DirtyTags::DirtyTransform` and clears `WorldUpdatedTag`; wired it into `Extrinsic.Runtime.EcsSystemBundle::RegisterPromotedEcsSystemBundle` after `BoundsPropagation`; added unit coverage in `tests/unit/ecs/Test.ECS.RenderSync.cpp`; reconciled `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` to expect three registered passes and the new `DirtyTransform`/`WorldUpdatedTag` end-state; refreshed `src/ecs/README.md`, `src/ecs/Systems/README.md`, `src/runtime/Runtime.EcsSystemBundle.cppm`, and `docs/migration/nonlegacy-parity-matrix.md`.

## Decision

- `Extrinsic.ECS.System.RenderSync` becomes a **CPU-only tag-forwarding pass**, not a retired placeholder.
- **Stamps**: `RenderSync` stamps `Components::DirtyTags::DirtyTransform` from `Components::Transform::WorldUpdatedTag` via `emplace_or_replace`.
- **Clears**: `RenderSync` clears `Components::Transform::WorldUpdatedTag` (bulk `clear<T>()` after the forwarding loop) so the producer/consumer cycle is closed within the ECS layer. `Runtime.RenderExtraction` continues to clear `Components::DirtyTags::DirtyTransform` per entity it processes.
- **Ordering**: `TransformHierarchy` (producer of `WorldUpdatedTag`) → `BoundsPropagation` (reads `WorldUpdatedTag` without clearing) → `RenderSync` (writes `WorldUpdatedTag`/`DirtyTransform`). Encoded via `WaitFor("TransformUpdate")` and `WaitFor("WorldBoundsUpdate")` on the `RenderSync` pass.
- **Layer cleanliness**: `Extrinsic.ECS.System.RenderSync` imports only `Extrinsic.Core.FrameGraph`, `Extrinsic.Core.Hash`, the relevant `Extrinsic.ECS.Component.*` modules, and the sibling system modules whose `PassName` constants it waits on. No graphics/RHI/runtime/platform/app imports.
- **Rationale**: keeps both tag lifecycles owned by ECS (the layer that defines them); preserves the existing `Runtime.RenderExtraction` contract that drains `DirtyTransform` per entity; gives any future downstream consumer a single ECS-side seam to subscribe to "world transform changed" without needing to coordinate with `WorldUpdatedTag`'s producer/consumer ordering.

## Goal
- Decide whether `Extrinsic.ECS.System.RenderSync` becomes a CPU-only ECS export/tag-forwarding seam or is formally retired, and define the `WorldUpdatedTag` to `DirtyTransform` handoff policy.

## Non-goals
- No graphics/RHI uploads, GPU residency allocation, renderer sidecars, or bindless/resource handles in ECS.
- No changes to runtime render extraction beyond the minimum needed to consume the selected ECS-side contract.
- No scene serialization or physics work.

## Context
- Owner/layer: `ecs` for CPU-only component/tag policy, with runtime as the consumer/clearing owner when graphics sidecars are involved.
- Source review: [`docs/reviews/2026-05-13-src-ecs-gap-analysis.md`](../../../docs/reviews/2026-05-13-src-ecs-gap-analysis.md).
- `Extrinsic.ECS.System.RenderSync` previously existed as an empty placeholder; the activated bundle (`RUNTIME-091`) registered only `TransformHierarchy` and `BoundsPropagation`.
- `Extrinsic.ECS.System.TransformHierarchy` emits `Components::Transform::WorldUpdatedTag` and deliberately does not stamp `Components::DirtyTags::DirtyTransform`.
- `Runtime.RenderExtraction` consumes ECS/renderable state and owns graphics sidecars, so any ECS export policy must remain CPU-only and data-oriented.

## Required changes
- [x] Decide and document one of the following: implement `RenderSync` as a CPU-only ECS pass, replace it with narrower helper functions, or retire/remove the placeholder through a separate mechanical cleanup task. (`RenderSync` is implemented as a CPU-only tag-forwarding pass; see Decision above.)
- [x] Define exactly who stamps `DirtyTags::DirtyTransform` from `Transform::WorldUpdatedTag` and who clears both tags. (`RenderSync` stamps `DirtyTransform` and clears `WorldUpdatedTag`; `Runtime.RenderExtraction` continues to clear `DirtyTransform` per entity it processes.)
- [x] If `RenderSync` remains, implement only CPU-only aggregation/tag-forwarding over ECS components and expose a minimal tested API. (`Extrinsic.ECS.Systems.RenderSync::OnUpdate(registry)` and `OnUpdate(registry, Stats&)` plus `RegisterSystem(FrameGraph&, registry&)` are now exported with the documented diagnostics counters.)
- [x] If runtime owns the entire handoff, update docs to state that `RenderSync` is intentionally absent/retired and runtime extraction interprets `WorldUpdatedTag` directly. (Not selected; ECS retains tag ownership.)
- [x] Add structural safeguards if new APIs are added so ECS still cannot import graphics/RHI/runtime/platform/app modules. (`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` already enforces the prohibited-import set across every file under `src/ecs`, so the new module is automatically covered.)

## Tests
- [x] Add `tests/unit/ecs/Test.ECS.RenderSync.cpp` for the new ECS pass. (Six cases cover forwarding, no-op behavior, idempotent re-stamping, leaving unrelated `DirtyTransform` alone, multi-entity stat accumulation, and a FrameGraph pipeline that asserts the pass runs after `TransformHierarchy` + `BoundsPropagation`.)
- [x] Cover dirty-transform stamping, consumer-cleared tags, clean entities, and entities without world matrices. (Covered by the cases above plus the no-`WorldUpdatedTag` no-op case which exercises the "no world matrix update happened" branch.)
- [x] If runtime directly consumes `WorldUpdatedTag`, add/update runtime integration tests proving extraction observes transform updates and clears only runtime-owned state. (Not applicable; runtime extraction continues to consume `DirtyTransform`. The existing `tests/contract/runtime/Test.RuntimeEcsSystemBundle.cpp` cases were updated to assert `WorldUpdatedTag` is cleared and `DirtyTransform` is stamped after the bundle runs.)
- [x] Keep ECS tests CPU-only and labeled `unit;ecs`; runtime tests should use existing `runtime` labels. (`Test.ECS.RenderSync.cpp` rides in the existing `IntrinsicECSTests` umbrella with labels `unit ecs`; `Test.RuntimeEcsSystemBundle.cpp` rides in the existing `IntrinsicRuntimeContractTests` umbrella with labels `contract runtime`.)

## Docs
- [x] Update `src/ecs/Systems/README.md` with the factual `RenderSync` decision. (The "Render sync boundary" section now describes the CPU-only tag-forwarding policy, the API surface, and the FrameGraph dependency edges.)
- [x] Update `src/ecs/README.md` if the public system module surface changes. (Module list unchanged; the dependency note now describes `RenderSync`'s tag-forwarding role explicitly.)
- [x] Update [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) if the render-sync retirement blocker changes. (The `ecs` row now reflects that `DirtyTags::DirtyTransform` is forwarded by `RenderSync` per `HARDEN-066`, and that the bundle covers `TransformHierarchy` + `BoundsPropagation` + `RenderSync`.)
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../../docs/api/generated/module_inventory.md) if modules are added, removed, renamed, or moved. (No module surface change — the `Extrinsic.ECS.System.RenderSync` interface module name is unchanged; only its export/implementation contents grew.)

## Acceptance criteria
- [x] The repository has one documented owner for transform GPU-sync dirty handoff from ECS world updates to runtime/graphics extraction. (`Extrinsic.ECS.Systems.RenderSync::OnUpdate` is the single owner; documented in `src/ecs/Systems/README.md` and `docs/migration/nonlegacy-parity-matrix.md`.)
- [x] `RenderSync` is either a tested CPU-only ECS seam or has an explicit retirement/removal follow-up. (Implemented as a tested CPU-only ECS seam.)
- [x] No graphics/RHI/runtime/platform/app dependency is introduced into `src/ecs`. (`tests/contract/ecs/Test.ECS.LayeringBoundaries.cpp` continues to scan every `src/ecs` file for forbidden imports.)

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
