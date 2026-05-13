# GRAPHICS-031B — Default debug surface substitution and diagnostics counters

## Goal
- Wire the graphics-owned snapshot-consumption substitution at the renderer span-copy step (`Graphics.Renderer.cpp:355..415` window) so that any unset / out-of-range material slot is replaced with `kDefaultMaterialSlotIndex` (slot 0 = `Material.DefaultDebugSurface`), and append the three additive `MaterialSystemDiagnostics` counters: `MissingMaterialFallbackCount`, `InvalidMaterialSlotCount`, `DefaultDebugSurfaceUses`.

## Non-goals
- No new debug-material variant (`GRAPHICS-031C` is optional follow-up).
- No frame-recipe change (`GRAPHICS-032A`).
- No new pipelines (the slot-0 pipeline already exists from `GRAPHICS-031A`).
- No change to the existing `MaterialSystemDiagnostics::FallbackSlotResolveCount` counter (it tracks the separate `GetMaterialSlot()` stale-handle path).

## Context
- Status: in-progress.
- Owner/agent: Claude on branch `claude/setup-agentic-workflow-JFf7h`.
- Owner/layer: `graphics/renderer`.
- Planning parent: [`tasks/done/GRAPHICS-031-default-debug-surface-material.md`](../../done/GRAPHICS-031-default-debug-surface-material.md), Decisions 7 (substitution path-(b)), 8 (reset cadence), 9 (diagnostics). Recorded as Impl-B in the parent's Required changes.
- Upstream gates: `GRAPHICS-031A` (slot 0 must be the default-debug-surface).

## Required changes
- [x] In the renderer's snapshot-copy step (between `SubmitRuntimeSnapshots` and `PrepareFrame`), iterate the renderable snapshot batch and replace material slot:
  - if the runtime field is the sentinel "unset" value → substitute slot 0, increment `MissingMaterialFallbackCount`.
  - if the slot integer is out-of-range (≥ `MaterialSystem::GetCapacity()`) → substitute slot 0, increment `InvalidMaterialSlotCount`.
  - independent of branch, total per-frame uses of slot 0 (after substitution) increments `DefaultDebugSurfaceUses`.
- [x] Add the three counters to `MaterialSystemDiagnostics` (alongside the existing `FallbackSlotResolveCount`).
- [x] Reset all three counters at the cadence locked by Decision 8 (per-frame at `BeginFrame()` or in `ResetFrameState()`, mirroring existing `MaterialSystemDiagnostics` reset rhythm). Implementation resets in both `SubmitRuntimeSnapshots()` (next to `m_InvalidSnapshotRecordCount = 0`) and `ResetFrameState()` to match the existing `InvalidSnapshotRecordCount` reset rhythm.
- [ ] Optional: add a runtime-side authoring shorthand path so a renderable submitted with no material descriptor reaches the renderer with the sentinel "unset" runtime field (Decision 7 path-(a)). *(Deferred — not exercised by current runtime extraction, which always writes `HasMaterialSlot = true`. Renderer substitution still triggers if a future extraction path leaves the field unset.)*

## Tests
- [x] `contract;graphics` test: a renderable submitted with sentinel-unset material renders with slot 0 and `MissingMaterialFallbackCount` increments by 1. (`tests/contract/graphics/Test.MaterialSubstitutionContract.cpp::SentinelUnsetSubstitutesDefaultSlotAndIncrementsMissingCounter`)
- [x] `contract;graphics` test: a renderable submitted with an out-of-range slot integer renders with slot 0 and `InvalidMaterialSlotCount` increments by 1. (`OutOfRangeSlotSubstitutesDefaultAndIncrementsInvalidCounter`)
- [x] `contract;graphics` test: `DefaultDebugSurfaceUses == (authored-default uses + MissingMaterialFallbackCount + InvalidMaterialSlotCount)` per frame. (`DefaultDebugSurfaceUsesEqualsAuthoredDefaultPlusFallbacks`)
- [x] `contract;graphics` test: pipeline state and `kDefaultMaterialSlotIndex` survive `RebuildGpuResources()` without identity churn. (`DefaultSlotIdentitySurvivesRebuildGpuResources`; covers slot 0 params + DefaultDebugSurface type ID and name byte-equality across the rebuild.)
- [ ] `contract;runtime` test (Decision 7 path-(a)): an entity with geometry + no material descriptor reaches the renderer with the sentinel "unset" runtime field. *(Deferred together with the optional runtime authoring shorthand above.)*
- [x] Additional `PerFrameCountersResetAtBeginFrame` test asserts the three counters are zeroed at `BeginFrame()` (Decision 8 reset cadence).

## Docs
- [x] Update `src/graphics/renderer/README.md` to enumerate the three new counters next to `FallbackSlotResolveCount`, point at the substitution call site, and link the now-active GRAPHICS-031B record.

## Acceptance criteria
- [x] All three counters reach the asserted values across the test scenarios.
- [x] No silent skips: every renderable that reaches the renderer either uses an authored-valid slot or substitutes to slot 0 deterministically.
- [x] No regression in `RebuildGpuResources()` identity assertions.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests IntrinsicRuntimeTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding lighting / texture sampling to the substitution path.
- Mutating runtime ECS state from graphics.
- Removing or repurposing the existing `FallbackSlotResolveCount` counter.

## Next verification step
- Local `cmake --preset ci` requires `clang-20`; the current container ships
  only `clang-18`, so the focused
  `IntrinsicGraphicsContractTests` / `ctest -L contract` gate from the
  Verification block above must be re-run on a CI host that satisfies the
  preset's compiler pin before retiring this task. Structural checks
  (`check_layering`, `check_task_policy`, `check_doc_links`,
  `check_test_layout`) all pass locally.
