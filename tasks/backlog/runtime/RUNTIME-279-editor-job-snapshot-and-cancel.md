---
id: RUNTIME-279
theme: F
depends_on: [RUNTIME-287]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive runtime slice; evidence is the diff, session-lifecycle contract tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality]
---
# RUNTIME-279 — `EditorJobCommandSurface::SnapshotAll` and `Cancel`

## Goal
Expose all editor-owned jobs and a cancel command through the existing editor job
surface, for the Jobs window (UI-060) and agent job operations.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Exists: `Runtime.JobService.cppm` (`JobState`, `JobSnapshot{Token, DebugName, State, Scope, Progress, ElapsedMilliseconds}`, `SnapshotAll()`, `Cancel(token)`); `Runtime.EditorJobProjection.cppm` (`EditorJobRecord{Token, Identity{EntityId, Scope, OutputSemantic, OutputName}, Name, State, Requested/ResolvedJobDomain, Dependencies, NormalizedProgress, ElapsedMilliseconds, Diagnostic}`, `EditorJobCommandSurface{Submit, FindActive, SnapshotEntity}`) bound in `src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp` with a token→identity map.
- Only per-entity "Derived jobs" bullets exist in the Inspector (`Sandbox.EditorShell.cpp`); no cancel surface.

## Control surfaces
- Config: N/A.
- UI: Jobs window (UI-060).
- Agent cancellation: `notifications/cancelled` (RUNTIME-312 slice 5) currently only silences the reply of a continuation call (the entry stays a tombstone counted against the pending cap until its continuation ends), and progress notifications already follow the call's own run (RUNTIME-312 slice 8, UI-069); once `Cancel` lands, that path should call it for the dropped continuation's job, which `AgentOperationOutcome::Progress` already identifies per call.
- Agent/CLI: `jobs_list` (read-only), `jobs_wait {token|identity, timeout_ms ≤ 60000}` (read-only; returns when finished or after the timeout while frames keep running), `jobs_cancel {token}` (mutating) in `Runtime.AgentOperations`.

## Implementation notes (2026-10-02, for review)
- Surface: `SnapshotAll()` and `Cancel(JobToken) -> EditorJobCancelStatus` on `EditorJobCommandSurface`, bound in the workspace session next to `SnapshotEntity` behind the attachment epoch; panels/agent reach them via `GetEditorJobs` / `CancelEditorJob(EditorProcessingCommands, ...)`. `SnapshotAll` lists only the identity-index jobs, i.e. exactly what `Cancel` accepts. `FindEditorOperationRun` (factored out of `ResolveEditorOperationProgress`) resolves an output key to its run's token for `jobs_wait`.
- K-Means and consolidation jobs are cancellable at the `JobService` level, but they are submitted by their services with a correlation id and never enter the editor identity index, so `Cancel` and `jobs_cancel` refuse them (`not_editor_job`) per "Forbidden changes"; an MCP-cancelled `run_kmeans`/`run_point_cloud_consolidation` keeps its tombstone until the service run completes. Scene save/load and asset jobs likewise.
- `JobService::Cancel` sets the flag on any non-terminal job (queued, dependency-blocked, running, awaiting gate/apply); the drain checks it before the readiness gate and before `ValidateBeforeApply`/`PublishCompletion`, so nothing publishes, and the unpublished finalizer runs exactly once (GPU transactions discard there). Editor command results report a cancelled job with their existing stale/not-applied status and message (there is no `EditorCommandStatus::Cancelled`).
- Review follow-ups (2026-10-02): `notifications/cancelled` cancels by the run's outputs at cancel time (`CancelEditorRuns`), so later same-output stages are reached; a cancelled `jobs_wait` ends at the next poll; a wait whose seen job was reaped between polls answers `finished`/`reaped`; runs ended by a cancel answer `cancelled`. CPU-only evidence: no CPU editor op queues a later stage, so the later-stage path is tested on the harness surface (`CancelEditorOutputRuns`).
- Operational follow-up (not claimed here): cancelling a GPU transaction parked in `AwaitingApply` (and its later Accept stage) needs a `gpu;vulkan` smoke that runs a Vulkan property smoothing or outlier run, cancels it through `jobs_cancel` while the readback is pending, and reads back that the previous output and the ring are unchanged. Owner: [RUNTIME-311](RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md), which owns that lifecycle.
- Agent: `jobs` became `jobs_list` (all retained jobs; token, `editor` output and `cancellable` for editor jobs). `jobs_wait` is read-only, polled as a continuation, and `NeedsPresentedFrame` so a minimize ends it; `jobs_cancel` is mutating and not destructive (nothing published, nothing outside the undo history changes). `notifications/cancelled` runs the call's `AgentOperationOutcome::Cancel`, which `FinishApply` sets to cancel every job the command queued through the surface.

## Acceptance criteria
- [ ] `SnapshotAll` and `Cancel(JobToken)` added to `EditorJobCommandSurface` and bound next to `SnapshotEntity`, attachment-epoch guarded.
- [ ] `Cancel` delegates to `JobService::Cancel` only for tokens in the editor identity map (asset decode and other non-editor jobs cannot be cancelled through it).
- [ ] Agent operations registered; `jobs_wait` never blocks the main thread.
- [ ] `Test.SandboxEditorSessionLifecycle.cpp` / `SandboxEditorJobHarness`: snapshot contents, cancel-through-surface, refusal for non-editor tokens and stale epochs.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorSessionLifecycle|EditorJob|AgentOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Cancelling jobs outside the editor identity map; a second job registry.
