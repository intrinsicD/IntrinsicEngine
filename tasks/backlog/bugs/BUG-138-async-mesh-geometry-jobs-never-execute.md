---
id: BUG-138
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug138"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T11:29:22Z"
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# BUG-138 — Async mesh geometry jobs never execute and stay Pending forever

## Progress — slice A landed 2026-08-08 (task stays open)

### What the triage evidence actually shows

Two of the reported observations do not survive checking, and saying so
narrows the search:

- **"No worker is executing the job" is not established by the CPU reading.**
  `~95%` on the main thread is what `ExtrinsicSandbox` always shows: the frame
  loop spins flat out whenever the display is being scanned out. It is not
  evidence of a stall. (`BUG-143` measured the same loop at 1 Hz when the
  display is DPMS-off, which is how much the host's state moves this number.)
- **The submit → dispatch → worker → drain → publish path is not broken.**
  `SandboxEditorUi.MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply`
  already drives a real `JobService` through the real
  `ApplyEditorMeshSimplifyCommand` and asserts `Published` plus a reduced face
  count, and it passes. Whatever fails in the live session is not generic
  dispatch.

### The defect this slice fixes

`MakeMeshCpuJobDesc` set `Work`, `ValidateBeforeApply`, and
`PublishCompletion`, but **not** `FinalizeUnpublishedOnMainThread`. Every
other queued runtime owner sets it — scene documents, asset import, clustering,
point-cloud consolidation — because `JobService` runs exactly one of
`PublishCompletion` or that finalizer per job, and the three non-publishing
terminal states (`Cancelled`, `Dropped`, `StaleDiscarded`) take the finalizer
path.

So a mesh CPU job that was refused told the editor **nothing at all**. The
panel kept the "`Mesh simplify CPU job queued (job 4:1).`" string it was handed
at submit time and displayed it forever. That is precisely the reported
symptom, and it is indistinguishable — from the UI — from a job that never ran.
It also means the live session could not report *why* the operation was
refused, which is why the original triage had nothing to go on.

Three of the four required changes are now met:

- The job records the apply gate's verdict in its shared state, and the
  finalizer publishes one terminal result on that kind's result sink with an
  actionable reason: mesh changed after queueing, entity gone, world retired,
  or terminated without publishing.
- `IsActiveEditorJobState` is false for every terminal state, so the owning
  action re-enables; the new tests assert that directly.
- Remesh, subdivide, and simplify share `MakeMeshCpuJobDesc`, so all five mesh
  CPU kinds are covered by one fix.

`SandboxEditorUi.MeshSimplifyStaleDiscardReportsTerminalResultInsteadOfStayingPending`
fails against the unfixed source with exactly the reported symptom ("A
stale-discarded mesh job published nothing, so the editor is still showing its
submit-time Pending message"), so it discriminates the two behaviours rather
than describing one.

### What is still open

The **cause of the refusal in the live session is not yet identified**, and
this slice does not claim it. Reproducing it needs a live `Engine::Run()`
session driving the real `EditorWorkspaceSession` against an imported mesh; the
existing contract harness deliberately does not cover session-level attachment
epochs and world scoping. The leading hypothesis, untested, is that the three
topology kinds fail `ValidateMeshCpuJobApply`'s extra
`SameMeshTopologyState(view, job.BeforeMesh)` check — or its geometry metadata
signature — on an imported mesh, while curvature and denoise, which skip that
check, publish normally.

With this slice landed, the live session now *names* that reason in the panel
instead of showing `Pending`, which is the cheapest way to finish the
diagnosis. Re-run the reported repro and read the message.

Still owed for closure: the identified cause, the fix that makes the three
operations complete, and the `Operational` evidence from a live session that
the task's Maturity section requires.

## Goal
- Make queued mesh simplify, subdivide, and remesh jobs actually execute,
  complete, publish their result, and re-enable their editor action — or fail
  closed with an actionable diagnostic instead of hanging.

## Non-goals
- No change to the simplify/subdivide/remesh algorithms themselves.
- No conversion of these operations to synchronous execution as the fix; the
  async path is the intended design and should work.
- No new job-system abstraction.

## Context
- Symptom: in `ExtrinsicSandbox`, `Mesh / Processing / Simplify`, `Subdivide`,
  and `Remesh` each queue a CPU job and then remain `Pending` indefinitely
  (observed > 90 s each, never completing):
  - `Mesh simplify CPU job queued (job 4:1)`
  - `Mesh subdivide CPU job queued (job 5:1)`
  - `Mesh remesh CPU job queued (job 6:1)`
- While pending, `top -H` on the process shows ~95% CPU on the **main thread
  only**; all 40 other threads are idle, so no worker is executing the job.
- The action button disables itself while a job is active, so after the first
  attempt the operation becomes permanently unavailable for that entity for the
  rest of the session.
- Synchronous editor geometry operations on the same selection *do* run and
  apply in the same session — mesh curvature, mesh denoise, and K-Means all
  complete. The defect is specific to the queued/derived-job path.
- Known-good plumbing, so the break is between submission and worker dispatch:
  `JobCommands.Submit` returns a valid token
  (`Runtime.EditorWorkspaceSession.cpp:416-426`, which sets
  `desc.Scope = m_Worlds->ActiveWorld()` before `m_Jobs->Submit`), and
  `JobService::DrainCompletions` is called from the frame loop
  (`Runtime.Engine.cpp:817`). `JobService` exposes `LastDrainParked`,
  `LastDrainStaleDiscarded`, and `LastDrainFinalizedUnpublished` counters
  (`Runtime.JobService.cppm:298-316`) that were not inspected during triage and
  are the natural first probe.
- Impact: three of the five core mesh-editing operations are unusable. Combined
  with the absent export path (`UI-046`), no destructive mesh edit can be
  completed and saved today.
- Owner: `runtime` kernel `JobService` plus the editor geometry-processing
  operation surface. Reproduced on the `ci-vulkan` build with the promoted
  Vulkan device operational; not a graphics-backend issue.

## Required changes
- [ ] Instrument or inspect the `JobService` drain counters
      (`LastDrainParked`, `LastDrainStaleDiscarded`,
      `LastDrainFinalizedUnpublished`, `CompletedJobs`) for a queued mesh
      simplify job to establish where the job stalls. Slice A made the refusal
      reason visible in the panel instead; the live counter reading is still
      owed.
- [ ] Fix the dispatch/completion break so the job runs on a worker and its
      result is published to the entity.
- [x] Ensure a job that cannot be dispatched fails closed with a distinct,
      actionable diagnostic instead of remaining `Pending` forever. Slice A:
      `FinalizeUnpublishedOnMainThread` publishes one terminal result naming
      the apply gate's verdict.
- [x] Ensure the owning editor action re-enables on terminal job state
      (success, failure, or cancellation). Asserted through
      `IsActiveEditorJobState` on the discarded records.

## Tests
- [ ] Add a runtime contract test that submits a mesh simplify job through the
      editor operation surface, drains frames, and asserts the job reaches a
      terminal state and publishes the expected face count.
- [x] Add equivalent coverage for subdivide and remesh
      (`MeshRemeshAndSubdivideStaleDiscardsReportTerminalResults`).
- [x] Add a test asserting the action's disabled state clears on terminal job
      state.
- [x] Default CPU gate stays green (slice A).

## Docs
- [x] Record the queued-job lifecycle expectation (submit → dispatch → drain →
      publish → re-enable) in the owning runtime doc
      (`src/runtime/README.md`, `Extrinsic.Runtime.GeometryProcessingOperations`).

## Acceptance criteria
- [ ] Simplify, subdivide, and remesh complete and change the selected mesh in
      a live `ExtrinsicSandbox` session.
- [ ] No editor geometry action can be left permanently disabled by a job that
      never reaches a terminal state.
- [ ] The stall cause is recorded in this task, not just worked around.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'GeometryProcessing|JobService|SandboxEditorMeshMethods' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- Making the operations synchronous to hide the dispatch defect.
- Adding a timeout that cancels the job without explaining why it never ran.
- Re-enabling the action on a non-terminal state.

## Maturity
- Target: `Operational` — the fix is only proven when a queued job completes in
  a live `Engine::Run()` session, not merely when a contract test drains a
  fake queue.
