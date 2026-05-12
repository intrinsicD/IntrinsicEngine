# BUILD-001 — Wire shader compilation to the promoted Sandbox build

## Goal
- Make the promoted `ExtrinsicSandbox` target compile every `assets/shaders/**.{vert,frag,comp}` GLSL source to SPIR-V at build time so that runtime calls like `RHI::PipelineDesc::VertexShaderPath = "assets/shaders/depth_prepass.vert"` and (after `GRAPHICS-031A`) `"assets/shaders/forward/default_debug_surface.vert"` resolve to a `.spv` binary in the runtime output directory.

## Non-goals
- No change to the existing `cmake/CompileShaders.cmake` helper.
- No change to GLSL source content.
- No change to the legacy `src/legacy/Apps/Sandbox` shader wiring (it already invokes the helper; that wiring is migrating out, not changing semantics in this task).
- No introduction of a new shader language (Slang is `GRAPHICS-041` planning-only).

## Context
- Status: not started.
- Owner/layer: `app/Sandbox` (CMake) and the build system; no source/runtime layer changes.
- Today: `cmake/CompileShaders.cmake` exposes `intrinsic_add_glsl_shaders(<target>)`, but its only invocation is `src/legacy/Apps/Sandbox/CMakeLists.txt:21`. The promoted Sandbox CMake (`src/app/Sandbox/CMakeLists.txt`) does not invoke it, so `ExtrinsicSandbox` builds without producing any SPIR-V.
- Consequence: even after `GRAPHICS-029B` / `GRAPHICS-030B` / `GRAPHICS-031A` land and the renderer attempts `PipelineManager::Create(PipelineDesc{ VertexShaderPath = "assets/shaders/.../*.vert" })`, the loader fails to find the `.spv` artefact and pipeline creation increments the GRAPHICS-018 fallback counter.
- This task does not require any C++ change; it is a CMake-only addition gated by `glslc` availability.

## Required changes
- [ ] In `src/app/Sandbox/CMakeLists.txt`, add `include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)` and `intrinsic_add_glsl_shaders(ExtrinsicSandbox)` (mirroring the legacy invocation pattern).
- [ ] Confirm `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is honored so the produced `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/...` tree sits next to the executable.
- [ ] Document the build-time `glslc` requirement in `src/app/Sandbox/README.md` (warning that without `glslc`, pipelines will fail to load and the renderer will increment the fallback counter).
- [ ] Optional: add a CMake-time message when `glslc` is missing with the exact apt/install hint.

## Tests
- [ ] Build smoke: `cmake --build --preset ci --target ExtrinsicSandbox_Shaders` produces the expected `.spv` files (validate with a `python3` glob in `tools/`, or CTest fixture). Acceptable lower-effort form: a CMake `add_test` that lists the runtime output `shaders/` directory and asserts non-empty file count.
- [ ] No `gpu`/`vulkan` test added by this task.

## Docs
- [ ] Update `src/app/Sandbox/README.md` to mention the new shader-compile step and the `glslc` host dependency.
- [ ] Update `cmake/README.md` (if present) or add a one-liner near `cmake/CompileShaders.cmake` documenting that the promoted Sandbox now invokes the helper.

## Acceptance criteria
- [ ] After `cmake --build --preset ci --target ExtrinsicSandbox`, the runtime output directory contains a `shaders/` subdirectory with at least the SPIR-V binaries needed by the GRAPHICS-031A/GRAPHICS-032 pipelines (`forward/default_debug_surface.vert.spv`, `forward/default_debug_surface.frag.spv`, `depth_prepass.vert.spv`, `instance_cull.comp.spv`, present pipeline shaders).
- [ ] When `glslc` is not installed, configure emits a clear warning and the build still succeeds for non-pipeline targets (matching the existing helper's behavior).
- [ ] No layering or task-policy violation introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicSandbox
test -d "$(cmake --preset ci -N 2>/dev/null | grep CMAKE_RUNTIME_OUTPUT_DIRECTORY | head -1)/shaders" || \
  find build/ci -type d -name shaders | head -1
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Modifying GLSL source content.
- Changing the legacy Sandbox shader wiring.
- Introducing a new shader language or build path.

## Next verification step
- Edit `src/app/Sandbox/CMakeLists.txt`, configure + build the target, confirm SPIR-V files land in the runtime output tree.
