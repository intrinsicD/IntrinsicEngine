---
id: RUNTIME-317
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive runtime slice; evidence is the diff, contract tests, review and CI.
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality]
---
# RUNTIME-317 — Editor job records report requested/resolved backend and diagnostic

## Goal
`EditorJobRecord` carries the requested backend, the resolved backend (after any
fallback) and the job's diagnostic for every editor operation, so the Jobs window
(UI-060) and the agent's `jobs_list` show real values instead of "-".

## Context
- Found in the UI-060 review (2026-10-02). `ToEditorJobRecord` in
  `src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp` (~191) never
  sets `RequestedJobDomain`, `ResolvedJobDomain` or `Diagnostic`.
- UI-060 shows "-" until this task lands.
- `JobDesc::Target` is always `CpuPool` for editor jobs, because GPU work runs
  through `GpuQueue` participants:
  - the requested backend is only known at each operation's submit site, from
    its backend config;
  - the resolved backend and the diagnostic are only known from the result or
    finalizer.
- Owners to reuse:
  - the RUNTIME-313 queued-job helper (`QueuedJobDelivery`, `ValidateQueuedJob`);
  - the RUNTIME-311 `GpuTransactionCore`;
  - `EditorJobIdentity`, which already carries `Run` and `Auxiliary`.
- Avoid one-off per-operation plumbing.

## Slice plan
1. Carry the requested domain on `EditorJobIdentity` (or the submit descriptor) at Submit through the shared helper.
2. Record the resolved domain and diagnostic when the finalizer or the GPU lifecycle completes, including fallbacks to CPU.
3. Adopt this in every queued operation family through the shared owners. Add a drift check if it is cheap.

## Implementation log
- Slice 1 (shared plumbing and every family):
  - Requested: `EditorJobIdentity::RequestedDomain`, set where each operation builds its identity from
    its backend config (`EditorJobDomainOfBackend(ToString(config.Backend))`; CPU-only mesh, curvature
    and UV families `Cpu`; texture bake `GpuGraphics`); `SubmitGpuTransactionRun` sets `GpuCompute`
    for every GPU transaction stage.
  - Resolved and diagnostic: `JobService::CompletingJob()` names the job whose main-thread callback
    runs. `GuardEditorProcessingResult` (every family's result wrapper, now bound without a sink
    too) reports `EditorJobOutcomeOf(result)` for that job through
    `EditorJobCommandSurface::ReportOutcome` after the sink. `GpuTransactionCore` reports its run
    explicitly (Ready: GpuCompute + "awaits Accept or Discard"; terminal: GpuCompute once a device
    result existed, else unknown, with the lifecycle message).
  - Records: the session and the job harness keep the outcome per run and build rows with
    `MakeEditorJobRecord` (a CPU request resolves to the CPU). Every job of a run shows the run's
    outcome.
  - `jobs_list` editor rows gain `requested_backend`, `resolved_backend` (`cpu`, `gpu_compute`,
    `gpu_graphics`, `auto`, null) and `diagnostic` (null when empty); `jobs_wait` rows too.
  - Drift guard `QueuedEditorJobDriftGuard.EveryEditorJobIdentityNamesItsRequestedDomain` (it found
    the resident keypoint run identity without one).
  - Behaviour change: `EditorOperationProgress::Diagnostic` (panels' progress) of a failed or
    cancelled run is the run's reported diagnostic instead of the bare job state; other runs show none.
  - Texture bake reports GpuGraphics on publication only; a failed bake still has no diagnostic
    (the bake's failure lives in the bake service, not in a delivered result).

- Review fixes (slice 1): a result resolves to a backend only when it produced its result
  (Applied/NoChange); a failed, cancelled, stale or pending result's backend fields are planned or
  defaults, so it resolves to nothing (a cancelled Vulkan point-sampling run showed `cpu`). A CPU
  request still shows CPU. The progress diagnostic is restricted to failed/cancelled runs. The
  session and harness share `RecordEditorJobOutcome`/`ToEditorJobRecord` over
  `EditorJobIdentityIndex`/`EditorJobOutcomeIndex`. GPU transactions' `Identity = {...}` sites name
  GpuCompute and the drift guard covers them. Tests: `ResidentPointSampling.CancelledVulkanRunResolvesToNoBackend`,
  `SandboxJobsWindow.ProgressCarriesTheDiagnosticOfFailedOrCancelledRunsOnly`,
  `RuntimeJobService.CompletingJobNamesTheCallbacksJobThroughNestedDrainsAndFinalizers`.

## Acceptance criteria
- [x] For CPU-only, GPU-requested-and-run, and GPU-requested-but-fell-back runs, `SnapshotAll` reports the right requested and resolved domain plus the diagnostic text. Contract tests use the job harness.
- [x] The Jobs window and `jobs_list` show the values, with "-" only when the value is truly unknown.
- [x] No per-operation copies; the shared helpers own the plumbing.

## Completion

Commit: `637707ef6`, `e47fbe7b8`. Completed 2026-10-02 with an independent Opus review.
- Full CPU suite 5730/5730.
- The requested backend comes from each operation's identity; the GPU transaction core supplies GpuCompute.
- The resolved backend and diagnostic come from the shared result guard through `JobService::CompletingJob()` and `ReportOutcome`, and only applied results resolve a backend. They are stored per run and shown in the Jobs window and in `jobs_list`/`jobs_wait`.
- A drift guard requires every identity to name its requested backend.
- Maturity: CPUContracted.
- A failed texture bake still has no diagnostic, because the failure lives in the bake service.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorSession|EditorJob|SandboxJobsWindow|AgentOperations|QueuedEditorJob' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
