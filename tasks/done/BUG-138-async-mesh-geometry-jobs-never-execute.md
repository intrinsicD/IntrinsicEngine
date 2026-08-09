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

## Status

- Completed and retired on 2026-08-09.
- Completion commit: this retirement commit.

Slice A (2026-08-08) made a refused mesh CPU job report itself instead of
sitting on its submit-time `Pending` string. That turned the reported symptom
from a silent hang into a named refusal, and the named refusal is what found
the cause.

### The cause

`ValidateMeshCpuJobApply` gated remesh, subdivide, and simplify on
`SameMeshTopologyState(view, job.BeforeMesh)`. That compares the halfedge,
edge, and face index arrays **stored in GeometrySources** against
`job.BeforeMesh`, which is those same sources **re-derived** at submit time
through `BuildMeshSoupFromGeometrySources` → `ToHalfedgeMesh`. The two agree
only when the stored numbering happens to be what a soup round-trip produces.

An import populates the sources from exactly such a conversion, so the first
topology edit on a freshly imported entity passed the gate and applied. That
edit then publishes its own mesh through `GS::PopulateFromMesh`, whose
numbering is whatever the edit plus `GarbageCollection` produced — a different
representation of the same mesh. Every later remesh/subdivide/simplify on that
entity therefore compared a soup re-derivation against a post-edit numbering,
found them unequal, and returned `StaleGeneration`. The mesh had not changed;
only the way it was written down had. The same comparison guarded the
command-history revalidation inside `CommitMeshTopologyReplacement`, so undo
and redo of a topology edit were exposed to it too.

This is why the report's operation split was exactly three-and-two: curvature
and denoise skip the topology comparison and were unaffected, and the live
session confirmed it — denoise applied on the very entity where simplify was
refused.

The original session hit the refusal on its *first* attempt rather than its
second because the deferred UV atlas replaces the imported halfedge topology
before the first edit is submitted (`BUG-137`), which is the same
representation mismatch arriving earlier.

Two of the report's own observations did not survive checking, and saying so
narrows the search for the next reader:

- **"No worker is executing the job" is not established by the CPU reading.**
  `~95%` on the main thread is what `ExtrinsicSandbox` always shows while the
  display is being scanned out (`BUG-143` measured the same loop at 1 Hz with
  the display DPMS-off). Dispatch was never broken; the worker ran and the
  result was discarded at the apply gate.
- **The submit → dispatch → worker → drain → publish path was not broken.**
  `MeshSimplifyRequestQueuesDerivedJobAndPublishesOnApply` already drove it end
  to end and passed, because it only ever ran one edit per entity.

### The fix

The apply gate now answers "do the stored sources still hold the mesh this job
was computed from" by reading the same data on both sides:
`MeshTopologyValueSignature` fingerprints the stored `e:v0`/`e:v1`,
`h:to_vertex`/`h:next`/`h:face`, and `f:halfedge` arrays plus their sizes and
deleted counts, taken at submit and recomputed at apply. It is
representation-independent and still detects every real change. The vertex half
of the old comparison survives as `SameMeshVertexState`, because vertex
numbering *does* survive the round-trip. The command-history generation carries
the same fingerprint and restamps it from the sources the apply just wrote, so
undo and redo compare against what is stored rather than a re-derivation of it.

### Evidence

`MeshSimplifyAppliesAgainAfterAnEarlierTopologyEdit` and
`MeshSubdivideAndRemeshApplyAfterAnEarlierSimplify` both fail against the
unfixed source with the live session's exact wording — "the mesh changed after
the job was queued, so the result no longer matches the geometry it was
computed from" — and pass with it.

Live `ci-vulkan` session on a nested `Xephyr` display with the promoted Vulkan
device, one imported `tests/data/sculpt.obj`, five consecutive topology edits,
every one `Applied`:

| # | Operation | Faces |
| --- | --- | --- |
| 1 | Simplify (FA-QEM, target 4000) | 7342 → 4000 |
| 2 | Simplify (target 2000) | 4000 → 2000 |
| 3 | Simplify (target 1000) | 2000 → 1000 |
| 4 | Subdivide (Loop, 1 iteration) | 1000 → 4000 |
| 5 | Remesh (uniform, 1 iteration) | applied |

Before the fix the same session refused edits 2 onward with `StaleEntity` and
left the action re-enabled but permanently unusable. Screenshots are in
`tasks/evidence/BUG-138/artifacts/`.

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
- [x] Establish where the job stalls. The drain counters were not the probe
      that answered it: slice A's terminal result named the refusal in the
      panel, and a live A/B against denoise — which runs the same staleness
      checks minus the topology comparison, and applied on the same entity —
      isolated the failure to `SameMeshTopologyState`. Dispatch was never
      broken, so there were no parked or undispatched jobs to count.
- [x] Fix the break so the job's result is published to the entity. The gate
      now compares the stored topology against the stored topology
      (`MeshTopologyValueSignature`) instead of against a soup re-derivation of
      it, and the command-history revalidation does the same.
- [x] Ensure a job that cannot be dispatched fails closed with a distinct,
      actionable diagnostic instead of remaining `Pending` forever. Slice A:
      `FinalizeUnpublishedOnMainThread` publishes one terminal result naming
      the apply gate's verdict.
- [x] Ensure the owning editor action re-enables on terminal job state
      (success, failure, or cancellation). Asserted through
      `IsActiveEditorJobState` on the discarded records.

## Tests
- [x] Add a runtime contract test that submits a mesh simplify job through the
      editor operation surface, drains frames, and asserts the job reaches a
      terminal state and publishes the expected face count
      (`MeshSimplifyAppliesAgainAfterAnEarlierTopologyEdit`, which drives two
      edits because one never reproduced the defect).
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
- [x] Simplify, subdivide, and remesh complete and change the selected mesh in
      a live `ExtrinsicSandbox` session — five consecutive edits, recorded
      under `## Status`.
- [x] No editor geometry action can be left permanently disabled by a job that
      never reaches a terminal state. Slice A's finalizer publishes one
      terminal result on every non-publishing path and
      `IsActiveEditorJobState` is false for all of them; the live session
      showed the refused action re-enabling immediately.
- [x] The stall cause is recorded above, not just worked around.

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
- Reached `Operational` on 2026-08-09; the session is recorded under
  `## Status`.
