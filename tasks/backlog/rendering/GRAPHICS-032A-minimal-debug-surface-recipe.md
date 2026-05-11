# GRAPHICS-032A — `FrameRecipe::MinimalDebugSurface` recipe and registration

## Goal
- Add the second frame recipe declared by `GRAPHICS-032`: `FrameRecipe::MinimalDebugSurface` with stable label `"recipe.minimal-debug-surface"`. The recipe declares exactly two passes (`Pass.Surface.MinimalDebug` then `Pass.Present.MinimalDebug`), exposes transient `SceneColorHDR` and `SceneDepth`, finalizes the imported `Backbuffer` through the existing fullscreen-triangle present contract, and exposes the three diagnostic counters (`MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount`).

## Non-goals
- No CPU-mock command body for `Pass.Surface.MinimalDebug` (that is `GRAPHICS-032B`).
- No CPU-mock command body for `Pass.Present.MinimalDebug` and no end-to-end CPU acceptance test (that is `GRAPHICS-032C`).
- No `gpu;vulkan` smoke (that is `GRAPHICS-032D`).
- No change to `BuildDefaultFrameRecipe` semantics; the minimal recipe is a separate opt-in contract.

## Context
- Status: not started.
- Owner/layer: `graphics/framegraph` for the recipe declaration, `graphics/renderer` for the diagnostics counters.
- Planning parent: [`tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md`](../../done/GRAPHICS-032-minimal-surface-present-command-path.md), Recorded as Impl-A in the parent's Required changes.
- Upstream gates: `GRAPHICS-031A` (slot-0 default-debug pipeline must exist).
- Documentation cross-references already locked the contract in `src/graphics/framegraph/README.md:23–27` and `src/graphics/renderer/README.md:116–130`.

## Required changes
- [ ] Add `BuildMinimalDebugSurfaceRecipe(graph, imports, sizing) -> FrameRecipeBuildResult` to `Extrinsic.Graphics.FrameRecipe` declaring two passes:
  - `Pass.Surface.MinimalDebug`: writes `SceneColorHDR` + `SceneDepth`, reads `MaterialBuffer` SSBO + scene-table BDA, consumes `Cull.SurfaceOpaque.IndexedArgs` / `Count`. Does not declare any pre-existing default-recipe pass.
  - `Pass.Present.MinimalDebug`: writes the imported `Backbuffer`, reads `SceneColorHDR`. Finalization is fullscreen-triangle (`Draw(3, 1, 0, 0)`).
- [ ] Add the stable label `"recipe.minimal-debug-surface"` and ensure recipe-vs-default isolation (the default recipe must not declare or share these passes; the minimal recipe must not declare any default-recipe pass).
- [ ] Add `RenderGraphFrameStats::MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, and `MinimalRecipeMissingPrerequisiteCount` counters.
- [ ] Reset the new counters at the existing `ResetFrameState()` cadence.
- [ ] Add a renderer entry point or recipe-selector hook so callers can opt into the minimal recipe via `RenderConfig` (e.g. `RenderConfig::FrameRecipe = FrameRecipeKind::Default | FrameRecipeKind::MinimalDebug`). Default stays `Default`.

## Tests
- [ ] `contract;graphics` test: `BuildMinimalDebugSurfaceRecipe` introspection asserts the two-pass declaration in order, the resource set (`SceneColorHDR`, `SceneDepth`, imported `Backbuffer`, `MaterialBuffer`, scene-table BDA, `Cull.SurfaceOpaque.*`), the stable label, and that no default-recipe pass name appears.
- [ ] `contract;graphics` test: with the recipe selector toggled to `MinimalDebug`, `BuildDefaultFrameRecipe` is **not** invoked; with the selector on `Default`, the minimal-recipe passes are not declared.
- [ ] `contract;graphics` test: `MinimalRecipeMissingPrerequisiteCount` increments when material/pipeline residency is missing for the surface pass even though the recipe still compiles.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/graphics/framegraph/README.md` to flip the planned `MinimalDebugSurface` recipe row to current state.
- [ ] Update `src/graphics/renderer/README.md` to enumerate the three new diagnostics counters.

## Acceptance criteria
- [ ] `BuildMinimalDebugSurfaceRecipe` compiles through the `ci` preset and passes recipe-introspection contract tests.
- [ ] Default recipe behavior is unchanged; the minimal recipe is opt-in.
- [ ] No `Pass.Surface.MinimalDebug` / `Pass.Present.MinimalDebug` command body lands in this slice.

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

## Next verification step
- Land the recipe + counters + selector hook, exercise the introspection contract tests, run the verification commands above.
