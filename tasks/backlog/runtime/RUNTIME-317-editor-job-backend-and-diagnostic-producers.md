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

## Acceptance criteria
- [ ] For CPU-only, GPU-requested-and-run, and GPU-requested-but-fell-back runs, `SnapshotAll` reports the right requested and resolved domain plus the diagnostic text. Contract tests use the job harness.
- [ ] The Jobs window and `jobs_list` show the values, with "-" only when the value is truly unknown.
- [ ] No per-operation copies; the shared helpers own the plumbing.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorSession|EditorJob|SandboxJobsWindow|AgentOperations|QueuedEditorJob' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```
