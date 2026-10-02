---
id: RUNTIME-311
theme: I
depends_on: [RUNTIME-298]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive follow-up; evidence is the diff, tests, and CI
contract_schema: 1
contracts: [geometry.property-coherence, geometry.element-domain-sources, repo.source-documentation]
---
# RUNTIME-311 — Unify the two-phase GPU Run/Accept transaction lifecycle (one lifecycle generic over N rings)

## Goal
Replace the five hand-copied two-phase GPU Run/Accept transaction lifecycles
(scalar, outlier, vertex normals, property smoothing, GPU positions) with one
runtime-owned lifecycle over N output rings. Keep typed method calculation,
statistics, and publication adapters at their existing owners.

## Context
- RUNTIME-313 hand-offs: (1) Coherent Point Drift keeps its own run object (`Busy` flag, deliver-once
  flag, interactive steps without a completion callback); decide whether it joins this lifecycle or
  stays a run object, and record why. (2) Device-gated checks a headless session cannot reach: a
  duplicate GPU start (scalar, keypoint resident, outliers, normals, smoothing) answers Pending without
  a handle or callback, and every GPU Accept stage carries the run's `EditorJobIdentity::Run`; pin both
  on a Vulkan host.
- RUNTIME-279 follow-up: add a `gpu;vulkan` smoke that cancels a Run/Accept transaction parked in `AwaitingApply` through the editor job surface (`jobs_cancel`) and reads back that the previous output and ring are unchanged; RUNTIME-279 proved this path on the CPU gate only.
RUNTIME-298 review found drift in ring-generation validation and Accept callback
handling. Those defects are fixed there; this task owns the deferred unification.
The 2026-10-01 duplication audit (re-verified at `665c693dd`) found the same
helper set copied in five files, not two:

| Transaction | File |
| --- | --- |
| Point scalar (density/spacing/weights/keypoints) | `Runtime.PointScalarTransaction.cpp` |
| Outliers | `Runtime.GeometryProcessingOperations.Outliers.cpp` |
| Vertex normals | `Runtime.GeometryProcessingOperations.Normals.cpp` (`BeginAccept`, accept job, run job) |
| Property smoothing | `Runtime.MeshFieldOperations.Smoothing.cpp` (`BeginAccept`, accept job, run job) |
| GPU positions (accept-only variant) | `Runtime.GeometryProcessingOperations.GpuPositions.cpp` |

All under `src/runtime/Editor/Operations/`. The copied pieces are `Current`, `Deliver`,
`Finish(phase, status, message)`, `Poll`, `CompleteRun`, `CompleteAccept`, `BeginAccept` and the
AutoAccept tail inside `PublishCompletion`. The Normals and Smoothing AutoAccept tails were
byte-identical at the audit revision; re-confirm before editing.

Reuse `Runtime.PointScalarTransaction` and the outlier transaction's existing
capture, readback, generation and guarded-delivery mechanisms, and
`GpuFrontReadback`/`PollGpuFrontReadback` (`Runtime.GeometryProcessingOperations.GpuFront.hpp`)
as the readback primitive. Do not introduce a service, registry, additional job hop or
public template framework. GpuPositions has no Run phase; the lifecycle must express an
accept-only transaction without a dummy run.

Sequencing: land the shared lifecycle with scalar and outliers first, then migrate normals,
smoothing and GPU positions as separate reviewed slices. The Sandbox row that drives these
transactions is [UI-071](../ui/UI-071-gpu-transaction-controls-and-refusal-presentation.md);
the per-job setup/completion prologue that precedes them is
[RUNTIME-313](../../done/RUNTIME-313-queued-editor-job-setup-and-completion-helper.md).

## Progress and recorded behaviour changes
- Owner: `src/runtime/Editor/Operations/Runtime.GpuTransactionLifecycle.{hpp,cpp}`. `GpuTransactionCore`
  is embedded by value in each typed transaction (rings with captured generations, readbacks,
  phase, Abandoned/Delivered/Publishing, run and accept tokens, `AcceptJobName`) with typed hooks
  (`Current`, `Poll`, `CompleteRun`, `CompleteAccept`, `Release`, `Deliver`). Slices: 1 scalar (typed
  Start and publication mode), 2 outliers, 3 normals, 4 smoothing, 5 GPU positions (accept-only:
  the core starts in ReadyToAccept with its ring acquired, no Run job).
- Shared semantics, recorded as changes where a method differed before:
  - Start refusal order is: active job on the output (Pending, shared 313 wording), then no residency,
    then a ring of the output awaiting Accept/Discard (InvalidProcessingParameters, "A GPU result for
    this output awaits Accept or Discard."). Outliers checked the residency and the ring before the
    active job; normals already used this order.
  - Cancel or stale finalize of either job: "<label> cancelled or stale; previous output retained."
    (StaleEntity, Discarded); before, every method had its own wording and only normals, smoothing
    and positions set `Abandoned`.
  - Accept while one is under way: Pending without a sink, InvalidProcessingParameters with one (that
    sink is never called); before scalar, outliers and normals answered InvalidProcessingParameters.
  - A refused automatic Accept ends Discarded when stale, otherwise Failed (scalar and outliers always
    ended Failed); the Run job then counts as not published (JobState `Dropped`), as does a run that
    ended in its own completion (normals' "no preview" previously counted as published).
  - A Discard issued from a history observer while Accept publishes is ignored for every method
    (only scalar had this guard; outliers and normals delivered Discarded mid-publication).
  - Discard keeps its typed status (scalar/outliers NoChange, normals/smoothing/positions StaleEntity).
  - A rejected Run submission closes the transaction without calling the sink (the immediate answer
    reports it); a rejected Accept submission delivers its failure once.
  - A rejected job-lane submission (Run or Accept) is worded by `MeshSupport::QueuedJobRejectedMessage`
    with the operation's job label ("Normal estimation job submission was rejected.", "... (Accept)."),
    as are the keypoint resident start and the positions Accept; before each method had its own
    ("Vulkan normals submission rejected.", "Scalar Accept submission rejected." ...). The drift guard
    rejects other hand-written job "submission rejected/refused" wording in the operations.
  - Accept of a front that is no longer resident says "previous output retained." (normals and
    smoothing said "previous normals retained." / their own wording).
  - Discard of a transaction waiting in ReadyToAccept now sets `Abandoned` for every method (before
    only while Running/Accepting for normals/smoothing/positions; not at all for scalar/outliers).
  - Workspaces: recorders capture their own workspace leases (the spatial cache keeps a recorder
    until its readback is safe), so a run discarded or cancelled while device work is in flight
    returns its workspace at once. Before, `Release` kept it unless the work was Ready and nothing
    retried later, so the run held it until its handle was dropped.
  - Smoothing: Stop before the first preview ends the run, from the Run's publication, as
    Discarded/NoChange ("stopped before a preview"); before it ended Discarded/StaleEntity. Not
    reachable deterministically on a device (the first chunk stores a preview whenever it holds a
    write slot), so it has no smoke; CPU contract tests cannot drive the Vulkan Run.
  - GPU positions: Accept while one is under way with a callback is refused (before: Pending without
    taking the callback); a Discard from a history observer during publication is ignored.
    `EditorGpuPositionRunCurrent` keeps its meaning (the positions read are unchanged); a ring that
    left the residency, or was replaced by another run's ring, fails Accept as "no longer resident"
    (GeometryProcessingFailed, delivered once) without touching the successor's ring.
  - GPU positions render commit: a pending block copy (`AcknowledgedCopyPending`) holds the accepted
    front's residency lease (`GeometryPositionCommitDesc::SourceLease`) until the frame that recorded
    the copy completed (`GetFramesInFlight`); every place a preview stops holding it (copied,
    superseded by a CPU upload, replaced by a new preview or commit, cleared, freed) retires it, and
    Shutdown/device-loss rebuild drop the retire list. Before, only `NoteUse(frame + 1)` protected
    the slot, which a late culling head (minimized frames) outlived.
  - Shared refusal and failure wording (user visible in panels and agent replies): Accept of
    changed inputs "The inputs changed since the run; discard the result and run again." (scalar
    and outliers said "Scalar/Outlier input or output changed; discard and run again."), nothing
    waiting "No GPU result waits for Accept." (scalar "No scalar result awaits Accept.", outliers
    "No outlier result waits for Accept."), a front no longer resident at Accept "The GPU result
    is no longer resident; previous output retained." (scalar "Scalar front is no longer resident.",
    outliers "Outlier front is no longer resident."; positions keep their own wording for a front
    missing before the readback). Snapshot refusal reasons keep their typed wording.
  - Not fixed here: a failed frame submit after a recorded commit copy (BUG-232).
  - Workspace-in-flight: no smoke asserts that a workspace is reused only after its readback; the
    recorder-owned lease follows the spatial cache contract and the discard/stop smokes pass.
  - Mesh-family CPU jobs (curvature, denoise, remesh, subdivide, simplify, UV, ICP, Progressive
    Poisson) word a rejected submission through `QueuedJobRejectedMessage` ("Mesh denoise CPU job
    submission was rejected.", before "... was rejected by the runtime job lane.").
- Coherent Point Drift stays a run object (deliberate exception): it has no output ring, no front
  readback and no Accept stage; its run alternates interactive E/M steps with an optional
  completion and a `Busy` flag, and its Vulkan E-step pump is an auxiliary job. The GPU lifecycle's
  Run/Accept phases, ring generations and front readbacks do not apply; the RUNTIME-313 drift guard
  keeps it on its own allowlist entry with that reason.
- Operational evidence (Xephyr `:7`, `build/ci-vulkan`, binary run directly and checked for `[       OK ]`):
  `RUNTIME311GpuTransactionCancel.AwaitingApplyCancelThroughTheEditorJobSurfaceKeepsThePreviousOutput`
  (`tests/integration/graphics/Test.GpuTransactionCancelSmoke.cpp`, `gpu;vulkan`) cancels a Vulkan
  property-smoothing transaction through the agent's `jobs_cancel` on a real
  `EditorWorkspaceAttachment`: the chunked Run while parked in AwaitingApply, then the Accept stage
  while its front readback keeps it parked (Accept is issued from a Maintenance hook, after the
  frame's transfers were collected, so the next completion drain parks it; 0 retries in 3 runs).
  Each ends once, Discarded/StaleEntity, JobState Cancelled; the previous CPU output, its revision
  and the canonical slot are unchanged and no ring is left. On the device it also pins a duplicate
  GPU start (Pending, no handle, no callback) and the Accept stage carrying the run's
  `EditorJobIdentity::Run`. A mutation that keeps the ring on finalize fails it. The six transaction
  smokes (scalar, outlier, point normals, vertex normals, smoothing x2, positions x2) pass with it
  (full `IntrinsicPointLBVHGpuTests` at slice 6: 53 passed, 1 opt-in profile skipped).
- Pending (GPU reserved by the operator after slice 6): a Vulkan rerun of the slice-7 positions
  Accept change (missing-ring check before the Accept refusal) and of the full `gpu;vulkan` CTest
  across all binaries after the slice-5 `GpuWorld` lease change; only `IntrinsicPointLBVHGpuTests`
  ran on the device after slice 5.

## Acceptance criteria
- [ ] One compiled lifecycle owns acquisition, polling, ring publication, Accept, Discard, cancellation and terminal delivery for one or N rings, including the accept-only (no Run phase) shape.
- [ ] Every ring generation, input/output revision and workspace attachment is checked before publication; stale cleanup preserves replacement rings.
- [ ] Start and Accept callbacks have the same replacement and exactly-once semantics across methods, including rejected submissions.
- [ ] Scalar, outlier, normals, smoothing and GPU-positions panels retain complete typed results after terminal transitions, with statistics, backend identity and IO counters intact.
- [ ] Existing method math, atomic multi-output publication, Undo/Redo and renderer observation behavior are preserved; all five duplicated lifecycles (including the byte-identical AutoAccept tails) are removed.
- [ ] Coherent Point Drift's run object is either on the shared lifecycle or recorded as a deliberate exception with its reason.
- [ ] On a Vulkan host, a duplicate GPU start answers Pending without a handle or callback, and every GPU Accept stage carries the run's `EditorJobIdentity::Run`.
- [ ] CPU contracts and real-device scalar, outlier, normals, smoothing and GPU-positions transaction smokes pass (register a `gpu;vulkan` smoke for any of the five that lacks one before migrating it); documentation and module inventory describe the shared owner.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -R 'PointScalarTransaction|OutlierTransaction|NormalTransaction|PropertySmoothingTransaction|GpuPositionsAccept|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
cmake --build build/ci-vulkan -j$(nproc)
ctest --test-dir build/ci-vulkan -R 'PointScalarTransactionGpuSmoke|OutlierTransactionGpuSmoke|NormalTransaction|PropertySmoothingTransaction|GpuPositionsAccept' -L gpu -L vulkan --output-on-failure --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
