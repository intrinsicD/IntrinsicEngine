---
id: UI-060
theme: F
depends_on: [RUNTIME-279]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive UI slice; evidence is the diff, the ImGui window test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; a window over the RUNTIME-279 job surface changes no property binding, publication, module interface or format contract.
---
# UI-060 — Jobs window

## Goal
Show every running and recent editor job with progress and a Cancel button.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Data and cancel from `EditorJobCommandSurface::SnapshotAll/Cancel` (RUNTIME-279); reuse the AssetIO queue progress text style in `Sandbox.EditorShell.cpp`; `ToString(JobState)`.

## Control surfaces
- Config: N/A.
- UI: `view.jobs` ("Jobs") under View.
- Agent/CLI: `jobs_list`/`jobs_wait`/`jobs_cancel` (RUNTIME-279).

## Acceptance criteria
- [ ] Table: name, entity, output, state, progress bar, elapsed, requested/resolved backend domain, diagnostic; Cancel per active row; collapsed `JobServiceStats` counters.
- [ ] ImGui test drives a long editor job (`SandboxEditorJobHarness`), sees the row, presses Cancel and observes the cancelled state.
- [ ] `docs/architecture/sandbox-editor-feature-boundaries.md` window list updated.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxProcessingPanels|SandboxEditorSessionLifecycle|JobsWindow' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Calling `JobService` directly from the app.
