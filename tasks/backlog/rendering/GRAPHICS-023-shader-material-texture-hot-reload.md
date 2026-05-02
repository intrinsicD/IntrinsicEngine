# GRAPHICS-023 — Shader, material, and texture hot reload

## Goal
Define and implement promoted hot-reload contracts for shaders, material layouts, pipeline cache invalidation, and texture/material residency updates without depending on legacy graphics modules.

## Non-goals
- No material editor UI work.
- No mandatory Vulkan/GPU execution in the default CPU correctness gate.
- No dependency on `src/legacy` graphics modules as runtime implementation dependencies.

## Context
The parity matrix lists shader registry/hot reload and material/texture hot reload as unproven for promoted graphics. `GRAPHICS-006` owns material/shader/pipeline registry contracts and `GRAPHICS-015` owns GPU asset/texture residency, but neither should absorb the full file-watch, recompilation, cache invalidation, texture update, and failure-fallback workflow. This task provides the dedicated promoted task home for that end-to-end reload behavior while preserving the `AGENTS.md` graphics ownership boundary.

## Required changes
- Define shader source identity and dependency tracking for promoted graphics.
- Define the shader compilation/recompilation seam behind an RHI/backend or tools boundary.
- Add pipeline cache invalidation and rebuild policy for shader/material layout changes.
- Add material layout compatibility checks after reload.
- Route texture reload/update through promoted graphics asset residency APIs.
- Preserve last-known-good pipeline, texture, and material state when reload fails.
- Emit structured diagnostics for compile failure, incompatible material layout, missing texture, failed upload, and fallback activation.
- Keep CPU-testable decision seams; Vulkan smoke coverage is optional.
- Cross-link the resulting contracts with `GRAPHICS-006` and `GRAPHICS-015`.

## Tests
- Add `unit;graphics` tests for cache-key stability and invalidation decisions.
- Add `contract;graphics` tests for failure fallback diagnostics and last-known-good behavior.
- Add optional `gpu;vulkan` smoke tests for actual backend shader/pipeline reload only when supported.
- Do not require Vulkan, a window, or live file watching in the default CPU gate.

## Docs
- Update `docs/architecture/graphics.md` with the promoted reload ownership seam if public contracts change.
- Update `docs/migration/nonlegacy-parity-matrix.md` when parity status changes.
- Cross-link from `GRAPHICS-006` and `GRAPHICS-015` if their contracts are narrowed or delegated to this task.

## Acceptance criteria
- Hot reload has a promoted task home independent of legacy graphics runtime modules.
- `GRAPHICS-006` can focus on registry/material/pipeline contracts without swallowing file watching and live reload end-to-end.
- `GRAPHICS-015` can focus on GPU asset/texture residency while delegating reload orchestration here.
- Failed reload does not destroy the last working runtime state and produces deterministic diagnostics.

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
