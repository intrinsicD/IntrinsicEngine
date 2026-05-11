# Task 11 — Add missing task: shader/material/texture hot reload

- Status: completed (2026-05-02)
- Owner: Codex (current branch)
- Branch / PR: current branch / TBD
- Completion date: 2026-05-02
- Commit / PR: local split branch `split/current-working-tree-2026-05-02`; remote PR reference TBD.
- Follow-ups: implementation remains in `tasks/backlog/rendering/GRAPHICS-023-shader-material-texture-hot-reload.md`.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root . --strict`.

---

You are working in https://github.com/intrinsicD/IntrinsicEngine.

Create a dedicated task for promoted shader/material/texture hot-reload ownership.

Create:

`tasks/backlog/rendering/GRAPHICS-023-shader-material-texture-hot-reload.md`

## Goal

Define and implement promoted hot-reload contracts for shaders, material layouts, pipeline cache invalidation, and texture/material residency updates without depending on legacy graphics modules.

## Context

The parity matrix lists shader registry/hot reload and material/texture hot reload as unproven for promoted graphics. GRAPHICS-006 mentions reload invalidation behavior but does not own the full file-watch/compiler/pipeline/material/texture reload path.

## Required scope

- Shader source identity and dependency tracking.
- Shader compilation/recompilation seam behind RHI/backend or tools boundary.
- Pipeline cache invalidation and rebuild policy.
- Material layout compatibility checks after reload.
- Texture reload/update path through graphics asset residency APIs.
- Failure fallback: keep last known good pipeline/texture/material when reload fails.
- Diagnostics for compile failure, incompatible layout, missing texture, failed upload.
- CPU-testable seams; Vulkan smoke tests optional.

## Tests

- [x] `unit;graphics` tests for cache-key stability and invalidation decisions.
- [x] `contract;graphics` tests for failure fallback diagnostics.
- [x] Optional `gpu;vulkan` tests for actual shader pipeline reload where supported.

## Docs

- [x] Update `docs/architecture/graphics.md`.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md`.
- [x] Cross-link GRAPHICS-006 and GRAPHICS-015.

## Acceptance criteria

- [x] Hot reload has a promoted task home.
- [x] GRAPHICS-006 can focus on registry/material/pipeline contracts without swallowing file watching and live reload end-to-end.
- [x] Failed reload does not destroy the last working runtime state.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No legacy module dependency.
- No mandatory Vulkan tests in default CPU gate.
- No material editor UI work.
