# BUILD-001 — Wire shader compilation to the promoted Sandbox build

## Goal
- Make the promoted `ExtrinsicSandbox` target compile every `assets/shaders/**.{vert,frag,comp}` GLSL source to SPIR-V at build time so that runtime calls like `RHI::PipelineDesc::VertexShaderPath = "assets/shaders/depth_prepass.vert"` and (after `GRAPHICS-031A`) `"assets/shaders/forward/default_debug_surface.vert"` resolve to a `.spv` binary in the runtime output directory.

## Non-goals
- No change to the existing `cmake/CompileShaders.cmake` helper.
- No change to GLSL source content.
- No change to the legacy `src/legacy/Apps/Sandbox` shader wiring (it already invokes the helper; that wiring is migrating out, not changing semantics in this task).
- No introduction of a new shader language (Slang is `GRAPHICS-041` planning-only).

## Context
- Status: done.
- Owner/agent: GitHub Copilot on `main`.
- Owner/layer: `app/Sandbox` (CMake) and the build system; no source/runtime layer changes.
- Today: `cmake/CompileShaders.cmake` exposes `intrinsic_add_glsl_shaders(<target>)`, but its only invocation is `src/legacy/Apps/Sandbox/CMakeLists.txt:21`. The promoted Sandbox CMake (`src/app/Sandbox/CMakeLists.txt`) does not invoke it, so `ExtrinsicSandbox` builds without producing any SPIR-V.
- Consequence: even after `GRAPHICS-029B` / `GRAPHICS-030B` / `GRAPHICS-031A` land and the renderer attempts `PipelineManager::Create(PipelineDesc{ VertexShaderPath = "assets/shaders/.../*.vert" })`, the loader fails to find the `.spv` artefact and pipeline creation increments the GRAPHICS-018 fallback counter.
- This task does not require any C++ change; it is a CMake-only addition gated by `glslc` availability.

## Required changes
- [x] In `src/app/Sandbox/CMakeLists.txt`, add `include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)` and `intrinsic_add_glsl_shaders(ExtrinsicSandbox)` (mirroring the legacy invocation pattern).
- [x] Confirm `CMAKE_RUNTIME_OUTPUT_DIRECTORY` is honored so the produced `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/...` tree sits next to the executable.
- [x] Document the build-time `glslc` requirement in `src/app/Sandbox/README.md` (warning that without `glslc`, pipelines will fail to load and the renderer will increment the fallback counter).
- [x] Optional: keep the existing configure-time `glslc not found` warning path; no helper behavior change was required.

## Tests
- [x] Build smoke: `cmake --build --preset ci --target ExtrinsicSandbox_Shaders` produces the expected `.spv` files (validated with `tools/repo/check_shader_outputs.py`).
- [x] No `gpu`/`vulkan` test added by this task.

## Docs
- [x] Update `src/app/Sandbox/README.md` to mention the new shader-compile step and the `glslc` host dependency.
- [x] Update `cmake/README.md` (if present) or add a one-liner near `cmake/CompileShaders.cmake` documenting that the promoted Sandbox now invokes the helper.

## Acceptance criteria
- [x] After `cmake --build --preset ci --target ExtrinsicSandbox`, the runtime output directory contains a `shaders/` subdirectory with the current promoted shader set, including `depth_prepass.vert.spv`, `instance_cull.comp.spv`, `post_fullscreen.vert.spv`, and triangle smoke shaders. The GRAPHICS-031A default-debug shader outputs will be produced by the same target once those sources land.
- [x] When `glslc` is not installed, configure emits a clear warning and the build still succeeds for non-pipeline targets (matching the existing helper's behavior).
- [x] No layering or task-policy violation introduced.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target ExtrinsicSandbox_Shaders
python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders \
  --require depth_prepass.vert.spv \
  --require instance_cull.comp.spv \
  --require post_fullscreen.vert.spv \
  --require triangle.vert.spv \
  --require triangle.frag.spv
cmake --build --preset ci --target ExtrinsicSandbox
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Modifying GLSL source content.
- Changing the legacy Sandbox shader wiring.
- Introducing a new shader language or build path.

## Next verification step
- Continue with the next unblocked Theme A slice (`RUNTIME-070` or `GRAPHICS-030B`, per dependency order and ownership).

## Completion
- Completed: 2026-05-12.
- Commit references: pending local commit/PR.
- Verification run in this session:
  - `cmake --preset ci`
  - `cmake --build --preset ci --target ExtrinsicSandbox_Shaders`
  - `python3 tools/repo/check_shader_outputs.py --dir build/ci/bin/shaders --require depth_prepass.vert.spv --require instance_cull.comp.spv --require post_fullscreen.vert.spv --require triangle.vert.spv --require triangle.frag.spv`
  - `cmake --build --preset ci --target ExtrinsicSandbox`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
  - `python3 tools/docs/check_doc_links.py --root .`
  - `cmake --build --preset ci --target IntrinsicTests`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/repo/check_test_layout.py --root . --strict`
- Additional broad gate attempted: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` failed on 14 graphics/asset-cache tests outside the BUILD-001 touched scope; see session report for the failing test list.
