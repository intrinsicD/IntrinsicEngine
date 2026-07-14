---
id: RUNTIME-126
theme: B
depends_on: [GRAPHICS-096, GRAPHICS-098]
maturity_target: Operational
---
# RUNTIME-126 — GPU readback jobs and result→property write-back in the derived-job graph

## Completion
- Retired on 2026-06-25 at maturity `Operational`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: runtime readback jobs now park through `StreamingExecutor` in
  `WaitingForReadback`, resume through `DerivedJobRegistry::DrainReadbacks()`,
  write dimension-checked GPU readback bytes into typed geometry properties,
  and keep dependent follow-up jobs pending until the write-back apply completes.
- Evidence: focused `GpuReadbackJob` / `DerivedJob` /
  `VertexAttributeBinding` CPU tests, explicit readback streaming integration
  tests, the default `ci` `IntrinsicTests` build, the full CPU-supported CTest
  gate, structural validators, and the opt-in `gpu;vulkan`
  `GpuReadbackJobGpuSmoke.VulkanTransferReadbackWritesPropertyAndFollowUpUploadsDerivedColor`
  round-trip passed.

## Goal
- Close the GPU→CPU leg of the transfer foundation at the runtime scheduling
  layer: add a **readback job kind** to the existing `DerivedJobRegistry` that
  drives a GRAPHICS-096 async `DownloadBuffer`, and a **readback→property
  write-back binding** that writes the result into a target `PropertyRegistry`
  property (dimensions must match, validated through GRAPHICS-095), so an
  algorithm can chain follow-ups on a GPU-computed result — e.g. "compute on GPU
  → read back → derive a per-vertex color / vector-field property → re-upload →
  make visible" — using the same `SubmitFollowUp` / `DependsOn` edges that
  derived geometry jobs already use.

## Non-goals
- No new scheduler, task graph, or executor: reuse `DerivedJobRegistry`
  (RUNTIME-112), `StreamingExecutor`, and `Core.Dag`.
- No new forward (CPU→GPU) binding: reuse RUNTIME-120's `VertexAttributeBinding`
  resolver and GRAPHICS-084's visualization property buffers. `RUNTIME-123`
  remains an editor binding UI follow-up and is not a dependency of this
  readback leg. This task adds only the reverse (readback) direction.
- No per-attribute *upload* streaming changes (owned by RUNTIME-122/124).
- No async readback transport changes (owned by GRAPHICS-096/097); this task
  consumes that transport.
- No editor UI surface (a UI follow-up can consume this later).

## Context
- Owning subsystem/layer: `src/runtime` (`runtime -> graphics/geometry/core/ecs`).
- The chaining/scheduling spine already exists: RUNTIME-112's
  `Runtime::DerivedJobRegistry` (`src/runtime/Runtime.DerivedJobGraph.cppm`)
  exposes `Submit(DerivedJobDesc)`, `SubmitFollowUp(...)`, `DependsOn`,
  `DerivedJobOutput`, `DerivedJobApplyContext`, and `TaskKind`, backed by
  `StreamingExecutor` with explicit dependencies, follow-up scheduling, and
  stale/cancel/failure diagnostics. Today its jobs are CPU/geometry derivations
  with a main-thread apply phase; none read back from the GPU.
- The forward direction is covered: RUNTIME-120 `VertexAttributeBinding` (done)
  binds any feasible typed, count-matched property to a vertex channel;
  GRAPHICS-084 (done) binds properties to scalar/color/vector-field visualization
  buffers and already enforces the `ElementCount × stride == bytes` rule.
  Editor-authored arbitrary channel binding remains owned by `RUNTIME-123` and
  is intentionally not required for a backend-neutral readback→property
  write-back job.
- The missing piece is the readback leg: GRAPHICS-096 adds the async
  `DownloadBuffer` ring (no caller-thread fence wait, drain-time delivery) and
  GRAPHICS-098 the imperative `GpuTransfer` facade; this task wires those into a
  derived-job kind plus the readback→property write-back binding, validated by
  GRAPHICS-095's property-agnostic dimension-match primitive.
- **Timing gap to close (the core design problem).** `DerivedJobRegistry` runs
  `DerivedJobDesc::Execute` on a `StreamingExecutor` worker and then runs
  `ApplyOnMainThread` as soon as that worker returns
  (`StreamingExecutor::ApplyMainThreadResults` finalizes immediately after apply).
  A GRAPHICS-096 readback result, however, arrives **later**, through the transfer
  queue's `CollectCompleted()` drain. So the current submission path has no
  non-blocking place to wait for the readback token/sink before
  `DerivedJobApplyContext` writes the property — an implementation would be forced
  either to block on the GPU fence (forbidden) or to apply before the bytes exist.
  This task must add an explicit, non-blocking **park/resume seam**: a job that
  issued a readback parks in a "waiting for readback" state and only resumes to
  its apply phase once the token reports complete and the sink has delivered. The
  engine already has the symmetric precedent — `StreamingTaskState::WaitingForGpuUpload`
  — so the natural shape is a `WaitingForReadback` resume state alongside it,
  polled at the frame drain after `CollectCompleted()`.
- ADR-0023 records the layering and the reuse-not-reinvent decision.

## Slice plan
- **Slice A (park/resume seam + CPU contract).** Add the non-blocking
  `WaitingForReadback` park/resume state (symmetric to `WaitingForGpuUpload`) so a
  readback job parks after issuing the download and resumes to apply only when the
  token completes at the frame drain; add the readback→property write-back binding
  (validate a GPU buffer range against a target property via GRAPHICS-095; write
  bytes into the property on resume/apply) and the readback `DerivedJobDesc` kind.
  Prove against a mock transfer queue (token completion controllable) / mock
  graphics seams. Preserves the default CPU gate.
- **Slice B (operational evidence).** Reuse the GRAPHICS-096 (and/or
  visualization) `gpu;vulkan` smoke to prove a device-computed buffer is read
  back, written into a CPU property, and a chained follow-up re-uploads a derived
  property that the renderer consumes.

## Required changes
- [x] Add a non-blocking readback park/resume seam. Issuing a readback must
      transition the task into a `WaitingForReadback` state (symmetric to the
      existing `StreamingTaskState::WaitingForGpuUpload`) that holds the
      GRAPHICS-096 `ReadbackToken` / sink; a per-frame poll after the transfer
      queue's `CollectCompleted()` resumes the task to its apply phase only once
      the token reports complete. The apply phase must never run before the bytes
      exist, and no caller/worker thread may block on a GPU fence. Recommended
      home: extend `StreamingExecutor` (RUNTIME-112) with the resume state so the
      seam is reusable; `DerivedJobRegistry` consumes it. (Touching the RUNTIME-112
      modules is expected and in-scope for this task.)
- [x] Add a readback→property write-back binding (runtime, backend-neutral):
      given a source GPU buffer range and a target `PropertyRegistry` property,
      validate dimensional compatibility through `RHI::BufferTransfer`
      (GRAPHICS-095) and write the readback bytes into the property on resume;
      fail closed with precise diagnostics on mismatch.
- [x] Add a readback `DerivedJobDesc` kind / submission path that issues a
      GRAPHICS-096 `DownloadBuffer` (or GRAPHICS-098 facade readback), parks via
      the seam above, and lands the result via `DerivedJobApplyContext` on resume,
      reusing the existing dependency / follow-up / stale-cancel machinery.
- [x] Ensure chaining works end to end: a follow-up job (`SubmitFollowUp` /
      `DependsOn`) does not become ready until the readback job has resumed and
      applied, so it can consume the written-back property to derive and re-upload
      a color / vector-field property through the existing forward binding.
- [x] Add readback-job diagnostics counters to the derived-job snapshot
      (readbacks issued/waiting/completed/failed/stale).

## Tests
- [x] CPU contract `tests/contract/runtime/Test.GpuReadbackJob.cpp`
      (labels `contract;runtime`):
      - a readback job with an **incomplete** mock token parks in
        `WaitingForReadback` and its `ApplyOnMainThread` does **not** run; after
        the mock token is marked complete and the drain runs, the job resumes and
        writes the property exactly once (proves apply never precedes the bytes
        and never blocks);
      - readback→property write-back binds a matching range and fails closed on
        dimension mismatch;
      - a `SubmitFollowUp` chain ("readback → derive color/vector-field →
        re-upload") stays un-ready until the readback resumes, then executes in
        dependency order;
      - stale/cancel of a parked readback job is diagnosed and does not apply.
- [x] Default CPU gate stays green; existing `DerivedJobRegistry` and
      `VertexAttributeBinding` tests remain green (no forward-path behavior change).

## Docs
- [x] Update `src/runtime/README.md` (derived-job / transfer section) describing
      the readback job kind and the readback→property write-back binding.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Cross-link ADR-0023 and RUNTIME-112 / GRAPHICS-096 / GRAPHICS-098.

## Acceptance criteria
- [x] A readback job parks in `WaitingForReadback` and resumes to its apply phase
      only after the GRAPHICS-096 token completes at the frame drain — the
      property is never written before the bytes exist and no thread blocks on a
      GPU fence.
- [x] An algorithm can submit a readback job whose GPU result is written into a
      CPU property (dimension-checked, fail-closed), and can chain follow-up
      derive+upload jobs that stay un-ready until the readback has applied.
- [x] No new scheduler/task-graph is introduced; the work composes
      `DerivedJobRegistry` + `StreamingExecutor` + GRAPHICS-096/098.
- [x] Default-gate contract tests pass; the opt-in `gpu;vulkan` round-trip is
      cited as run for `Operational`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -R 'GpuReadbackJob|DerivedJob|VertexAttributeBinding' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
# Operational: cite a ci-vulkan gpu;vulkan readback round-trip run here.
```

Completed 2026-06-25:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests -- -j16
ctest --test-dir build/ci --output-on-failure -R '^MeshPrimitiveViewExtraction.VertexPositionDirtyRepacksBothViews$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GpuReadbackJob|DerivedJob|VertexAttributeBinding' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'RuntimeStreamingExecutor.Readback' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GpuReadbackJobGpuSmoke' --timeout 120
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke.ReferenceTriangleVertexColorStreamShadesDeferredSurface' --timeout 180
cmake --build --preset ci --target IntrinsicTests -- -j16
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Introducing a new scheduler/task-graph/executor instead of reusing
  `DerivedJobRegistry` / `StreamingExecutor` / `Core.Dag`.
- Running a readback job's apply phase before its token has completed, or
  blocking a caller/worker thread on a GPU fence to bridge the timing gap (the
  park/resume seam is mandatory).
- Duplicating the forward property→channel binding (reuse RUNTIME-120 and
  GRAPHICS-084; keep the RUNTIME-123 editor binding UI separate).
- Adding Vulkan/RHI-specific knowledge to runtime beyond the graphics public API.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
- Slice A closes `Scaffolded → CPUContracted`. `Operational` owned by `RUNTIME-126`
  (this task's Slice B) via the cited `gpu;vulkan` readback round-trip.
