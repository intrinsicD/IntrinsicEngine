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
contracts: [runtime.render-diagnostics-locality]
---
# UI-065 — Diagnostics / Log window

## Goal
Give users an in-editor log with level/category filters, a device-status header and
a recent-operations table.

## Context
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
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|DiagnosticsWindow|DiagnosticsStream' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Reading `Core::Log` directly from app code.
