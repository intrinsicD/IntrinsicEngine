# GRAPHICS-081 — Retire `FrameRecipe::MinimalDebugSurface` scaffold once default recipe is operational

## Status

- Status: done (`Retired`).
- Owner/layer: `graphics/framegraph`, `graphics/renderer`, `graphics/vulkan`, tests, docs, and task records.
- Branch: `main` working tree.
- Completed: 2026-06-02.
- Commit/PR: pending current change.
- Maturity: `Retired`; the bootstrap recipe path is deleted, not shimmed.
- Host note: local `ci-vulkan` default-recipe smoke selection succeeded but skipped on this host because no operational Vulkan/GLFW lane was available. The default-recipe smoke/readback fixture remains in-tree and prior capable-host evidence is recorded in `GRAPHICS-076E/F`.

## Goal

- Remove the bootstrap `MinimalDebugSurface` recipe, its pass classes, recipe selector, diagnostics counters, shaders, dedicated tests, CMake entries, and module-inventory rows after the canonical default recipe became the visible-triangle path.

## Non-goals

- No mutation of the default-recipe pass set, resource declarations, or barrier sequencing.
- No removal of `Material.DefaultDebugSurface` slot 0.
- No removal of the reference scene, procedural geometry residency bridge, or Vulkan operational gate.
- No reduction of `gpu;vulkan` smoke coverage; the default-recipe visible-triangle fixture remains the canonical opt-in coverage.

## Context

- Upstream gates retired before this task: `GRAPHICS-031A/B`, `GRAPHICS-070..076`, `GRAPHICS-076E/F`, and `GRAPHICS-080`.
- `GRAPHICS-032A/B/C/D` and `GRAPHICS-033C/D` authored the bootstrap visible-triangle scaffold. Those tasks remain historical records under `tasks/done/`, but their code/test artifacts are no longer live.
- The append-only Vulkan reason taxonomy was preserved by renaming `MinimalRecipeRecordingMissing` to `DefaultRecipeRecordingMissing` rather than deleting the reason slot.

## Required changes

- [x] Removed `BuildMinimalDebugSurfaceRecipe(...)`, its exported labels, and the `RenderConfig::FrameRecipe` selector.
- [x] Deleted `Pass.Surface.MinimalDebug` and `Pass.Present.MinimalDebug` module interfaces/implementations.
- [x] Removed bootstrap pass members, executor routes, pipeline leases, helper functions, and diagnostics counters from `NullRenderer` / `IRenderer` stats.
- [x] Removed the bootstrap backbuffer readback hook/counter while preserving the default-recipe readback seam.
- [x] Removed deleted pass modules from `src/graphics/renderer/Passes/CMakeLists.txt`.
- [x] Deleted bootstrap shaders and minimal-recipe CPU/gpu test fixtures.
- [x] Renamed the Vulkan operational gate reason to `DefaultRecipeRecordingMissing` while preserving enum ordinal order.
- [x] Regenerated `docs/api/generated/module_inventory.md` after source-tree/module-surface deletion.

## Tests

- [x] Focused graphics contract target builds after the deletions.
- [x] Default-recipe tests remain in-tree, including `DefaultRecipeSurfaceGpuSmoke` for opt-in Vulkan visible-triangle coverage.
- [x] Negative grep confirms the retired bootstrap strings are absent from `src/`, `tests/`, `assets/`, and `cmake/`.
- [x] Local `ci-vulkan` default-recipe smoke selection ran and skipped deterministically on this non-operational host rather than selecting the deleted bootstrap smoke.

## Docs

- [x] Updated `src/graphics/framegraph/README.md`, `src/graphics/renderer/README.md`, and `src/graphics/vulkan/README.md` for current default-recipe state.
- [x] Updated `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, `docs/adr/0005-vulkan-operational-readiness-gate.md`, and `docs/roadmap.md` for scaffold retirement and reason rename.
- [x] Added review-note pointers in `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` and `docs/reviews/2026-05-30-sandbox-app-remaining-gates.md`.
- [x] Updated backlog DAGs and open follow-up tasks so future work targets the default recipe and does not reintroduce the retired scaffold.
- [x] Added `Retired by GRAPHICS-081` notes to the affected historical done tasks.

## Acceptance criteria

- [x] All bootstrap minimal-recipe artifacts are removed from `src/`, `tests/`, `assets/`, `cmake/`, and generated module inventory.
- [x] Default-recipe visible-triangle `gpu;vulkan` coverage remains present; this host selected but skipped the opt-in smoke due missing operational Vulkan/GLFW support.
- [x] Task records, architecture docs, Vulkan docs, renderer/framegraph docs, and backlog DAGs are synchronized.
- [x] `VulkanOperationalReason` remains append-only via reason rename rather than deletion.

## Verification

```bash
ctest --test-dir build/ci-vulkan --output-on-failure -R 'DefaultRecipeSurfaceGpuSmoke' -L 'gpu' -L 'vulkan' --timeout 120
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
! grep -RE 'MinimalDebugSurface|MinimalDebug|recipe\.minimal-debug-surface|BuildMinimalDebugSurfaceRecipe|MinimalSurfacePassExecutions|MinimalPresentPassExecutions|MinimalRecipeMissingPrerequisiteCount' src tests assets cmake
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes

- Mixing the retirement with new feature work or shader/material/recipe additions.
- Reducing `gpu;vulkan` smoke coverage below the default-recipe visible-triangle path.
- Deleting the default debug surface material, reference scene, procedural geometry bridge, or Vulkan operational gate.
- Bypassing the append-only Vulkan operational reason invariant.
