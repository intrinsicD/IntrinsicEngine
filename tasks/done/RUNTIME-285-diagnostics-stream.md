---
id: RUNTIME-285
theme: F
depends_on: [CORE-011, RUNTIME-287]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: operator-directed interactive implementation; diff, focused tests, combined verification and Claude review provide evidence.
contract_schema: 1
contracts: [repo.source-documentation, runtime.render-diagnostics-locality]
---
# RUNTIME-285 — Diagnostics stream (log cursor, device status, operation records)

## Completion — 2026-10-03

Implementation commit: `1606d469e`; tasks activated before implementation in `51f49207d`.

The Engine-owned stream shares log snapshots, truthful device status and 256 invocation records with UI/MCP. Deferred completion/abandonment updates one record, observer polling is excluded, and allocation intervals use the existing telemetry owner's new cumulative counter across frame resets.

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
- Provide one runtime diagnostics snapshot readable by cursor: new log entries,
  device status (requested/actual backend, fallback, validation errors) and a ring of
  recent operation records, for the Diagnostics window (UI-065) and agents.

## Non-goals
- No new logging API or telemetry framework; no per-panel instrumentation sweep.

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Sources: `Core::Log` cursor stream (CORE-011); `Runtime.DeviceBootstrap.cppm` `RuntimeDeviceSelection{UsePromotedVulkanDevice, FallsBackToNullDevice}`; device diagnostics snapshot field `VulkanValidationErrorCount`; per-operation `RequestedBackend`/`ActualBackend`/`BackendFallbackReason` on results; `Core::Telemetry` (`Alloc::SnapshotCumulativeBytes`, preserved across frame resets); `RenderGraphFrameStats` fallback counters.
- Operation records are appended by `Runtime.AgentOperations` invocation (agent and batch lanes) and by the family apply results where cheap; start with agent/batch lanes.

## Control surfaces
- Config: N/A.
- UI: Diagnostics / Log window (UI-065).
- Agent/CLI: `diagnostics_read {since_cursor, levels[], categories[], limit ≤ 1000}` (read-only, returns `next_cursor`) and `device_status` (read-only) in `Runtime.AgentOperations`.

## Required changes
- [x] `Runtime.DiagnosticsStream.cppm` + implementation: `ReadEditorDiagnostics(cursor, filter)` → log entries, `DeviceStatus{RequestedBackend, ActualBackend, FallbackReason, IsOperational, ValidationEnabled, ValidationErrorCount}`, `OperationRecords` (name, source Editor/AgentCli, wall time µs, allocation delta, requested/actual backend, status) in a bounded ring.
- [x] `AgentOperations` invocation appends records; agent operations registered.

## Tests
- [x] Contract: cursor paging and filters over injected log entries; device status on Null composition (`Test.RuntimeDeviceSelection.cpp` extension); operation record appended per agent invocation.

## Docs
- [x] `docs/architecture/agent-control-lane.md` and `docs/architecture/runtime.md` diagnostics notes; module inventory regenerated.

## Acceptance criteria
- [x] UI and agent read the same snapshot; cursors never skip or duplicate entries (wrap reported as dropped count).
- [x] Device fallback and validation error counts are visible without reading logs.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicCoreTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(DiagnosticsStream|RuntimeDeviceSelection|AgentOperations|LogRingBuffer)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Toggling validation layers or debug settings from this surface (ARCH-019 exclusions).

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.
