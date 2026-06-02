# GRAPHICS-032A — `FrameRecipe::MinimalDebugSurface` recipe and registration

## Goal
- Add the second frame recipe declared by `GRAPHICS-032`: `FrameRecipe::MinimalDebugSurface` with stable label `"recipe.minimal-debug-surface"`. The recipe declares exactly two passes (`Pass.Surface.MinimalDebug` then `Pass.Present.MinimalDebug`), exposes transient `SceneColorHDR` and `SceneDepth`, finalizes the imported `Backbuffer` through the existing fullscreen-triangle present contract, and exposes the three diagnostic counters (`MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount`).

> **Scaffold notice.** Everything authored by this task is bootstrap-only and is removed by [`GRAPHICS-081`](../backlog/rendering/GRAPHICS-081-retire-minimal-debug-recipe-scaffold.md) once the canonical default recipe records every pass body operationally (`GRAPHICS-070..076`). Do not build long-lived call sites on `BuildMinimalDebugSurfaceRecipe`, `FrameRecipeKind::MinimalDebug`, the `MinimalDebugSurface`/`MinimalDebugPresent` pass classes, or the three minimal-recipe diagnostics counters.

## Non-goals
- No CPU-mock command body for `Pass.Surface.MinimalDebug` (that is `GRAPHICS-032B`).
- No CPU-mock command body for `Pass.Present.MinimalDebug` and no end-to-end CPU acceptance test (that is `GRAPHICS-032C`).
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No change to `BuildDefaultFrameRecipe` semantics; the minimal recipe is a separate opt-in contract.

## Context
- Status: done.
- Owner/agent: Claude on branch `claude/setup-agentic-workflow-JrJnr`.
- Owner/layer: `graphics/framegraph` for the recipe declaration, `graphics/renderer` for the diagnostics counters.
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-A in the parent's Required changes.
- Upstream gates: `GRAPHICS-031A` (slot-0 default-debug pipeline must exist) — done.
- Documentation cross-references already locked the contract in `src/graphics/framegraph/README.md:23–27` and `src/graphics/renderer/README.md:116–130`.

## Required changes
- [x] Add `BuildMinimalDebugSurfaceRecipe(graph, imports, sizing) -> FrameRecipeBuildResult` to `Extrinsic.Graphics.FrameRecipe` declaring two passes:
  - `Pass.Surface.MinimalDebug`: writes `SceneColorHDR` + `SceneDepth`, reads `MaterialBuffer` SSBO + scene-table BDA, consumes `Cull.SurfaceOpaque.IndexedArgs` / `Count`. Does not declare any pre-existing default-recipe pass.
  - `Pass.Present.MinimalDebug`: writes the imported `Backbuffer`, reads `SceneColorHDR`. Finalization is fullscreen-triangle (`Draw(3, 1, 0, 0)`).
- [x] Add the stable label `"recipe.minimal-debug-surface"` and ensure recipe-vs-default isolation (the default recipe must not declare or share these passes; the minimal recipe must not declare any default-recipe pass). Implemented as `kMinimalDebugSurfaceRecipeLabel` / `kMinimalDebugSurfacePassName` / `kMinimalDebugPresentPassName` `std::string_view` constants exported from `Extrinsic.Graphics.FrameRecipe`.
- [x] Add `RenderGraphFrameStats::MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, and `MinimalRecipeMissingPrerequisiteCount` counters.
- [x] Reset the new counters at the existing per-frame `m_LastRenderGraphStats = {}` cadence in `ResetFrameState()` (BeginFrame) and at `ExecuteFrame()` entry, mirroring the existing reset rhythm for `Compile`/`Execute`/`CommandRecords`.
- [x] Add a renderer entry point or recipe-selector hook so callers can opt into the minimal recipe via `RenderConfig`. Implemented as `Core::Config::FrameRecipeKind { Default, MinimalDebug }` on `RenderConfig::FrameRecipe`, plus the `IRenderer::SetFrameRecipe()` / `GetFrameRecipe()` virtuals; `Graphics.Renderer.cpp::ExecuteFrame` dispatches to `BuildMinimalDebugSurfaceRecipe` when the renderer's recipe kind is `MinimalDebug`. Default stays `Default`; runtime callers translate `RenderConfig::FrameRecipe` into the setter (full runtime wiring tracked in a follow-up slice).

## Tests
- [x] `contract;graphics` test: `BuildMinimalDebugSurfaceRecipe` introspection asserts the two-pass declaration in order, the resource set (`SceneColorHDR`, `SceneDepth`, imported `Backbuffer`, `Material.Buffer`, `GpuWorld.SceneTable`, `Cull.SurfaceOpaque.*`), the stable label, and that no default-recipe pass name appears. (`MinimalDebugSurfaceRecipeDeclaresTwoPassesInOrderWithStableLabels` in `tests/contract/graphics/Test.FrameRecipeContract.cpp`)
- [x] `contract;graphics` test: minimal- and default-recipe introspections are mutually isolated — neither declares the other's pass labels. (`MinimalAndDefaultRecipesAreMutuallyIsolated`)
- [x] `contract;graphics` test: `MinimalRecipeMissingPrerequisiteCount` increments when material/pipeline residency is missing for the surface pass even though the recipe still compiles. Implemented via `FrameRecipeBuildResult::MissingPrerequisiteCount` (mirrored into the renderer counter), counting material, surface-opaque bucket (joint args/count), and scene-table residency gaps. (`MinimalDebugSurfaceRecipeCountsMissingPrerequisites`)
- [x] `contract;graphics` test: missing backbuffer hard-fails the recipe build with a diagnostic (mirrors `DefaultFrameRecipe`). (`MinimalDebugSurfaceRecipeRequiresValidBackbuffer`)
- Out of scope for this task: no `gpu`/`vulkan` test in this slice (covered by GRAPHICS-032D).

## Docs
- [x] Update `src/graphics/framegraph/README.md` to flip the planned `MinimalDebugSurface` recipe row to current state and point at the build entry point.
- [x] Update `src/graphics/renderer/README.md` to enumerate the three new diagnostics counters, the `RenderConfig::FrameRecipe` selector, and the increment site.

## Acceptance criteria
- [x] `BuildMinimalDebugSurfaceRecipe` compiles through the `ci` preset and passes recipe-introspection contract tests. *(Pending CI host: local container ships clang-18 only; preset pins clang-20. Structural checks pass locally; contract-test compilation and ctest run scheduled on remote CI.)*
- [x] Default recipe behavior is unchanged; the minimal recipe is opt-in via `IRenderer::SetFrameRecipe`.
- [x] No `Pass.Surface.MinimalDebug` / `Pass.Present.MinimalDebug` command body lands in this slice. (Recording bodies remain in GRAPHICS-032B/C; the renderer's `ExecuteFrame` dispatches unknown pass names through the existing `SkippedNonOperational`/`SkippedUnavailable` soft-skip path.)

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding command-recording bodies for the new passes.
- Mutating `BuildDefaultFrameRecipe` semantics.
- Adding a `gpu;vulkan` test.
- Mixing mechanical file moves with semantic refactors.

## Completion
- Completed: 2026-05-13.
- Commit reference: `3931705` ("GRAPHICS-032A Wire minimal-debug-surface recipe and diagnostics") via PR #819 from `claude/setup-agentic-workflow-JrJnr`, merged to `main` at 2026-05-13T13:05:23Z.
- Verification:
  - Project CI ran on PR #819 (`ci` preset, clang-20 toolchain) and passed before merge to `main`.
  - Authoring session ran the structural checks locally; the focused `cmake --preset ci` / `ctest -L contract` gate ran in the PR's CI environment because the authoring container shipped clang-18 only.
