# Task Graph / Render Graph / Streaming Graph Cutover Audit (2026-04-27)

This audit records the current post-merge status requested by `TODO.md` (T081) for the `src_new` graph architecture rollout.

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
- Remaining TODO validation items requiring broader end-to-end environment coverage (e.g., Vulkan validation path) remain tracked separately in `TODO.md` Phase 6/8 checklists.
