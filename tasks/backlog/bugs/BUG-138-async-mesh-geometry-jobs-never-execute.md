---
id: BUG-138
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - geometry.element-domain-sources
---
# BUG-138 — Async mesh geometry jobs never execute and stay Pending forever

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
      simplify job to establish where the job stalls.
- [ ] Fix the dispatch/completion break so the job runs on a worker and its
      result is published to the entity.
- [ ] Ensure a job that cannot be dispatched fails closed with a distinct,
      actionable diagnostic instead of remaining `Pending` forever.
- [ ] Ensure the owning editor action re-enables on terminal job state
      (success, failure, or cancellation).

## Tests
- [ ] Add a runtime contract test that submits a mesh simplify job through the
      editor operation surface, drains frames, and asserts the job reaches a
      terminal state and publishes the expected face count.
- [ ] Add equivalent coverage for subdivide and remesh.
- [ ] Add a test asserting the action's disabled state clears on terminal job
      state.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Record the queued-job lifecycle expectation (submit → dispatch → drain →
      publish → re-enable) in the owning runtime doc.

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
