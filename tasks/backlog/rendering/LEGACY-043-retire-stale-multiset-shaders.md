---
id: LEGACY-043
theme: F
depends_on: []
maturity_target: Retired
---
# LEGACY-043 — Retire stale multi-descriptor-set shader sources

## Goal
- Delete the pre-bindless GLSL shader sources under `assets/shaders/` that
  use the retired multi-descriptor-set binding model (`set = 0..3` camera/
  texture/instance/material tiers), which no renderer pass references and
  which are physically incompatible with the promoted Vulkan device's
  single-set global pipeline layout — and stop compiling them on every
  build.

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
- Confirmed-stale sources (multi-set model, not referenced by any
  `PipelineDesc` shader path in `src/graphics/renderer/`):
  `assets/shaders/surface.vert`, `surface.frag`, `surface_gbuffer.vert`,
  `surface_gbuffer.frag`, `deferred_lighting.frag`,
  `deferred/gbuffer.vert`, `deferred/gbuffer.frag`, root-level
  `triangle.vert`, `triangle.frag`, `point.vert`, `point.frag`,
  `line.vert`, `line.frag` (root level only — the referenced replacements
  live under `forward/` and `deferred/`).
- `cmake/CompileShaders.cmake` uses `file(GLOB_RECURSE)` over
  `assets/shaders/`, so every stale source is compiled to `.spv` and
  copied into the runtime shader directory on every build.
- The implementer must re-verify the stale list at execution time
  (`grep -r <name> src/`) before deleting — shader path references are
  string-built via `Core::Filesystem::GetShaderPath`, and substring
  collisions (e.g. `default_debug_surface.frag` contains `surface.frag`)
  make a naive grep read as "referenced". Match full path strings.

## Required changes
- [ ] Re-verify each file in the Context list is not loaded by any
      `PipelineDesc` shader path (full-path match, not substring) in
      `src/`, `tests/`, or recipe/config documents under `config/` and
      `assets/`.
- [ ] Delete the confirmed-stale shader sources.
- [ ] Remove or update stale mentions of the deleted files in
      `src/graphics/renderer/README.md`, `src/runtime/README.md`, and any
      other docs that name them (`grep -rn` for each deleted filename).
- [ ] Update the legacy-model reference in
      `assets/shaders/deferred/lighting.frag`'s header comment so it does
      not point at a deleted file (describe the retired model inline
      instead).

## Tests
- [ ] Clean configure + build compiles the shader set with no references
      to deleted files (`CONFIGURE_DEPENDS` glob re-scan picks up the
      deletions).
- [ ] Default CPU gate stays green:
      `ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine'`.
- [ ] If a Vulkan-capable host is available, one opt-in smoke run
      (`ctest --test-dir build/ci -L 'gpu' ...`) confirming the default
      recipe still builds all pipelines; otherwise record that the change
      is source-deletion only and the CPU gate plus a successful shader
      compile pass is the evidence.

## Docs
- [ ] Doc mentions cleaned per Required changes (renderer/runtime READMEs).
- [ ] Update `tasks/backlog/rendering/README.md` status line on retirement.

## Acceptance criteria
- [ ] No shader source under `assets/shaders/` declares a descriptor set
      other than the bindless heap at `set = 0, binding = 0`
      (`grep -rE "set = [1-9]" assets/shaders/` returns nothing, and every
      remaining `set = 0` binding is the heap).
- [ ] The build output `shaders/` directory no longer contains `.spv`
      artifacts for the deleted sources after a clean build.
- [ ] No doc or comment references a deleted shader path.
- [ ] CPU gate passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
grep -rEn "set = [1-9]" assets/shaders/ || echo "no multi-set shaders remain"
for f in surface.vert surface.frag surface_gbuffer.vert surface_gbuffer.frag deferred_lighting.frag; do grep -rn --include='*.cpp' --include='*.cppm' "shaders/$f" src/ && echo "STALE REF: $f"; done; true
```

## Forbidden changes
- Deleting or editing any shader referenced by a renderer pass, recipe
  document, or test.
- Redesigning the binding model or `CompileShaders.cmake` beyond stale
  removal.
- Mixing in unrelated shader or renderer work.

## Maturity
- Target: `Retired` — the legacy sources are deleted with no
  compatibility shim; the active bindless/BDA shaders are untouched. No
  follow-up is owed.
