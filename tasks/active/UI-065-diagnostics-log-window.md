---
id: UI-065
theme: F
depends_on: [RUNTIME-285]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui window test, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, runtime.render-diagnostics-locality]
---
# UI-065 — Diagnostics / Log window

## Goal
Give users an in-editor log with level/category filters, a device-status header and
a recent-operations table.

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Data from `ReadEditorDiagnostics` (RUNTIME-285); nothing in `src/app` reads the core log today.

## Control surfaces
- Config: N/A.
- UI: `view.diagnostics` ("Diagnostics / Log"): level/category filter, auto-scroll, Clear (calls `ClearEntries`; sequence continues), device status header (requested vs actual backend, validation count), recent operations table.
- Agent/CLI: `diagnostics_read`/`device_status` (RUNTIME-285).

## Acceptance criteria
- [ ] Window registered; filters and auto-scroll work; the window keeps its own cursor.
- [ ] ImGui test: emit tagged log lines, filter by category, clear, and see new entries afterwards; Null device status shown.
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` window list updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxDiagnostics|DiagnosticsWindow|DiagnosticsStream)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Reading `Core::Log` directly from app code.

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.
