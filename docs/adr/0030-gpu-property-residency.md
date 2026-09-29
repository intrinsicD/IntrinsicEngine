# ADR 0030: GPU property residency with preview swaps and end-of-method CPU commit

- **Status:** Proposed
- **Date:** 2026-09-29
- **Owners:** graphics / runtime
- **Related tasks:** GRAPHICS-153, GRAPHICS-154, GRAPHICS-155, RUNTIME-292, GRAPHICS-156, RUNTIME-293, RUNTIME-294

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
- **Hybrid methods.** Several methods run a CPU stage every iteration:
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
     the recipe `BufferBDA`. Compatible resident visualization inputs may still be borrowed.
2. **Typed canonical storage.** The residency stores a property in its own type and layout
   (float, double or integer scalars; float vectors) with a layout identity. A float presentation
   view is separate and derived on the GPU. A lossy copy is never used as an authoritative input.
   Kernels that need another layout (planar, vec4, double3, float-float) convert once on the
   device at their start. Where it is cheaper, the kernel reads the canonical layout instead
   (LOP and k-means read stride-12 float3).
3. **Input resolution.**
   - Row-aligned `v:position` of a point cloud or graph whose sidecar geometry is live, whose
     observed revision equals the property revision, and whose block carries no preview is
     borrowed straight from the GpuWorld block. `EditorFeatureBindings` gets an extraction query
     binding for this.
   - Everything else is uploaded once per CPU revision into the residency and reused while the
     revision holds.
   - A block that holds a preview is never returned as canonical input unless the caller is that
     same transaction.
4. **Lifetime by completion, not frame counts.**
   - Every slot and every borrowed range records its producer and all consumer completions: frame
     fences, immediate/transfer timeline values, and the pool copy.
   - A slot may be rewritten or freed only when all are complete and it is neither front nor
     leased.
   - Ring depth is a capacity policy per key:
     - pool-mirrored positions: 2;
     - directly addressed previews: 3;
     - terminal-only outputs: 1 or 2.
   - When the ring is exhausted, previews are dropped or coalesced; the ring never overwrites and
     never blocks.
   - Borrowed pool ranges are pinned. Compaction treats pinned ranges as occupied destinations.
   - Device loss evicts everything, and running jobs abort.
5. **Preview swap.**
   - Positions are copied (a gather for seam-split meshes) from the ring front into the GpuWorld
     block. This keeps record, allocation and shadow ownership where they are; re-pointing the
     BDA would also work but is not chosen.
   - The copy is recorded where `SubmitPendingUploadBarriers` already runs (head of the culling
     pass), with compute-write->transfer-read and transfer-write->shader-read barriers, reusing
     `GpuTransferInCommandUploadDesc`.
   - During a preview:
     - bounds are widened conservatively (or the instance bypasses culling);
     - primitive pick refinement is disabled for the entity (entity-level picks only);
     - dependent normals keep their last CPU state.
6. **Commit (end of method).**
   - One terminal readback of the front, in the property's precision.
   - Then the existing undoable publication (`PublishPointScalarField` and a positions analogue
     with the same before/after and revision-watch shape).
   - Then `BindRevision` to the new CPU revision. For 1:1 domains the GpuWorld shadow is patched
     and extraction acknowledges the new revision, without `MarkGpuDirty` /
     `MarkVertexPositionsDirty`, so nothing is uploaded again. Meshes commit through the ordinary
     revision-delta upload.
   - "Applied" is reported only after the CPU publication succeeds.
7. **Abort, cancel, stale.**
   - Nothing is published, the leases are discarded (freed only after their completions), and the
     preview is removed.
   - The block is restored from the **current** CPU state: a forced extraction re-upload, not the
     transaction's old shadow.
   - Busy or stale acquisition defers or aborts; it is not a silent CPU fallback.
8. **Hybrids are declared.** Per-iteration traffic that a CPU stage needs stays and is reported
   in diagnostics. Removing it means porting that stage, one task per method.
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
  - Two or three buffers per written property.
  - While a preview is shown, the renderer and GPU picking see newer data than CPU consumers;
    refinement, bounds and dependent normals need the special handling listed above.
  - GpuWorld gains a pin and a copy hook.
  - Completion tracking has to cover immediate submits.
- **Follow-ups:**
  - GRAPHICS-154 (lifetime + resolver);
  - GRAPHICS-155 (typed residency);
  - RUNTIME-292 (first scalar transaction end to end);
  - GRAPHICS-156 (position previews);
  - RUNTIME-293 (positions commit);
  - RUNTIME-294 (per-method migration and ports of CPU stages);
  - GRAPHICS-153 slice 6 (measurements).

## Alternatives Considered

- **GPU-authoritative properties with lazy readback.** Rejected by the operator: the CPU must be
  current once a method completes (undo, save, CPU methods).
- **Re-pointing the geometry record at the ring slot.** It works for the shaders, but it splits
  allocation, shadow and compaction ownership. The copy is simpler to reason about; its cost gets
  measured.
- **Extending `VisualizationPropertyBufferResidency` as the store.** Rejected: it is host-visible,
  CPU-written, allocates a new buffer per change, and is keyed by string.
- **Frame-number slot reuse.** Rejected: it cannot account for immediate submits or delayed
  completion.

## Validation

- **Null-device contract tests:**
  - resolving twice uploads once; a revision bump uploads once more;
  - slot reuse only after completion;
  - swap, commit, cancel, stale and undo behave as described;
  - a preview block is refused as canonical input.
- **gpu;vulkan smokes:**
  - an LBVH built from the pool is bitwise equal to one built from an upload;
  - preview pixels move before commit and return on abort;
  - after commit the CPU equals the readback;
  - per migrated method, parity with the CPU reference (bitwise where it is bitwise today);
  - IO counters show zero position upload for resident inputs.
- **Before/after table:** IO and time on the Vlasic meshes and the CPD fixture.
