# GRAPHICS-027 — Remove rendergraph compile diagnostic string shim

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-018Q` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-lfLwc`.
- Promotion commit: pending (this commit moves the file from `tasks/backlog/rendering/` to `tasks/active/` and redirects the rendering backlog README link).
- Implementation commit: pending. Migrate `src/graphics/renderer/Graphics.Renderer.cpp` and the test callers in `tests/contract/graphics/Test.RenderGraphValidation.cpp`, `Test.ImGuiPresentContract.cpp`, `Test.FrameRecipeContract.cpp`, `Test.PostProcessChainContract.cpp`, `Test.DebugViewContract.cpp`, `tests/unit/graphics/Test.RenderGraphDebugDump.cpp`, and `tests/integration/graphics/Test.RenderGraphLegacy.cpp` from `GetLastCompileDiagnostic()` to `GetLastCompileValidationResult()` / `CompiledRenderGraph::ValidationFindings`; remove the string shim API and `g_LastCompileDiagnostic` / `RenderGraph::Impl::LastCompileDiagnostic` storage from `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cppm`/`.cpp` and `src/graphics/framegraph/Graphics.RenderGraph.cppm`/`.cpp`; sync `src/graphics/renderer/README.md` and `docs/architecture/rendering-three-pass.md` to drop the compatibility-shim language and the `GRAPHICS-027` link.
- Task-state commit: pending retirement commit (will move the file from `tasks/active/` to `tasks/done/` and redirect the rendering backlog README entry).
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after the file move; the implementation slice runs the focused CTest filter `RenderGraphValidation|RenderGraphDebugDump|FrameRecipeContract` followed by the default CPU correctness gate from `AGENTS.md` once the build is green.

## Goal

Remove the temporary `GetLastCompileDiagnostic()` string compatibility shim once downstream callers have migrated to structured rendergraph validation findings.

## Non-goals

- No new validation codes or rendergraph behavior changes.
- No renderer feature, shader, Vulkan, or UI diagnostic work.
- No changes to `RenderGraphValidationResult` semantics beyond removing the legacy string surface.

## Context

Owned by `graphics/framegraph` and `graphics/renderer`. `GRAPHICS-022` introduced `RenderGraphValidationResult`, `CompiledRenderGraph::ValidationFindings`, `RenderGraphCompiler::GetLastCompileValidationResult()`, and `RenderGraph::GetLastCompileValidationResult()` while retaining `GetLastCompileDiagnostic()` as a temporary string compatibility shim.

## Required changes

- Audit in-repository call sites of `RenderGraphCompiler::GetLastCompileDiagnostic()` and `RenderGraph::GetLastCompileDiagnostic()`.
- Replace caller assertions/reporting with `GetLastCompileValidationResult()` or `CompiledRenderGraph::ValidationFindings` where appropriate.
- Remove the string shim API and thread-local/member storage once no longer used.
- Update diagnostics docs and task references that mention the compatibility shim.

## Tests

- Update any tests that assert the string shim to assert structured validation findings instead.
- Preserve CPU-only coverage for compile hard errors and successful compile warnings.

## Docs

- Update `docs/architecture/rendering-three-pass.md` and `src/graphics/renderer/README.md` to remove compatibility-shim language.
- Refresh generated module inventory if exported symbols are removed.

## Acceptance criteria

- No production or test code calls `GetLastCompileDiagnostic()`.
- Compile failures remain inspectable via `RenderGraphValidationResult` findings.
- Focused rendergraph validation tests pass.
- Default CPU correctness gate remains green.

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
