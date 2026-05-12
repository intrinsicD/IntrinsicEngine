# GRAPHICS-081 — Retire `FrameRecipe::MinimalDebugSurface` scaffold once default recipe is operational

## Goal
- Remove the `MinimalDebugSurface` recipe, its two pass classes (`Pass.Surface.MinimalDebug`, `Pass.Present.MinimalDebug`), the renderer's `MinimalDebugSurfacePass` / `MinimalDebugPresentPass` members and executor branches, the three minimal-recipe diagnostics counters, the `RenderConfig::FrameRecipe = FrameRecipeKind::MinimalDebug` selector option, and the dedicated tests once the canonical default frame recipe is fully operational and the reference triangle renders end-to-end through it. The MinimalDebug scaffold exists only to derisk the triangle path; it must not remain in the codebase after the default recipe replaces it.

## Non-goals
- No mutation of the default-recipe pass set, resource declarations, or barrier sequencing.
- No removal of GRAPHICS-031 (`Material.DefaultDebugSurface` at slot 0) — that material is the canonical missing-material fallback and stays.
- No removal of GRAPHICS-029A/B (reference scene + TriangleProvider) — the reference triangle stays as the canonical sandbox content.
- No removal of GRAPHICS-030A/B/C (procedural geometry residency bridge) — that is the canonical procedural-source path.
- No removal of GRAPHICS-033A/B (Vulkan operational-status evaluator + diagnostics) — those stay as the operational gate.
- No reduction of `gpu;vulkan` smoke coverage: the visible-triangle assertion must continue to pass against the default recipe before the minimal-recipe smoke is removed (covered by `GRAPHICS-081-prereq` test below).

## Context
- Status: not started; **blocked** on the full default-recipe pass family landing operationally.
- Owner/layer: `graphics/framegraph` (recipe removal), `graphics/renderer` (renderer members + executor routes + counters), `graphics/renderer/Passes` (pass class deletions), `tests/` (test deletions/migrations), `docs/` (review trail).
- Planning parents: `tasks/done/GRAPHICS-032-minimal-surface-present-command-path.md` (the recipe contract being removed), `tasks/done/GRAPHICS-033-vulkan-operational-readiness-and-diagnostics.md` (operational gate inputs).
- Upstream gates (all must be retired in `tasks/done/`):
  - `GRAPHICS-031A` + `GRAPHICS-031B` — default debug surface material & substitution (the canonical missing-material fallback is wired).
  - `GRAPHICS-070` — `Pass.Forward.Surface` operationally wired.
  - `GRAPHICS-071` — `Pass.Forward.Line` + `Pass.Forward.Point` operationally wired.
  - `GRAPHICS-072` — Deferred GBuffer + lighting operationally wired.
  - `GRAPHICS-073` — `Pass.Shadows` + shadow atlas operationally wired.
  - `GRAPHICS-074` — Selection ID + outline + picking-readback drain operationally wired.
  - `GRAPHICS-075` — Postprocess chain operationally wired.
  - `GRAPHICS-076` — `Pass.DebugView` + canonical `Pass.Present` operationally wired.
- Why retire: the MinimalDebug recipe shares no resources, passes, or diagnostics with the default recipe (per `GRAPHICS-032` Decision: recipe-vs-default isolation). Once the default recipe records all canonical pass bodies and produces a visible triangle on a Vulkan-capable host, the MinimalDebug scaffold becomes dead code that:
  - duplicates code paths in the executor lambda,
  - confuses agents about which recipe is canonical,
  - adds CMake module-graph nodes and SPIR-V build dependencies that are no longer used,
  - keeps test fixtures alive that test scaffolding rather than product.

## Prerequisite: the default-recipe equivalent of the GRAPHICS-033D smoke must already be green

Before this task can begin its deletions, the `gpu;vulkan` visible-triangle assertion from `GRAPHICS-033D` must be **ported to the default recipe** (or a sibling fixture authored alongside it) and reach the same pixel-readback assertions. That port is part of the upstream gate `GRAPHICS-076` (canonical `Pass.Present` operationally wired). This task verifies the port exists and is passing as its first action; it does not author the port itself.

## Companion intermediate-solution audit

This task is the largest scaffold retirement on the triangle-path; the smaller intermediate solutions introduced alongside the triangle-path tasks each have their own retirement arc owned by another task, but `GRAPHICS-081` verifies the audit is clean before deleting the minimal recipe:

- [ ] `GRAPHICS-029A`'s **no-op default provider** is no longer the resolution path for any registered selector (replaced by `GRAPHICS-029B`'s `TriangleProvider` registration). The "unknown selector → terminate" semantic from `GRAPHICS-029B`'s closing checkbox is in place; the no-op default is unused.
- [ ] `GRAPHICS-029B`'s **direct `m_ReferenceCamera → RenderFrameInput::Camera` substitution** has been deleted by `RUNTIME-081` (CameraControllers); the reference-scene `CameraViewInput` survives only as the controller's seed. Grep `m_ReferenceCamera` returns zero matches in `Engine::BuildRenderFrameInput` and equivalent helpers.
- [ ] `GRAPHICS-029B`'s **`#if __has_include(...)` (or CMake-flag) test guard** for `ProceduralGeometryRef` absence has been removed by `GRAPHICS-030A`'s retirement; the corresponding contract assertion is now unconditional.
- [ ] `GRAPHICS-080`'s **acceptance pointer to the minimal recipe** has been replaced by the canonical default-recipe pointer per its staged-acceptance section.
- [ ] `GRAPHICS-079`'s **closing-cleanup assertion** is in place: a default-recipe frame in the operational state reports zero `SkippedNonOperational`/`SkippedUnavailable` statuses for canonical pass names.

If any of these audit boxes cannot be checked, the corresponding owner task must retire first; `GRAPHICS-081` does not paper over an unfinished retirement arc.

## Required changes

### Source removal
- [ ] Remove `Extrinsic::Graphics::BuildMinimalDebugSurfaceRecipe(...)` and the `FrameRecipeKind::MinimalDebug` enum value from `Extrinsic.Graphics.FrameRecipe`.
- [ ] Remove the `RenderConfig::FrameRecipe` selector (or, if other recipes are added later, keep the field and remove only the `MinimalDebug` variant). Default behavior is the canonical default recipe.
- [ ] Delete `src/graphics/renderer/Passes/Pass.Surface.MinimalDebug.cppm`, `Pass.Surface.MinimalDebug.cpp`, `Pass.Present.MinimalDebug.cppm`, `Pass.Present.MinimalDebug.cpp`.
- [ ] Remove `MinimalDebugSurfacePass`, `MinimalDebugPresentPass`, the corresponding pipeline leases, and the `m_MinimalDebugSurfacePass` / `m_MinimalDebugPresentPass` members from `NullRenderer` (`Graphics.Renderer.cpp`).
- [ ] Remove the `"Pass.Surface.MinimalDebug"` and `"Pass.Present.MinimalDebug"` branches from the renderer's executor lambda. Remove the helper functions `RecordMinimalDebugSurfacePass(...)` and `RecordMinimalDebugPresentPass(...)`.
- [ ] Remove the three diagnostics counters (`MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount`) from `RenderGraphFrameStats` and from any reset/accumulation code.

### CMake / module-graph removal
- [ ] Remove the deleted pass-module entries from `src/graphics/renderer/Passes/CMakeLists.txt`.
- [ ] Refresh `docs/api/generated/module_inventory.md` via the recorded tool.

### Test removal / migration
- [ ] Delete the CPU contract tests authored by `GRAPHICS-032A`/`B`/`C` that target the minimal recipe (recipe introspection asserting the two-pass declaration, the property-based surface/present command-stream tests, the end-to-end CPU acceptance driver). The canonical equivalents under the default recipe (authored by `GRAPHICS-070..076`) must already cover the same shape.
- [ ] Delete the opt-in `gpu;vulkan` fixtures authored by `GRAPHICS-032D` and `GRAPHICS-033D` that target the minimal recipe. The default-recipe equivalents must already cover visible-triangle pixel assertions.
- [ ] Update `tests/README.md` to remove rows referring to the minimal recipe fixtures.

### Docs removal / migration
- [ ] Update `src/graphics/framegraph/README.md`: remove the `MinimalDebugSurface` recipe row, the `recipe.minimal-debug-surface` label note, and the cross-link to `GRAPHICS-032`.
- [ ] Update `src/graphics/renderer/README.md`: remove the `MinimalDebug*` pass rows, the three diagnostics counters, and the cross-link to `GRAPHICS-032` for the bootstrap-only behavior. The canonical `Pass.Forward.Surface` + `Pass.Present` rows from `GRAPHICS-070`/`076` remain.
- [ ] Update `src/graphics/vulkan/README.md`: remove the `MinimalRecipeRecordingMissing` reason from the GRAPHICS-033 reason taxonomy if and only if no other path consumes it. Otherwise rename it to a default-recipe-recording absence reason and document the rename. The renaming option is preferred to keep `VulkanOperationalReason` append-only across history; deletion is allowed only if the reason was never observed in any released CI run.
- [ ] Update `docs/architecture/graphics.md` and `docs/architecture/rendering-three-pass.md` if they reference the minimal recipe by name; otherwise leave untouched.
- [ ] Update `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` with a one-line retire note pointing at this task (do not rewrite history).

### Task retirement
- [ ] Move the planning parents and implementation children of the retired scaffold's lifecycle to `tasks/done/` with completion notes. The planning parent `GRAPHICS-032` is already in `tasks/done/`; only its implementation children `GRAPHICS-032A`/`B`/`C`/`D` (which by then will also be retired) need their done-files updated with a `Retired by GRAPHICS-081` cross-link.
- [ ] Mark `GRAPHICS-033C` and `GRAPHICS-033D` done-files with a similar cross-link if their bodies referenced the minimal recipe directly.

## Tests
- [ ] Build smoke: `cmake --build --preset ci --target IntrinsicTests` succeeds after the deletions; no dangling module imports.
- [ ] `contract;graphics` test (pre-existing default-recipe coverage): default-recipe pass set continues to record every canonical pass against the GRAPHICS-070..076 tests; no regression.
- [ ] `gpu;vulkan` fixture (pre-existing default-recipe coverage): the default-recipe visible-triangle smoke still passes on Vulkan-capable hosts; the minimal-recipe smoke is gone.
- [ ] Negative test: a build search for the strings `MinimalDebugSurface`, `MinimalDebug`, `recipe.minimal-debug-surface`, `BuildMinimalDebugSurfaceRecipe`, `MinimalSurfacePassExecutions`, `MinimalPresentPassExecutions`, `MinimalRecipeMissingPrerequisiteCount` returns zero matches in `src/`, `tests/`, `assets/`, and `cmake/`. (One match is acceptable inside `docs/reviews/` and inside this task file; no others.)
- [ ] `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` produces a diff that only removes the four deleted module entries.

## Docs
- [ ] Update the rendering DAG (`tasks/backlog/rendering/README.md`) to mark `GRAPHICS-032`/`032A..D` and the `MinimalDebug` rows as retired by `GRAPHICS-081`.
- [ ] Update the parent backlog README (`tasks/backlog/README.md`) Theme A section to note the scaffold retirement.

## Acceptance criteria
- [ ] All minimal-recipe artifacts are removed from `src/`, `tests/`, `assets/`, `cmake/`, and the module inventory.
- [ ] The default-recipe visible-triangle `gpu;vulkan` fixture passes on Vulkan-capable hosts (the test is the same as the one that replaced `GRAPHICS-033D`'s minimal-recipe assertion).
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict`, `python3 tools/docs/check_doc_links.py --root .`, and `python3 tools/repo/check_layering.py --root src --strict` all pass after the deletions.
- [ ] `VulkanOperationalReason` taxonomy remains append-only (renamed reasons preferred over deleted reasons).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Opt-in default-recipe Vulkan smoke (on Vulkan-capable hosts):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsIntegrationTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu;vulkan' --timeout 120

# Confirm no minimal-recipe symbols remain:
! grep -RE 'MinimalDebugSurface|MinimalDebug|recipe\.minimal-debug-surface|BuildMinimalDebugSurfaceRecipe|MinimalSurfacePassExecutions|MinimalPresentPassExecutions|MinimalRecipeMissingPrerequisiteCount' \
  src tests assets cmake

python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing the retirement with new feature work or shader/material/recipe additions.
- Reducing `gpu;vulkan` smoke coverage below the level reached by `GRAPHICS-033D` (the default-recipe equivalent must remain in place).
- Deleting `Material.DefaultDebugSurface` (slot 0) — it stays as the canonical missing-material fallback.
- Deleting reference scene / procedural geometry / Vulkan operational gate code.
- Bypassing `VulkanOperationalReason` append-only invariant — prefer renaming over deleting reason enum values.
- Removing `docs/reviews/2026-05-11-sandbox-graphics-gap-analysis.md` or rewriting its history; only append a retire-note pointer.

## Next verification step
- Confirm all upstream gates (`GRAPHICS-031A/B`, `GRAPHICS-070..076`) are retired in `tasks/done/`, confirm the default-recipe `gpu;vulkan` visible-triangle smoke is green on a Vulkan-capable host, then begin deletions module-by-module and re-run the verification commands above after each module deletion.
