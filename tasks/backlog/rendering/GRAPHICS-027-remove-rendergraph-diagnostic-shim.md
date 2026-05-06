# GRAPHICS-027 — Remove rendergraph compile diagnostic string shim

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
