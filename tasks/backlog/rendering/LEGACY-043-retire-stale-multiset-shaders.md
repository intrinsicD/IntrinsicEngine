---
id: LEGACY-043
theme: F
depends_on:
  - GRAPHICS-105
maturity_target: Retired
---
# LEGACY-043 — Retire stale multi-descriptor-set shader sources

## Goal
- Delete the pre-bindless GLSL shader families under `assets/shaders/` that
  use retired multi-descriptor-set or non-heap fixed-`set = 0` layouts (plus
  their paired fragments), which no renderer pass references and which cannot
  form promoted pipelines against the Vulkan device's single-set global
  layout — and stop compiling them on every build.

## Non-goals
- No changes to the active binding model (single bindless heap at
  `set = 0` + 256-byte push-constant range + BDA vertex pulling) or to any
  shader the renderer loads.
- No changes to `cmake/CompileShaders.cmake`'s glob mechanism beyond what
  is needed to stop compiling deleted files (deleting the sources is
  sufficient; `CONFIGURE_DEPENDS` re-scans).
- No shader feature work, no new pipelines.

## Context
- Owning subsystem/layer: `graphics` shader assets
  (`assets/shaders/`), with doc references in
  `src/graphics/renderer/README.md`.
- The promoted Vulkan device creates exactly one pipeline layout:
  one descriptor set (the bindless heap, `Backends.Vulkan.Device.cpp`,
  `setLayoutCount = 1`) plus a single push-constant range.
  `VulkanCommandContext::BindPipeline` binds only that set. Shaders
  declaring `set = 1..3` (or non-heap `set = 0` bindings such as a
  `CameraBuffer` UBO) can never form a valid pipeline against it.
- The active `assets/shaders/deferred/lighting.frag` header comment
  already names `assets/shaders/deferred_lighting.frag` as the legacy
  model that "cannot be honored" by the promoted layout.
- The 2026-07-16 execution-time audit corrected the original inventory:
  `assets/shaders/surface_gbuffer.vert` does not exist;
  `assets/shaders/deferred/gbuffer.frag` is read by
  `RendererFrameLifecycle` and is one of `GRAPHICS-105`'s two promoted
  `ResolveSurfaceNormal` contract paths even though the production descriptor
  currently loads `deferred/default_debug_gbuffer.frag.spv`; and root
  `assets/shaders/line.frag` is named by the `RHI.PipelineRegistry` fixture.
  Those files are not currently unreferenced deletion candidates.
- Current deletion candidates are root `surface.vert`, `surface.frag`,
  `surface_gbuffer.frag`, `deferred_lighting.frag`,
  `deferred/gbuffer.vert`, root `triangle.vert`, `triangle.frag`,
  `point.vert`, `point.frag`, and `line.vert`. Root `line.frag` becomes a
  candidate only after its test fixture is redirected to a surviving shader.
  `deferred/gbuffer.frag` becomes eligible only if `GRAPHICS-105` explicitly
  consolidates its contract into the surviving default deferred path; if
  `GRAPHICS-105` retains it, this task must retain it too.
- `cmake/CompileShaders.cmake` uses `file(GLOB_RECURSE)` over
  `assets/shaders/`, so every stale source is compiled to `.spv` and
  emitted into the runtime shader directory on every build.
- The implementer must re-verify the stale list at execution time
  (`grep -r <name> src/`) before deleting — shader path references are
  string-built via `Core::Filesystem::GetShaderPath`, and substring
  collisions (e.g. `default_debug_surface.frag` contains `surface.frag`)
  make a naive grep read as "referenced". Match full path strings.
- The original whole-tree descriptor assertion was also too broad: other
  shader families legitimately use fixed `set = 0` bindings. This task audits
  only the retirement candidates; it does not redefine every surviving shader
  as a bindless graphics pipeline.

## Required changes
- [ ] Re-verify each current deletion candidate in the Context list is not
      loaded by any
      `PipelineDesc` shader path (full-path match, not substring) in
      `src/`, `tests/`, or recipe/config documents under `assets/`.
- [ ] Record the candidate-scoped descriptor audit with a whitespace-tolerant
      pattern such as `layout\s*\([^)]*\bset\s*=\s*[1-9]`; do not turn it
      into a whole-tree ban on fixed-binding shader families.
- [ ] Apply the completed `GRAPHICS-105` decision: retain
      `deferred/gbuffer.frag` if it remains a promoted contract path, or add it
      to the deletion inventory only if its contract has been consolidated
      into a surviving shader.
- [ ] Redirect the `RHI.PipelineRegistry` root `line.frag` fixture to a
      surviving shader path before treating root `line.frag` as stale.
- [ ] Delete the confirmed-stale shader sources.
- [ ] Remove or update stale mentions of the deleted files in
      renderer/FrameRecipe/pass source comments, renderer contract tests,
      `src/graphics/renderer/README.md`, architecture/ADR docs, and agent
      review guidance that still treats them as available (`rg` each resolved
      filename across `src/`, `tests/`, `assets/`, and `docs/`). Explanatory
      retirement history may continue to name deleted paths explicitly.
- [ ] Update the legacy-model reference in
      `assets/shaders/deferred/lighting.frag`'s header comment so it does
      not point at a deleted file (describe the retired model inline
      instead).
- [ ] If canonical `docs/agent/*` guidance changes, regenerate its skill
      mirror with `python3 tools/agents/sync_skills.py --write`.

## Tests
- [ ] Configure and build in a new, dedicated build directory so the shader
      set is compiled with no orphaned outputs from earlier glob contents.
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/legacy-043-ci
      -LE 'gpu|vulkan|slow|flaky-quarantine'`.
- [ ] If a Vulkan-capable host is available, one opt-in smoke run
      (`ctest --test-dir build/ci -L 'gpu' ...`) confirming the default
      recipe still builds all pipelines; otherwise record that the change
      is source-deletion only and the CPU gate plus a successful shader
      compile pass is the evidence.

## Docs
- [ ] Doc mentions cleaned per Required changes (renderer/runtime READMEs).
- [ ] Update `tasks/backlog/rendering/README.md` status line on retirement.

## Acceptance criteria
- [ ] Every deleted candidate is proven unreferenced and incompatible with the
      promoted path it was claimed to duplicate; no whole-tree assertion is
      made about legitimate fixed-binding shader families outside this task.
- [ ] The build output `shaders/` directory no longer contains `.spv`
      artifacts for the deleted sources after the dedicated fresh build.
- [ ] `deferred/gbuffer.frag` follows the recorded `GRAPHICS-105` outcome, and
      no referenced shader or test fixture is deleted.
- [ ] No live pipeline, recipe, config, or test reference treats a deleted
      shader path as available; explicit compatibility explanation and retired
      history may still name it as deleted.
- [ ] CPU gate passes.

## Verification
```bash
test ! -e build/legacy-043-ci
cmake --preset ci -B build/legacy-043-ci
cmake --build build/legacy-043-ci --target IntrinsicTests
ctest --test-dir build/legacy-043-ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
deleted=(surface.vert surface.frag surface_gbuffer.frag deferred_lighting.frag deferred/gbuffer.vert triangle.vert triangle.frag point.vert point.frag line.vert line.frag)
# If GRAPHICS-105 consolidated deferred/gbuffer.frag, append it to deleted.
for f in "${deleted[@]}"; do
  test ! -e "assets/shaders/$f"
  test ! -e "build/legacy-043-ci/bin/shaders/$f.spv"
  ! rg -n --fixed-strings "shaders/$f.spv" src tests assets
done
```

## Forbidden changes
- Deleting or editing any shader referenced by a renderer pass, recipe
  document, or test, except deleting `deferred/gbuffer.frag` after an explicit
  `GRAPHICS-105` consolidation decision or redirecting the test-only root
  `line.frag` fixture before deletion.
- Redesigning the binding model or `CompileShaders.cmake` beyond stale
  removal.
- Mixing in unrelated shader or renderer work.

## Maturity
- Target: `Retired` — the legacy sources are deleted with no
  compatibility shim; the active bindless/BDA shaders are untouched. No
  follow-up is owed.
