# ADR 0030: GPU property residency, observing renderer, and Accept to the CPU

- **Status:** Proposed
- **Date:** 2026-09-29
- **Owners:** graphics / runtime
- **Related tasks:** GRAPHICS-153, GRAPHICS-154, GRAPHICS-155, RUNTIME-292, GRAPHICS-156, RUNTIME-293, RUNTIME-294, RUNTIME-295, METHOD-064, METHOD-065

## Context

The operator measured that GPU backends are often slower than their CPU twins. Several things
contributed:
- per-iteration round trips;
- indices rebuilt inside loops (fixed in GRAPHICS-153 slice 1);
- whole results read back per chunk (slice 4);
- inputs uploaded again even though the renderer already holds them.

The operator decided:
1. GPU methods reuse resident buffers.
2. CPU<->GPU traffic happens only at a method's start and end.
3. Each written property gets a double- or triple-buffered GPU set; a swap at a frame boundary
   updates the render buffer, also for live previews.
4. When the method has finished, the result is synchronized to the CPU, which stays the source of
   truth afterwards.
5. Future GPU backends use the same mechanism.

`docs/architecture/property-coherence.md` (`geometry.property-coherence`) currently says three
things:
- the CPU PropertySet is the authority whenever no GPU method is in flight;
- GPU methods upload once, iterate GPU-local and read back once;
- sharing GpuWorld render allocations is a later, measured optimization.

Facts this decision rests on (verified on `claude/cpd-nystrom`, 2026-09-29, by two independent
reviews):
- **Renderer positions.** They live in `GpuWorld.ManagedVertexBuffer0`: device-local, Storage,
  BDA, no TransferSrc. Each geometry has a block of SoA channel streams, with positions as float3
  at stride 12. Shaders pull them through `GpuGeometryRecord.VertexBufferBDA` and
  `gl_VertexIndex - VertexOffset` (`assets/shaders/depth_prepass.vert:33-36`); GPU entity picking
  reads the same BDA (`selection/entity_id.vert:43`).
- **GpuWorld's CPU copy.** GpuWorld keeps a CPU shadow of every packed block (`VertexBytes`) and
  replays it on compaction and on `RebuildGpuResources`. Compaction has no production caller yet.
- **Extraction.** Entity -> geometry handle, the observed `v:position` revision and the seam gather
  map (`MeshSourceVertexForGpuVertex`) live in the Engine-private `RenderExtractionCache`.
  - Point clouds and graphs are 1:1 with `v:position`, including deleted rows.
  - Meshes with UV or normal seams are not.
  - Culling bounds come from CPU positions at plan build.
  - Primitive pick refinement uses CPU positions.
- **Visualization residency.** `VisualizationPropertyBufferResidency` is host-visible and
  CPU-written, and allocates a new buffer per change. Recipes already accept an external
  `BufferBDA`/`DirtyStamp`.
- **Submission.** Frame participants record at the end of the frame's graphics command buffer.
  Immediate compute (GRAPHICS-150) is submitted on the graphics queue, so both are ordered before
  every later frame. Host-visible completion (`CollectCompleted`) is a once-per-frame poll: work is
  delivered once it has finished, not necessarily in the frame that submitted it.
- **CPU stages in today's implementations.** Several methods run a CPU stage every iteration (an
  implementation state, not an algorithmic necessity; see decision 8):
  - ICP solve and CPD M-step;
  - WLOP/CLOP/EAR projection;
  - the reductions of normals, outliers, FPFH, density, weights, spacing, bilateral and
    construction, which download neighborhoods.

  LOP, k-means, keypoints, property filter/CG, FPS and the CPD E-step record their own compute.

## Decision

1. **Owner and surface.**
   - Graphics owns the buffers: one concrete `Graphics.GpuPropertyResidency` with plain records.
     It is ECS-blind, keyed like `GeometryResidencyKey`, and holds device-local `BufferManager`
     leases.
   - Runtime (`GeometryIntegration`) owns binding and authority with free functions
     `ResolveGpuPropertyInput`, `AcquireGpuPropertyOutput` and `CommitGpuProperty`. The per-run
     transaction state lives in the existing job state (framed or immediate), not in a new class
     with its own lifecycle.
   - No service hierarchy, no registry.
   - `VisualizationPropertyBufferResidency` stays separate. GPU-authored scalars reach it through
     the recipe `BufferBDA`. Visualization buffers are never method inputs.
2. **Typed canonical storage.** The residency stores a property in its own type and layout
   (float, double or integer scalars; float vectors) with a layout identity. A float presentation
   view is separate and derived on the GPU. A lossy copy is never used as an authoritative input.
   Kernels that need another layout (planar, vec4, double3, float-float) convert once on the
   device at their start. Where it is cheaper, the kernel reads the canonical layout instead
   (LOP and k-means read stride-12 float3).
3. **One direction of data flow; the renderer only observes.**
   - Data flows CPU -> residency -> render buffers. GPU methods read and write residency buffers
     only. Render buffers (`GpuWorld` blocks, visualization buffers) are never a source for a
     method.
   - A property's residency entry has a **canonical slot** that equals the current CPU revision.
     It is uploaded once per revision when a GPU method or the renderer first needs it, and
     reused while the revision holds. While a method runs, the entry also has that method's
     **ring** (write slot and front).
   - `SpatialIndexCache` and every GPU method take their positions from the residency, so all
     methods share one GPU copy per entity, property and revision.
   - Later step (RUNTIME-295): when a property is resident, extraction fills its render block by
     GPU copy from the canonical slot instead of uploading it from the CPU again, so each change
     is uploaded exactly once.
4. **Lifetime by completion, not frame counts.**
   - Every slot records its producer and all consumer completions: method kernels, the render
     copy, directly bound render reads over the frames in flight, and immediate/transfer
     timeline values.
   - A slot may be rewritten or freed only when all are complete and it is neither canonical,
     front nor leased.
   - Ring depth is a capacity policy per key:
     - fronts only copied into a render block (positions): 2;
     - fronts bound directly by the renderer (scalars, colors): 3;
     - terminal-only outputs: 1 or 2.
   - When the ring is exhausted, previews are dropped or coalesced; the ring never overwrites and
     never blocks.
   - Device loss evicts everything, and running methods abort.
5. **Observation (what the renderer shows).**
   - The renderer observes the properties the appearance settings select: positions, the
     colormap scalar, colors.
   - For each such property it shows the running (or finished, not yet accepted) method's ring
     front, i.e. the slot not being written, if there is one, and otherwise the canonical slot.
     Switching the appearance to a property under computation shows its live state at once.
   - Scalars and colors: the front is bound directly through the visualization recipe's
     `BufferBDA`, with no copy. A double property gets a float presentation view derived on the
     GPU.
   - Positions: the front is GPU-copied (a gather for seam-split meshes) into the `GpuWorld`
     block at the head of the culling pass, where `SubmitPendingUploadBarriers` runs, using
     `GpuTransferInCommandUploadDesc`. The barriers are compute-write->transfer-read and
     transfer-write->shader-read.
   - While positions show uncommitted data:
     - bounds are widened conservatively (or culling bypassed);
     - primitive pick refinement is disabled for the entity (entity-level picks only);
     - dependent normals keep their CPU state;
     - the block's CPU shadow is marked stale, so compaction or replay never writes old bytes
       over it.
6. **Accept (the user commits).**
   - A method that finishes or is stopped enters "ready to accept". Its front stays observed and
     the CPU is unchanged.
   - Accept (the panel's Apply) does, in order:
     1. one readback of the front in the property's precision;
     2. the existing undoable publication (`PublishPointScalarField` and a positions analogue
        with the same before/after and revision-watch shape);
     3. the front becomes the canonical slot bound to the new CPU revision.
   - For point clouds and graphs, the `GpuWorld` shadow is patched and extraction acknowledges
     the revision without `MarkGpuDirty` / `MarkVertexPositionsDirty`, so nothing is uploaded
     again. Meshes use the ordinary revision-delta upload until RUNTIME-295.
   - Accepting a stopped method takes its intermediate state (today's CPD "pause, then Apply").
   - "Applied" is reported only after the CPU publication succeeds.
   - Batch and agent commands may accept automatically when they finish (unchanged contracts).
7. **Discard, cancel, stale.**
   - Discard or cancel: nothing is published, the ring is released (freed only after its
     completions), and observation returns to the canonical slot. The render block is restored
     from the current CPU state by a forced extraction update, not from an old shadow.
   - If the CPU property changes while a result waits for Accept, Accept is disabled with the
     reason (stale). The result can only be discarded.
   - Busy or stale acquisition defers or aborts; it is never a silent CPU fallback.
8. **CPU stages are declared, then ported.**
   - Where today's implementation runs a CPU stage per iteration, the traffic that stage needs
     stays and is reported in diagnostics.
   - Almost every such stage can be ported. Only MST normal orientation is inherently sequential,
     and even it has parallel alternatives. The ports are sized per method in RUNTIME-294, with
     vertex normals and outliers first.
   - Nonrigid CPD/BCPD becomes GPU-friendly with a basis-filtered M-step (METHOD-065).
9. **Backend seam.**
   - A GPU method's graphics workspace records from `GpuPropertyView` inputs into `WriteLease`
     outputs, never from CPU spans. Its runtime job uses Resolve, Acquire and Commit.
   - Config, `RequestedBackend`/`ActualBackend`, fallback reasons and the residency IO counters
     stay uniform per module.
   - `geometry.property-coherence` is amended in the first slice that changes behaviour. The
     `method.engine-integration` publication row states "GPU preview: yes/no; commit via X".

## Consequences

- **Positive:**
  - Resident inputs upload nothing.
  - Fully GPU methods move data only at start and end.
  - Previews are visible without a CPU round trip.
  - New GPU backends inherit publication, undo and reporting.
- **Trade-offs:**
  - A resident property exists twice on the GPU: canonical slot and render block (positions of
    10k points: +120 KB; 385k: +4.6 MB), plus the ring while a method runs.
  - Until RUNTIME-295, a property used by both the renderer and a GPU method is uploaded twice
    per revision.
  - While uncommitted data is shown, the renderer and GPU picking see newer data than CPU
    consumers; refinement, bounds and dependent normals need the handling listed above.
  - GpuWorld gains a copy hook and a stale-shadow flag; completion tracking has to cover
    immediate submits and direct render reads.
- **Follow-ups:**
  - GRAPHICS-154 (completion lifetimes + residency input, first consumer SpatialIndexCache);
  - GRAPHICS-155 (typed residency);
  - RUNTIME-292 (first scalar transaction end to end);
  - GRAPHICS-156 (position observation);
  - RUNTIME-293 (positions Accept);
  - RUNTIME-295 (extraction fills render blocks from the residency);
  - RUNTIME-294 (per-method migration and ports of CPU stages);
  - GRAPHICS-153 slice 6 (measurements).

## Alternatives Considered

- **GPU-authoritative properties with lazy readback.** Rejected by the operator: the CPU must be
  current once a method completes (undo, save, CPU methods).
- **Methods reading the render buffer (borrowing `GpuWorld` blocks as inputs).** This was the
  first draft. Rejected by the operator: render buffers are observers only. Borrowing needed
  pinning, compaction coupling and guards against reading uncommitted previews as canonical
  input, and it only worked for row-aligned float positions.
- **Re-pointing the geometry record at the ring slot.** It works for the shaders, but it splits
  allocation, shadow and compaction ownership, and seam-split meshes need a gather anyway. The
  copy is simpler to reason about; its cost gets measured.
- **Committing automatically when a method ends.** Rejected for interactive use: the user
  accepts. Batch and agent commands keep their automatic publication.
- **Extending `VisualizationPropertyBufferResidency` as the store.** Rejected: it is host-visible,
  CPU-written, allocates a new buffer per change, and is keyed by string.
- **Frame-number slot reuse.** Rejected: it cannot account for immediate submits or delayed
  completion.

## Validation

- **Null-device contract tests:**
  - acquiring an input twice uploads once; a revision bump uploads once more;
  - slot reuse only after completion, including direct render reads;
  - observation picks the ring front during a run and the canonical slot otherwise;
  - Accept, Discard, cancel, stale-while-pending and undo behave as described;
  - no method input ever resolves to a render buffer.
- **gpu;vulkan smokes:**
  - an LBVH built from the residency is bitwise equal to one built from today's upload;
  - observed pixels move before Accept and return on Discard;
  - after Accept the CPU equals the readback, and the next GPU run uploads nothing;
  - per migrated method, parity with the CPU reference (bitwise where it is bitwise today);
  - IO counters show zero uploads for resident inputs across consecutive methods.
- **Before/after table:** IO and time on the Vlasic meshes and the CPD fixture.
