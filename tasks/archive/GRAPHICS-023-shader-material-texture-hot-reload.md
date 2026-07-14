# GRAPHICS-023 — Shader, material, and texture hot reload

## Goal
Define and implement promoted hot-reload contracts for shaders, material layouts, pipeline cache invalidation, and texture/material residency updates without depending on legacy graphics modules.

## Non-goals
- No material editor UI work.
- No mandatory Vulkan/GPU execution in the default CPU correctness gate.
- No dependency on `src/legacy` graphics modules as runtime implementation dependencies.
- No promoted live file watcher in this task. Reload triggers are explicit
  shader-path/generation invalidation calls or runtime asset events; adding an
  editor/development watcher later is optional tooling work, not a deletion gate.

## Context
The parity matrix lists shader registry/hot reload and material/texture hot reload as unproven for promoted graphics. `GRAPHICS-006` owns material/shader/pipeline registry contracts and `GRAPHICS-015` owns GPU asset/texture residency, but neither should absorb the full file-watch, recompilation, cache invalidation, texture update, and failure-fallback workflow. This task provides the dedicated promoted task home for that end-to-end reload behavior while preserving the `AGENTS.md` graphics ownership boundary.

## Required changes
- [x] Define shader source identity and dependency tracking for promoted graphics.
- [x] Define the shader compilation/recompilation seam behind an RHI/backend or tools boundary.
- [x] Add pipeline cache invalidation and rebuild policy for shader/material layout changes.
- [x] Add material layout compatibility checks after reload.
- [x] Route texture reload/update through promoted graphics asset residency APIs.
- [x] Preserve last-known-good pipeline, texture, and material state when reload fails.
- [x] Emit structured diagnostics for compile failure, incompatible material layout, missing texture, failed upload, and fallback activation.
- [x] Keep CPU-testable decision seams; Vulkan smoke coverage is optional.
- [x] Cross-link the resulting contracts with `GRAPHICS-006` and `GRAPHICS-015`.

## Tests
- [x] Add `unit;graphics` tests for cache-key stability and invalidation decisions.
- [x] Add CPU graphics coverage for failure fallback diagnostics and last-known-good behavior.
- [x] Add optional `gpu;vulkan` smoke tests for actual backend shader/pipeline reload only when supported.
- [x] Do not require Vulkan, a window, or live file watching in the default CPU gate.

## Docs
- [x] Update `docs/architecture/graphics.md` with the promoted reload ownership seam if public contracts change.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` when parity status changes.
- [x] Cross-link from `GRAPHICS-006` and `GRAPHICS-015` if their contracts are narrowed or delegated to this task.

## Acceptance criteria
- [x] Hot reload has a promoted task home independent of legacy graphics runtime modules.
- [x] `GRAPHICS-006` can focus on registry/material/pipeline contracts without swallowing file watching and live reload end-to-end.
- [x] `GRAPHICS-015` can focus on GPU asset/texture residency while delegating reload orchestration here.
- [x] Failed reload does not destroy the last working runtime state and produces deterministic diagnostics.

## Completion
- Completed: 2026-06-03.
- Commit reference: this task-retirement commit.
- Maturity: `CPUContracted`. Shader-path/generation invalidation,
  pipeline-manager recompile diagnostics, material-layout compatibility
  decisions, texture fallback/reload retention, and runtime asset-generation
  observation/acknowledgment are CPU-testable. Backend shader recompilation
  smoke remains optional `gpu;vulkan` coverage.
- Notes:
  - `RHI::PipelineRegistry` owns shader-path + generation cache identity and
    `InvalidateShaderPath(...)` diagnostics.
  - `RHI::PipelineManager` owns the promoted recompilation seam. Failed
    `Recompile(...)` calls keep the previously active backend pipeline alive and
    report `FailedRecompileCount`.
  - `Graphics::EvaluateMaterialLayoutReloadCompatibility(...)` records typed
    layout mismatch decisions before material state can be swapped.
  - `GpuAssetCache` preserves previous GPU views across reload through the
    frame-anchored retire queue; `MaterialSystem::ResolveTextureAssetBindings`
    resolves missing/pending/failed texture assets through the fallback texture
    or deterministic failure diagnostics.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No legacy module dependency.
- No mandatory Vulkan tests in the default CPU gate.
- No material editor UI implementation.
- No shader feature expansion unrelated to reload contracts.
