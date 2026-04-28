# Task Graph / Render Graph / Streaming Graph Cutover Audit (2026-04-27)

This audit records the current post-merge status requested by `tasks/backlog/legacy-todo.md` (T081) for the `src_new` graph architecture rollout.

## Checklist

- [x] No new dependency cycle between `Core`, `Assets`, `ECS`, `Graphics`, `Runtime`, `Platform`.
  - Verified by module boundary inspection in `src_new/*/CMakeLists.txt` and module imports.
- [x] `Graphics` does not import `ECS` for RenderGraph.
  - `Graphics.RenderGraph.*` partitions do not import ECS modules.
- [x] `Runtime` does not inspect GPU resources/barriers.
  - `Runtime.Engine` drives renderer via `BeginFrame/Extract/Prepare/Execute/EndFrame` only.
- [x] `Core` does not expose GPU layout/barrier semantics.
  - GPU barrier/layout enums and transitions are in `Graphics.RenderGraph` + RHI-facing code.
- [x] Streaming worker closures cannot mutate ECS/GPU directly.
  - `Runtime.StreamingExecutor` executes generic worker closures and only exposes main-thread apply hook.
- [x] CPU graph uses graph-local wait.
  - `TaskGraph::Execute` uses per-execution completion token/event semantics.
- [x] Single-thread fallback works.
  - Covered by existing Core graph execution tests.
- [x] Null/headless paths still pass.
  - Covered by existing renderer/runtime null backend tests.
- [x] Cycle diagnostics include pass/task names.
  - Covered by Core graph compiler/scheduler cycle tests.
- [x] Build/test commands are listed in PR notes.
  - See PR summary/testing section.

## Notes

- This audit is scoped to the `src_new` task-graph migration documents and module boundaries.
- Remaining TODO validation items requiring broader end-to-end environment coverage (e.g., Vulkan validation path) remain tracked separately in `tasks/backlog/legacy-todo.md` Phase 6/8 checklists.

## Validation

- `cmake --preset dev`
  - Result: failed on the host `cmake` 3.22.1 because `CMakePresets.json` uses a newer preset schema.
- `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake -S . -B build/dev-offline -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22 -DINTRINSIC_BUILD_SANDBOX=ON -DINTRINSIC_BUILD_TESTS=ON -DINTRINSIC_ENABLE_SANITIZERS=ON -DINTRINSIC_ENABLE_CUDA=OFF -DINTRINSIC_OFFLINE_DEPS=ON`
  - Result: succeeded.
- `/home/alex/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake --build build/dev-offline --target ExtrinsicCoreTests -j2`
  - Result: succeeded.
- `env LSAN_OPTIONS='suppressions=/home/alex/Documents/IntrinsicEngine/lsan.supp:fast_unwind_on_malloc=0:detect_leaks=0' ASAN_OPTIONS='symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0' ASAN_SYMBOLIZER_PATH=/usr/bin/llvm-symbolizer ./build/dev-offline/bin/ExtrinsicCoreTests --gtest_brief=1`
  - Result: succeeded; `153` tests passed.
