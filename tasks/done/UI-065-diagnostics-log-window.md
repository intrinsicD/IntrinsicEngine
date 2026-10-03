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

## Completion — 2026-10-03

Implementation commit: `1606d469e`; tasks activated before implementation in `51f49207d`.

View Diagnostics / Log uses the runtime stream for filtered log pages, device state and operation history. Tests exercise registration, exact filters, Clear followed by filter changes, cursor reset and auto-scroll without a presented Vulkan frame.

Verification: `ci` configure and `IntrinsicTests` build succeeded; final full CPU
run selected 5,808 tests: 5,807 passed, zero failed, one GLFW/LSan lifecycle test
skipped. The focused inspection/diagnostics suites passed 184/184; supplemental
locality/runtime/socket suites passed 84/84; Python MCP bridge passed 21/21.
Strict layering, test layout, task policy and documentation links passed;
source-documentation scan found zero objective errors. Module inventory refreshed.

Review: Claude Opus 5.5 (`xhigh`) approved the corrected fixed diff
`a729e0f1e80d3bb9d1817bdfcf70e36c2bb2324435009a14d271ff30fade8fe3`.
Its findings and the follow-up numerical/allocation issues were corrected and
verified. The full gate also exposed pre-existing BUG-234, fixed and separately
approved before the successful full rerun. CPU evidence does not substitute for
separate sanitizer or live Vulkan gates.

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
- [x] Window registered; filters and auto-scroll work; the window keeps its own cursor.
- [x] ImGui test: emit tagged log lines, filter by category, clear, and see new entries afterwards; Null device status shown.
- [x] `docs/architecture/sandbox-editor-feature-boundaries.md` window list updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(SandboxDiagnostics|DiagnosticsStream)\.'
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
