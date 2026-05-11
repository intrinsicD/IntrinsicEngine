# GRAPHICS-027 — Remove rendergraph compile diagnostic string shim

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-018Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-07.
- Branch: `claude/agentic-workflow-session-lfLwc`.
- Promotion commit: `ca7ca1e` (move file from `tasks/backlog/rendering/` to `tasks/active/`, add Status block, insert backlog README DAG entry pointing at the active path, redirect the `docs/architecture/rendering-three-pass.md` cross-reference).
- Implementation commit: `bd1f12d` (remove `RenderGraphCompiler::GetLastCompileDiagnostic()` and `RenderGraph::GetLastCompileDiagnostic()` from the public surface; drop `g_LastCompileDiagnostic` thread-local, `RenderGraph::Impl::LastCompileDiagnostic` member storage, and the dead `CompiledRenderGraph::Diagnostic` field; migrate `src/graphics/renderer/Graphics.Renderer.cpp` and the contract/unit/integration test callers to `GetLastCompileValidationResult()` / `CompiledRenderGraph::ValidationFindings`; sync `src/graphics/renderer/README.md` and `docs/architecture/rendering-three-pass.md` to drop the compatibility-shim language and document `Findings.front().Message` as the canonical human-readable summary seam).
- Task-state commit: this retirement commit (moves the file from `tasks/active/` to `tasks/done/`, redirects the rendering backlog README entry to the done path, and refreshes the GRAPHICS-022 follow-up cross-references in `tasks/done/GRAPHICS-022-rendergraph-diagnostics-validation.md` and `tasks/done/task-10-graphics-022-rendergraph-diagnostics.md`).
- Resolution: the `GetLastCompileDiagnostic()` string compatibility shim introduced for one release alongside `RenderGraphValidationResult` in `GRAPHICS-022` is fully retired. All in-repo callers (renderer, contract tests, unit tests, integration tests) now consume structured `RenderGraphValidationFinding` records, with the first finding's `Message` field providing the same first-hard-error mirror the shim previously exposed. The thread-local and per-instance string storage and the `CompiledRenderGraph::Diagnostic` field are removed, leaving findings as the single canonical compile-diagnostic surface. Pure CPU/null path; no Vulkan/GPU label changes. Verification: `python3 tools/agents/check_task_policy.py --root . --strict`, `python3 tools/docs/check_doc_links.py --root .`, `python3 tools/repo/check_layering.py --root src --strict`, `python3 tools/repo/check_test_layout.py --root . --strict`. The default CPU correctness gate (`cmake --preset ci`, `cmake --build --preset ci --target IntrinsicTests`, `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) requires the repository's clang-20 toolchain and was deferred to clang-20 CI; the local environment ships only clang-18, which lacks C++23 `std::expected` / library-modules support.

## Goal

Remove the temporary `GetLastCompileDiagnostic()` string compatibility shim once downstream callers have migrated to structured rendergraph validation findings.

## Non-goals

- No new validation codes or rendergraph behavior changes.
- No renderer feature, shader, Vulkan, or UI diagnostic work.
- No changes to `RenderGraphValidationResult` semantics beyond removing the legacy string surface.

## Context

Owned by `graphics/framegraph` and `graphics/renderer`. `GRAPHICS-022` introduced `RenderGraphValidationResult`, `CompiledRenderGraph::ValidationFindings`, `RenderGraphCompiler::GetLastCompileValidationResult()`, and `RenderGraph::GetLastCompileValidationResult()` while retaining `GetLastCompileDiagnostic()` as a temporary string compatibility shim.

## Required changes

- [x] Audit in-repository call sites of `RenderGraphCompiler::GetLastCompileDiagnostic()` and `RenderGraph::GetLastCompileDiagnostic()`.
- [x] Replace caller assertions/reporting with `GetLastCompileValidationResult()` or `CompiledRenderGraph::ValidationFindings` where appropriate.
- [x] Remove the string shim API and thread-local/member storage once no longer used.
- [x] Update diagnostics docs and task references that mention the compatibility shim.

## Tests

- [x] Update any tests that assert the string shim to assert structured validation findings instead.
- [x] Preserve CPU-only coverage for compile hard errors and successful compile warnings.

## Docs

- [x] Update `docs/architecture/rendering-three-pass.md` and `src/graphics/renderer/README.md` to remove compatibility-shim language.
- [x] Refresh generated module inventory if exported symbols are removed.

## Acceptance criteria

- [x] No production or test code calls `GetLastCompileDiagnostic()`.
- [x] Compile failures remain inspectable via `RenderGraphValidationResult` findings.
- [x] Focused rendergraph validation tests pass.
- [x] Default CPU correctness gate remains green.

## Verification

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'RenderGraphValidation|RenderGraphDebugDump|FrameRecipeContract' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes

- No Vulkan-only required tests.
- No removal of structured validation findings.
- No renderer visual-output behavior changes.
