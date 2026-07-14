# BUILD-002 — Deduplicate shader custom commands across sandbox targets

## Goal
- Fix Ninja generation failures caused by multiple CMake custom commands producing the same `bin/shaders/**/*.spv` outputs when both promoted and legacy Sandbox targets wire shader compilation.

## Non-goals
- No GLSL source changes.
- No Sandbox runtime behavior changes.
- No mechanical source moves.

## Context
- Status: done.
- Completed: 2026-05-13.
- Commit references: pending local commit/PR.
- Symptom: Ninja reports `multiple rules generate bin/shaders/culling/instance_cull.comp.spv` during `ninja -t recompact` in `cmake-build-debug`.
- Expected behavior: `intrinsic_add_glsl_shaders()` may be called by multiple executable targets that share the default runtime shader output directory without generating duplicate custom-command outputs.
- Impact: CLion/Ninja build regeneration fails before compiling.
- Owner/layer: build system (`cmake/CompileShaders.cmake`) plus task record only; no engine layer dependency changes.

## Required changes
- [x] Update `cmake/CompileShaders.cmake` to emit each shader output rule once per source/output shader set.
- [x] Preserve the existing `<target>_Shaders` convenience targets for callers such as `ExtrinsicSandbox_Shaders` and `Sandbox_Shaders`.

## Tests
- [x] Reconfigure a build tree containing both Sandbox invocations and confirm Ninja no longer reports duplicate shader output rules.
- [x] Build focused promoted and legacy shader targets.

## Docs
- [x] No architecture/API docs required; task record documents the build-system bug and fix.

## Acceptance criteria
- [x] `cmake-build-debug` regeneration succeeds and the duplicate-rule repro is shown fixed.
- [x] Canonical `ci` preset was attempted; completion is environment-blocked because this machine has no `clang-20`, `clang++-20`, or `clang-scan-deps-20` binary on disk.
- [x] Focused shader build succeeds when `glslc` is available, or the existing warning-only path remains intact when it is not.
- [x] No layering violations are introduced.

## Verification
```bash
cmake -S . -B cmake-build-debug
/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja -C cmake-build-debug -t recompact
cmake --preset ci
cmake --build --preset ci --target ExtrinsicSandbox_Shaders
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Verification results
- [x] `cmake -S . -B cmake-build-debug`
- [x] `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/ninja/linux/x64/ninja -C cmake-build-debug -t recompact`
- [x] `cmake --build cmake-build-debug --target ExtrinsicSandbox_Shaders`
- [x] `cmake --build cmake-build-debug --target Sandbox_Shaders`
- [x] `python3 tools/repo/check_shader_outputs.py --dir cmake-build-debug/bin/shaders --require culling/instance_cull.comp.spv --require instance_cull.comp.spv --require post_fullscreen.vert.spv --require triangle.vert.spv --require triangle.frag.spv`
- [x] `python3 tools/agents/check_task_policy.py --root . --strict`
- [x] `python3 tools/repo/check_layering.py --root src --strict`
- [x] `cmake --preset ci` attempted and blocked by missing preset toolchain: `clang-20`, `clang++-20`, and `clang-scan-deps-20` were not found on `PATH` or under `/usr`, `/opt`, or `/home/alex`.

## Forbidden changes
- Do not remove either Sandbox shader invocation.
- Do not change shader source content.
- Do not introduce new runtime or graphics dependencies.


