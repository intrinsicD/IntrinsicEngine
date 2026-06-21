# ADR 0023: CPU↔GPU transfer foundation — async two-way transport, property↔buffer-range binding, and a deferred/chainable transfer task graph

- **Status:** Proposed
- **Date:** 2026-06-21
- **Owners:** graphics (RHI + renderer transport) + runtime (binding + scheduling)
- **Related tasks:** GRAPHICS-095, GRAPHICS-096, GRAPHICS-097, GRAPHICS-098,
  RUNTIME-126. Anchors existing work that already covers the upload/binding/
  scheduling spine: RUNTIME-120 (vertex attribute binding resolver, done),
  RUNTIME-121/122/123/124 (color channel, declarative SoA layout, editor
  bind-any-property, per-channel partial uploads), RUNTIME-112 (`DerivedJobRegistry`
  with explicit dependencies + follow-up scheduling, done), and GRAPHICS-084
  (visualization property-buffer residency + dimension-match validation, done).

## Context

The goal is a CPU↔GPU data-transfer foundation that is **asynchronous in both
directions, per-attribute, fast, robust, and flexible**: algorithms (and the
editor UI) should be free to bind *any feasible property* to *any appropriate
GPU buffer range* — the only hard requirement being that the dimensions match —
and to **schedule** that transfer to run at the next opportunity and **chain**
follow-up work onto it (e.g. "I edited normals → also derive a per-vertex color
from them and a vector-field overlay, upload all three, then make them visible").

Crucially, most of the spine for this already exists; the foundation should
*compose* it, not duplicate it:

- **Async upload (have).** `RHI::ITransferQueue::UploadBuffer / UploadTexture /
  UploadTextureFullChain` → `TransferToken`, `IsComplete()` pollable from any
  thread, staging reclaimed once per frame in `CollectCompleted()` (the only
  fence wait). `GpuAssetCache` wraps it with a `NotRequested → CpuPending →
  GpuUploading → Ready` state machine (`src/graphics/rhi/RHI.TransferQueue.cppm`,
  `src/graphics/renderer/Graphics.GpuAssetCache.cppm`).
- **Async download (missing).** The only download primitive is
  `IDevice::ReadBuffer`, a `gpu;vulkan` smoke helper that is host-visible-only,
  `WaitIdle`-stalling, and silently no-ops otherwise (`RHI.Device.cppm:156-163`).
  Every shipping readback is a bespoke per-frame drain (`Picking.Readback`,
  histogram). There is no general, non-stalling readback path.
- **Task graph (have).** `Extrinsic.Core.Dag.TaskGraph` +
  `Extrinsic.Core.Dag.Scheduler` are a real dependency DAG with
  `QueueDomain {Cpu, Gpu, Streaming}`, `DependsOn`, priorities, resource
  read/write declarations, cycle detection, and `BuildPlan()`
  (`src/core/Core.Dag.*`). This is the task-graph mechanism.
- **Deferred/chained execution (have).** `Runtime::StreamingExecutor` runs
  `StreamingTaskDesc{ Kind, Priority, DependsOn, Execute, ApplyOnMainThread }` on
  a background pool with a two-phase compute→main-thread-apply model and a
  `WaitingForGpuUpload` state; pumped/drained on the frame boundary by `Engine`
  (`src/runtime/Runtime.StreamingExecutor.cppm`). This is the "run at next
  opportunity + chain follow-ups" mechanism.
- **Property model (have).** `Geometry::PropertyRegistry` holds named, typed
  arrays (`v:position`, `v:normal`, `v:color`, scalar fields) with element count
  and component type (`src/geometry/Geometry.Properties.cppm`). Component
  *dimension* is implicit in the element type today.
- **Per-attribute streaming substrate (partial).** ADR-0022's SoA model gives
  each channel its own contiguous range + BDA; RUNTIME-122 Slice A landed the CPU
  `VertexLayout` / `VertexChannelStreams` substrate; per-channel GPU residency
  (RUNTIME-122 Slice B) and single-channel partial uploads (RUNTIME-124) are
  open. This is the upload-side mechanism for "stream just the normals".
- **Dimension-match validation + visualization adapters (have, scoped).**
  GRAPHICS-084 validates `ElementCount × stride == bytes` and stride-matches a
  declared `ValueType`, with `Domain {Vertex,Edge,Face,Instance}` and adapters
  (scalar / color / vector-field / isoline) that turn a property into a
  renderable buffer (`Graphics.VisualizationPackets.cppm`,
  `Runtime.VisualizationAdapters.cppm`). This is exactly the "dimensions must
  match" rule and the "normals → color / vector-field" derivation — but today
  only for the visualization path, not a general property↔buffer-range binding.

Three real gaps remain: (1) no async GPU→CPU readback; (2) no *general*
property↔buffer-range binding (only the visualization-scoped one); (3) no
*transfer-job* layer that lets the editor/algorithms schedule and chain transfers
over the existing executor/task-graph instead of going through the coarse,
whole-geometry `DirtyVertexAttributes` repack path.

## Decision

Build the foundation as layered, independently reviewable pieces. Lower layers
are imperative RHI primitives (graphics); upper layers are declarative,
schedulable, chainable jobs (runtime) that *compose* the existing task graph and
streaming executor. **No new scheduler, no new task-graph, no new DAG is
introduced** — `Core.Dag` and `StreamingExecutor` are reused.

1. **`RHI::BufferTransfer` (GRAPHICS-095)** — CPU-pure, backend-neutral buffer
   sub-range math/validation mirroring `RHI::TextureUpload`: sub-range validation,
   alignment, partial-write region planning, **and typed/dimension-match
   validation** (does `elementCount × componentBytes` fit/equal a target region
   of a given offset/size/stride?). This is the single fail-closed "dimensions
   match" primitive shared by uploads, readback, and the binding layer.

2. **Async GPU→CPU readback ring on `ITransferQueue` (GRAPHICS-096, headline).**
   `DownloadBuffer(...)` → `ReadbackToken` + `ReadbackSink`, backed by a recycled
   host-visible readback staging ring, delivered on the existing
   `CollectCompleted()` drain with no caller-thread fence wait — the download
   mirror of the upload guarantee, making transport **symmetric and async both
   ways**. Vulkan implements; Null fail-closes. (GRAPHICS-097 extends it to
   textures, reusing `RHI::TextureUpload` layout in reverse.)

3. **`Graphics::GpuTransfer` imperative facade (GRAPHICS-098).** The low-level
   "do it now on this command context" primitive: upload-CPU-range-to-device with
   the `TransferWrite → ShaderRead` barrier bracket, and read-device-to-CPU over
   the GRAPHICS-096 ring with the `TransferRead` bracket. Centralizes the barrier
   bracketing BUG-049 got wrong; composes existing seams, no new RHI surface.

4. **Property↔buffer-range binding — reuse + close the readback gap.** The
   forward (CPU property → GPU buffer) binding already exists and must be reused,
   not duplicated: RUNTIME-120's `VertexAttributeBinding` resolver binds any
   feasible typed, count-matched property to a logical vertex channel
   (position/normal/texcoord/color/custom) with fail-closed diagnostics;
   RUNTIME-123 exposes the editor "bind any property as normals/colors";
   GRAPHICS-084 binds properties to scalar/color/vector-field visualization
   buffers with the `ElementCount × stride == bytes` dimension-match rule. The
   **only missing direction is readback → write back into a CPU property**: a
   runtime binding that takes a GPU buffer range, validates it against a target
   `PropertyRegistry` property through GRAPHICS-095's dimension-match primitive
   (**the only constraint is that dimensions match**), and writes the readback
   bytes into that property. That binding is added inside RUNTIME-126.

5. **GPU readback jobs in the existing derived-job graph (RUNTIME-126).** The
   chaining/scheduling spine already exists and must be reused, not re-invented:
   RUNTIME-112's `DerivedJobRegistry` (`Submit` / `SubmitFollowUp` / `DependsOn`
   / `DerivedJobOutput` / `DerivedJobApplyContext`), backed by
   `StreamingExecutor` and `Core.Dag`, already provides deferred execution,
   explicit dependencies, follow-up scheduling, and stale/cancel/failure
   diagnostics. RUNTIME-126 adds (a) a **readback job kind** that drives a
   GRAPHICS-096 `DownloadBuffer` and lands its result via the existing
   main-thread apply phase, and (b) the readback→property write-back binding from
   item 4, so an algorithm can **chain follow-ups on a GPU-computed result**
   (e.g. "compute on GPU → read back → derive a color/vector-field property →
   re-upload → make visible") using the same `SubmitFollowUp`/`DependsOn` edges
   that derived geometry jobs already use. The editor "I edited normals →
   schedule an upload executed next opportunity" path already exists via the
   `DirtyVertexAttributes` deferred-extraction trigger (RUNTIME-123/124); the new
   work only adds the readback leg and its job wiring.

Robustness/flexibility are uniform: every transfer entry point validates through
GRAPHICS-095, fails closed (`Core::Expected` / invalid token, never silent
no-op), exposes monotonic diagnostics counters, and never blocks a caller thread
on a GPU fence. Algorithms pick the property and the target range; the binding
layer enforces only dimensional compatibility.

## Consequences

- Positive: one symmetric, non-stalling transport; a single dimension-match
  contract reused across upload / readback / binding; a *general* property↔range
  binding instead of a visualization-only one; deferred + chained transfers
  expressed on the existing `Core.Dag` / `StreamingExecutor` spine (no parallel
  scheduler); per-attribute streaming via the SoA per-channel work. The editor
  and algorithms get the "schedule + chain" ergonomics directly.
- Trade-off: the readback ring adds host-visible staging + one more drain
  responsibility; readback and chained jobs are at least one frame latent by
  design (the alternative is the `WaitIdle` stall). The binding/job layers add
  runtime surface that must stay backend-neutral (no RHI/Vulkan knowledge leaks
  into runtime beyond the existing graphics public API).
- Layering: transport + math + imperative facade live in `graphics`
  (`graphics/rhi`, `graphics/vulkan`, `graphics/renderer`); binding + job
  scheduling live in `runtime` (which already depends on graphics, geometry,
  core, ecs). No new dependency edges.
- Follow-up ordering: GRAPHICS-095 → GRAPHICS-096 (headline) → GRAPHICS-097 /
  GRAPHICS-098; RUNTIME-126 depends on GRAPHICS-096/098 and composes the existing
  RUNTIME-112 `DerivedJobRegistry`; per-attribute *upload* streaming continues
  under the existing RUNTIME-122/124 series (unchanged by this ADR).

## Alternatives Considered

- **Build a new transfer scheduler / transfer DAG.** Rejected: `Core.Dag.TaskGraph`
  (with a `Gpu`/`Streaming` `QueueDomain`) and `StreamingExecutor` (with
  `DependsOn`, priorities, two-phase apply, `WaitingForGpuUpload`) already provide
  dependency ordering, chaining, and CPU→GPU handoff. A second scheduler would
  duplicate cancellation, priority, and drain semantics and fragment the frame
  loop.
- **Keep `IDevice::ReadBuffer` + `WaitIdle` as the readback story.** Rejected as
  the foundation (full device stall, host-visible-only, silent no-op); retained
  only as a test-scoped helper.
- **A bespoke readback drain per feature (status quo).** Rejected: re-implements
  staging/fence/drain per feature and is the missing-barrier bug class (BUG-049).
- **Re-implement a general property→buffer binding from scratch.** Rejected:
  RUNTIME-120 (vertex channels), RUNTIME-123 (editor bind-any-property), and
  GRAPHICS-084 (visualization buffers + dimension match) already cover the
  forward direction. This ADR reuses them and adds only the missing readback
  direction, lifting the *math* of the dimension-match rule into GRAPHICS-095 so
  it is shared rather than copied.
- **Extend the coarse `DirtyVertexAttributes` repack path for the readback leg.**
  Rejected: that path is for CPU-authored upload (and already exists for
  per-attribute streaming via RUNTIME-124). GPU→CPU results land through the
  existing `DerivedJobRegistry` apply phase instead.
- **Synchronous mapped readback (ReBAR / persistently-mapped).** Orthogonal
  placement optimization; a future per-buffer placement hint can sit atop the
  ring. Not forked here.

## Validation

- GRAPHICS-095: CPU contract tests for sub-range/alignment/partial-write *and*
  dimension-match validation (default CPU gate).
- GRAPHICS-096/097: CPU/null contract tests for fail-closed tokens, mock-drain
  delivery, and diagnostics; opt-in `gpu;vulkan` smoke round-trips a
  device-computed buffer/texture to the CPU through the ring with no `WaitIdle`.
- GRAPHICS-098: CPU contract tests prove the barrier brackets against a recording
  mock context; operational evidence reuses the GRAPHICS-096 smoke.
- RUNTIME-126: CPU contract tests prove the readback→property write-back binds a
  GPU buffer range to a matching CPU property and fails closed on dimension
  mismatch; that a readback job runs through the existing `DerivedJobRegistry`
  apply phase; and that a `SubmitFollowUp`/`DependsOn` chain ("GPU compute → read
  back → derive color/vector-field property → re-upload") composes against mock
  graphics/executor seams. Operational evidence reuses the GRAPHICS-096 /
  visualization `gpu;vulkan` smokes.
