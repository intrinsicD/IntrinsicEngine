---
id: LEGACY-043
theme: F
depends_on:
  - GRAPHICS-105
maturity_target: Retired
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - repo.source-documentation
---
# LEGACY-043 — Retire stale multi-descriptor-set shader sources

## Goal
- Delete the pre-bindless GLSL shader families under `assets/shaders/` that
  use retired multi-descriptor-set or non-heap fixed-`set = 0` layouts (plus
  their paired fragments), which no renderer pass references and which cannot
  form promoted pipelines against the Vulkan device's single-set global
  layout — and stop including them in clean or shader-invalidated builds.

## Non-goals
- No changes to the active binding model (single bindless heap at
  `set = 0` + 256-byte push-constant range + BDA vertex pulling) or to any
  shader the renderer loads.
- No changes to `cmake/CompileShaders.cmake`'s glob mechanism beyond what
  is needed to stop compiling deleted files (deleting the sources is
  sufficient; `CONFIGURE_DEPENDS` re-scans).
- No shader feature work, no new pipelines.

## Context
- REVIEW-007 G03 (operator decision 2026-10-06) added root `shadow_depth.vert`
  and the conditional `.glsl` includes below to this task; no immediate
  deletion or unblocking of `GRAPHICS-105`.
- REVIEW-007 G01 already deleted root `triangle.vert`, `triangle.frag`,
  `point.vert`, `point.frag`, `line.vert`, `line.frag` and
  `deferred/gbuffer.vert` (commit a3ded5d7c, together with 21 other unloaded
  shaders). They are no longer candidates here; read them at `087e6e17b`.
- The compiler-locality work through GRAPHICS-143 did not remove these shader
  sources. Preserve the GRAPHICS-105 dependency and its deferred-shader decision;
  GRAPHICS-144 is a separate renderer dependency task, not a shader retirement.
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
  currently loads `deferred/default_debug_gbuffer.frag.spv`.
- Current deletion candidates are root `surface.vert`, `surface.frag`,
  `surface_gbuffer.frag`, `deferred_lighting.frag`, and `shadow_depth.vert`
  (fixed `set = 0` `CameraBuffer` layout; the shadow pipeline already loads
  `depth_prepass.vert.spv`, so a `set > 0` search alone misses it).
- Conditional include candidates: `shadow_sampling.glsl` (included only by
  `surface.frag` and `deferred_lighting.frag`) and `surface_color_resolve.glsl`
  (included only by `surface.frag` and `surface_gbuffer.frag`). Delete each
  only after its last includer is gone and a repository-wide reference check
  is empty; no blanket `.glsl` cleanup.
- Known legacy source-test readers: `Test.RendererFrameLifecycle.cpp`
  (`ForwardSurfacePipelineSurvivesOperationalRebuild`) reads `surface.frag`
  and `surface_gbuffer.frag` into `retainedSurfaceFragment` /
  `retainedGBufferFragment` and asserts `DecodePropertyTextureNormal` on them.
  Remove exactly those two readers and their assertions with the shaders; keep
  the test and its active-shader, `common/surface_material.glsl` and
  `common/property_texture_normal.glsl` checks.
  `deferred/gbuffer.frag` becomes eligible only if `GRAPHICS-105` explicitly
  consolidates its contract into the surviving default deferred path; if
  `GRAPHICS-105` retains it, this task must retain it too.
- `cmake/CompileShaders.cmake` uses `file(GLOB_RECURSE)` over
  `assets/shaders/`, so stale sources remain build inputs and produce `.spv`
  outputs on clean builds or when their source/shared shader includes change.
  Settled incremental builds reuse up-to-date outputs.
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
- [x] Root `line.frag` (and the other root triangle/point/line shaders plus
      `deferred/gbuffer.vert`) deleted by REVIEW-007 G01 (commit a3ded5d7c).
- [ ] Re-verify `shadow_depth.vert` is unreferenced; the active shadow
      pipeline stays unchanged.
- [ ] Remove the two legacy source-test readers named in Context together
      with their assertions; do not remove any other test or fixture.
- [ ] Delete `shadow_sampling.glsl` / `surface_color_resolve.glsl` only if no
      includer remains.
- [ ] Delete the confirmed-stale shader sources.
- [ ] Remove or update stale mentions of the deleted files in
      renderer/FrameRecipe/pass source comments, renderer contract tests,
      `src/graphics/renderer/README.md`, architecture/ADR docs, and agent
      review guidance that still treats them as available (`rg` each resolved
      filename across `src/`, `tests/`, `assets/`, and `docs/`). Explanatory
      retirement history may continue to name deleted paths explicitly.
      Known sites at `087e6e17b`: `Graphics.Renderer.cpp` (legacy-shader
      comments near the surface/shadow/deferred pipeline builders),
      `Graphics.FrameRecipe.cpp`, `Pass.Deferred.Lighting.cpp`,
      `Test.RendererFrameLifecycle.cpp` comments,
      `deferred/lighting.frag`, `deferred/default_debug_gbuffer.frag`,
      `src/graphics/renderer/README.md` (push-constant compatibility policy),
      `docs/architecture/rendering-target-architecture.md`,
      `docs/architecture/rendering-three-pass.md`, ADR-0011, ADR-0022,
      `docs/agent/review.md`, and `RUNTIME-218` (`deferred_lighting.frag:92`).
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
      no referenced shader or test fixture is deleted, except the two
      inventoried legacy source-test readers removed with their shaders.
- [ ] `shadow_depth.vert` is re-confirmed unreferenced and the active shadow
      pipeline is unchanged; includes are deleted only without remaining
      includers; active material/normal-decode assertions still pass.
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
deleted=(surface.vert surface.frag surface_gbuffer.frag deferred_lighting.frag shadow_depth.vert)
# If GRAPHICS-105 consolidated deferred/gbuffer.frag, append it to deleted.
for f in "${deleted[@]}"; do
  test ! -e "assets/shaders/$f"
  test ! -e "build/legacy-043-ci/bin/shaders/$f.spv"
  ! rg -n --fixed-strings "shaders/$f.spv" src tests assets
  ! rg -n --fixed-strings "ReadShaderSource(\"$f\")" tests
done
# For each deleted include: ! rg -n '#include "(shadow_sampling|surface_color_resolve).glsl"' assets/shaders
cmake --build build/legacy-043-ci --target IntrinsicShaderOutputs
```

## Forbidden changes
- Deleting or editing any shader referenced by a renderer pass, recipe
  document, or test, except deleting `deferred/gbuffer.frag` after an explicit
  `GRAPHICS-105` consolidation decision, and removing only the two
  inventoried legacy source-test readers together with their shaders.
- Redesigning the binding model or `CompileShaders.cmake` beyond stale
  removal.
- Mixing in unrelated shader or renderer work.

## Maturity
- Target: `Retired` — the legacy sources are deleted with no
  compatibility shim; the active bindless/BDA shaders are untouched. No
  follow-up is owed.
