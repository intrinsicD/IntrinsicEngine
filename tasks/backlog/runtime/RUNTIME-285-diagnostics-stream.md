---
id: RUNTIME-285
theme: F
depends_on: [CORE-011, RUNTIME-287]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, runtime.render-diagnostics-locality]
---
# RUNTIME-285 — Diagnostics stream (log cursor, device status, operation records)

## Goal
- Provide one runtime diagnostics snapshot readable by cursor: new log entries,
  device status (requested/actual backend, fallback, validation errors) and a ring of
  recent operation records, for the Diagnostics window (UI-065) and agents.

## Non-goals
- No new logging API or telemetry framework; no per-panel instrumentation sweep.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Sources: `Core::Log` cursor stream (CORE-011); `Runtime.DeviceBootstrap.cppm` `RuntimeDeviceSelection{UsePromotedVulkanDevice, FallsBackToNullDevice}`; device diagnostics snapshot field `VulkanValidationErrorCount`; per-operation `RequestedBackend`/`ActualBackend`/`BackendFallbackReason` on results; `Core::Telemetry` (`Alloc::SnapshotBytes`); `RenderGraphFrameStats` fallback counters.
- Operation records are appended by `Runtime.AgentOperations` invocation (agent and batch lanes) and by the family apply results where cheap; start with agent/batch lanes.

## Control surfaces
- Config: N/A.
- UI: Diagnostics / Log window (UI-065).
- Agent/CLI: `diagnostics_read {since_cursor, levels[], categories[], limit ≤ 1000}` (read-only, returns `next_cursor`) and `device_status` (read-only) in `Runtime.AgentOperations`.

## Required changes
- [ ] `Runtime.DiagnosticsStream.cppm` + implementation: `ReadEditorDiagnostics(cursor, filter)` → log entries, `DeviceStatus{RequestedBackend, ActualBackend, FallbackReason, IsOperational, ValidationEnabled, ValidationErrorCount}`, `OperationRecords` (name, source Editor/AgentCli, wall time µs, allocation delta, requested/actual backend, status) in a bounded ring.
- [ ] `AgentOperations` invocation appends records; agent operations registered.

## Tests
- [ ] Contract: cursor paging and filters over injected log entries; device status on Null composition (`Test.RuntimeDeviceSelection.cpp` extension); operation record appended per agent invocation.

## Docs
- [ ] `docs/architecture/agent-control-lane.md` and `docs/architecture/runtime.md` diagnostics notes; module inventory regenerated.

## Acceptance criteria
- [ ] UI and agent read the same snapshot; cursors never skip or duplicate entries (wrap reported as dropped count).
- [ ] Device fallback and validation error counts are visible without reading logs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'DiagnosticsStream|RuntimeDeviceSelection|AgentOperations|Logging' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Toggling validation layers or debug settings from this surface (ARCH-019 exclusions).
