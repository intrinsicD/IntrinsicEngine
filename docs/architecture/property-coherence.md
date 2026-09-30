# Geometry property CPU/GPU coherence

This document is the canonical contract for geometry properties that cross a
CPU/GPU boundary for rendering or method execution. It applies uniformly to
every method; method packages do not call renderer upload functions and do not
maintain a second dirty flag.

## Authority at method boundaries

`Geometry::PropertySet` is the canonical CPU authority whenever no GPU method
is in flight. CPU methods read const properties and publish results through the
existing mutable property API. GPU methods snapshot and upload their resolved
CPU inputs once before dispatch, keep intermediate iterations in GPU-local
working buffers, and perform one terminal readback. A GPU result is not
reported as applied until runtime has revalidated the request and published the
terminal values to the named canonical CPU output properties.

Methods never compute in, or read from, `GpuWorld`'s packed render allocation
or visualization buffers: render buffers only observe (ADR 0030). A GPU method's
inputs come from the **GPU property residency** (`Graphics.GpuPropertyResidency`,
ECS-blind; runtime binds entities through `Runtime.GpuPropertyBinding`):

- **Canonical slot.** One device-local buffer per key, CPU revision and typed
  layout (`GpuPropertyLayout`: scalar type, channels, stride, count, row map).
  It is uploaded through the transfer queue when a GPU user first needs that
  revision and shared by every later user while the revision holds; a new
  revision or another layout uploads once into a new buffer. The property keeps
  its own type: a double property is resident as double, and a layout mismatch
  is a miss, never a reinterpretation; a tightly packed stride and stride 0
  are one identity. A refused transfer-queue upload caches nothing (counted;
  the caller defers, never a synchronous write of unknown outcome). A slot's
  frame use is complete one frame past the frames-in-flight distance, because
  frame N's fence is waited by `BeginFrame(N + FramesInFlight)` after the
  counter already reads that value. Consumers record their own
  `TransferWrite -> ShaderRead` barrier before the first read (the LBVH build
  already does).
- **Output ring.** While a method writes a property, its key has a ring of
  1..3 slots (a per-key depth policy): `AcquireBack` hands out a write slot,
  `Publish` makes it the front, `Front` is what the renderer observes, and
  after Accept `BindRevision` makes the front the canonical slot of the new CPU
  revision, so the next run uploads nothing. `Discard` releases the ring. An
  exhausted ring (every slot front, leased or pending) drops the preview; it
  never blocks and never overwrites.
- **Completion.** Every slot records its last frame of use (`NoteUse`) and its
  transfer/readback tokens (`AddCompletion`); it is rewritten or freed only when
  the frames in flight have passed, every token is complete and no lease is
  held. `DestroyBuffer` is frame-deferred, so freeing was already safe; the
  records exist for in-place ring rewrites and eviction.
- **Cache.** Canonical slots are an LRU cache (`Tick`): a slot unused for
  `render.gpu_property_idle_evict_seconds` is evicted, and above
  `render.gpu_property_budget_megabytes` the least recently used slots go
  first, size-weighted. Use is a method input or a frame in which the renderer
  observes the slot (`MarkObserved`). Slots with a ring, pending completions, a
  lease or observed this frame are never evicted; an evicted slot costs one
  upload on its next use. The clock is injected for tests. IO counters report
  uploads, readbacks, hits, misses, evictions, resident bytes, publishes, ring
  waits and dropped previews.

The stable boundary is:

```text
CPU method: const CPU input -> CPU kernel -> canonical CPU output/revision
GPU method: CPU revision -> canonical residency slot (once per revision, shared)
                          -> GPU-only iterations -> ring front (observed by the renderer)
                          -> Accept: one readback -> CPU output/revision -> front becomes canonical
Rendering:  CPU revision delta -> copied upload plan -> staging/copy -> GPU draw
            (or the ring front bound directly while a transaction shows it)
```

`SpatialIndexCache` owns the residency (created on first GPU use, configured
from the render config, ticked from its maintenance hook). Current users of
canonical slots: its GPU index in property space over every row (GRAPHICS-154);
an index keeps its slot leased while it is current, so that slot is not evicted
under it. Transformed or compacted indices keep a private upload. Where a
method still runs a CPU stage per iteration, the traffic that stage needs is
reported by the method until its port lands (RUNTIME-294).

### GPU property transactions (observation and Accept)

A GPU method that writes a property runs as a **transaction** (ADR 0030
decisions 5-7; first consumer: Vulkan property smoothing, RUNTIME-292). Its
per-run state lives in the method's job state, not in a service:

- **Input and output.** The kernel reads its input from the canonical slot
  (`ResolveGpuPropertyInput`; one upload per CPU revision, so a second run on
  an unchanged revision uploads zero input bytes) and writes the output
  property's ring (`AcquireGpuPropertyOutput`, depth 3 for renderer-bound
  scalars). Conversion between the property's own scalar type and the kernel's
  working layout happens on the device (`property_filter.comp` Load/Store); a
  double scalar additionally publishes a float **presentation ring** keyed by
  the same name with `ValueKind Float` (`GpuPropertyPresentationRef`), because
  the colormap shader reads floats. The implicit solver's seeds are gathered
  from the canonical slot too (`SeedsOnDevice`); only its CPU-assembled
  right-hand-side diagonal and fixed-row coupling are uploaded per run (ADR
  0030 decision 8, reported in the result message).
- **Byte identity.** The ring front that Accept binds must equal the CPU
  revision it publishes: the store first copies the output property's
  canonical slot (rows outside the sampled live rows keep their published
  bytes; a new output's read as 0, as the CPU publication writes them) and
  writes the input's own value for fixed and isolated rows (the CPU restores
  those from the input). Values convert to the property's scalar type on the
  device exactly as the CPU casts them.
- **Observation.** Extraction asks the residency owner
  (`RenderExtractionCache::SetGpuPropertyObserver`, set by `SpatialIndexCache`)
  for every appearance-selected scalar; while a ring front covers the property's
  rows, the scalar recipe binds it through `BufferBDA` instead of uploading the
  CPU property, and the residency records the frame's use (`MarkObserved`).
  Surface lanes with a seam-split vertex remap keep the CPU upload (the front
  follows property rows, not split GPU vertices). An output that does not
  exist on the CPU yet (a new output name) is encoded from the front's element
  count with the appearance's manual range, or 0..1 when the range is invalid
  or auto (auto-range needs CPU values); an existing output keeps its CPU
  auto-range. The editor's display request ("Show <output>",
  `ApplyEditorVisualizationRecipeCommand`) accepts such a pending resident
  scalar through `EditorVisualizationEditingContext::PendingResidentScalar`
  (bound to the residency's ring check); once the ring is discarded the
  request is refused as for any missing property and the appearance that still
  names it encodes as a missing source (the lane falls back). Vector
  properties are not colormap scalars and are not observed.
- **Ready to accept.** A finished or stopped run keeps its front observed and
  the CPU unchanged. Stop takes a chunked solve's latest published preview (an
  explicit filter is one submission and completes as usual).
- **Accept** reads the front back once in the property's precision (the
  readback token is a slot completion; the front stays leased until the bytes
  landed), runs the existing undoable publication and, only when it succeeded,
  binds the front as the canonical slot of the new CPU revision
  (`BindRevision`). "Applied" is reported only after that publication; batch
  and agent commands (`ApplyEditorPropertySmoothingCommand`) accept
  automatically.
- **Discard, cancel, stale.** Discard or cancel publishes nothing and releases
  the ring (freed after its completions; a readback in flight keeps its lease
  until the bytes landed); observation returns to the CPU property. If the
  inputs or the output change while a result waits (also an in-place output,
  which the publication guards by value rather than by revision), Accept is
  refused with the reason and only Discard remains. Undo moves the CPU
  revision on, so the next GPU use uploads once. A refused slot defers the run
  (bounded) and never falls back to the CPU silently.

### Position observation (GRAPHICS-156)

Positions are always shown, so extraction also asks the observer for the
entity's `v:position` (a Vec3 ref on the provenance domain: mesh vertex,
graph node, point-cloud point). The float3 ring front is not bound directly:
its rows live in property order while `GpuWorld` blocks hold packed float3 at
stride 12 per GPU vertex, so the front is handed to `GpuWorld` as the block's
**position preview** (`SetGeometryPositionPreview`) and copied at the head of
the culling pass, where `SubmitPendingUploadBarriers` runs, with a
compute-write -> transfer-read barrier on the front and the managed vertex
buffer's transfer-write -> shader-read barrier. Point clouds, graphs and the
canonical mesh edge/vertex views are 1:1 with the property rows (deleted rows
included) and copy; a seam-split surface gathers through a device copy of
`MeshSourceVertexForGpuVertex` uploaded once per remap revision
(`gpu_world_position_gather.comp`). The copy is recorded only when the front's
stamp or the remap changed or the block was rewritten (a CPU position upload
during the preview lands first and is copied over again; compaction and
rebuild replay set the same flag). The residency records the frame's use of
the front (`MarkObserved`). A front whose row count does not fit the block is
refused and counted (`PositionPreviewBlocksRejected`), never reinterpreted.

While an entity shows uncommitted positions:

- its instances submit unbounded culling bounds (`UnboundedPreviewBounds`, a
  finite radius the frustum test always passes), since the CPU bounds do not
  describe the preview;
- primitive pick refinement is off for the entity
  (`RenderExtractionCache::ShowsUncommittedPositions`, read by
  `SceneInteractionModule`): a primitive-target pick edits nothing and an
  entity-target pick selects the entity as usual. The pre-extraction hooks ask
  `ObservesUncommittedPositions` (the observer, i.e. the state the frame being
  built will show) rather than the last extraction's flag, so the first
  preview frame and the first frame after Discard are already right. The
  preview state is part of the primitive pick stamp taken with the request, so
  a preview that starts or ends before the readback lands discards that pick
  rather than refining pixels rendered from one set of positions against
  another;
- the selected-primitive highlights (built from the CPU `v:position`) are not
  submitted for the entity until the preview ends;
- dependent normals keep their CPU state (only the position range is copied);
- the block's CPU shadow is **stale** (`PositionShadowStale` on
  `GpuGeometryResidencyView`): compaction and `RebuildGpuResources` replay
  every channel but the position range, and the next culling head copies the
  front into the rewritten block. A rebuild re-uploads the gather maps from
  their CPU copies first; when no recordable preview remains for a stale
  block (the preview was cleared, the device refused the map, or the gather
  pipeline cannot be created), the replay writes the shadow after all, so the
  block never holds undefined bytes.

When the front disappears without an acknowledged Accept (Discard, cancel, a
mesh Accept), the frame's extraction forces a position channel upload from the
current CPU positions (`DirtyVertexPositions`; `PositionPreviewRestores`),
which makes the shadow authoritative again. The block is never restored from
the old shadow, so a CPU edit made during the preview shows after Discard.
Routing render uploads through the residency (RUNTIME-295) follows.

### Accept of positions (RUNTIME-293)

A GPU method that writes `v:position` begins a **run** before its first write
(`BeginEditorGpuPositionRun` in `Runtime.GeometryProcessingOperations`: the
capture of every row's value, the live rows and the positions' revision, kept
in the method's job state; the internal shape is `PointPositionCapture` /
`PublishPointPositionField`, the positions analogue of
`PublishPointScalarField`). Begin creates the positions' ring and hands its
first write slot to the method (`EditorGpuPositionRunFirstBack`); the run owns
that ring (`GpuPropertyResidency::RingGeneration`) and a second run cannot
begin while a ring waits for Accept or Discard. Accept (`AcceptEditorGpuPositionRun`; batch and
agent commands accept at once) does, in order:

1. one readback of the front in float3 (`GpuFrontReadback`, shared with the
   scalar transaction: immediate where the device can, otherwise with the
   frame; the front stays leased until the bytes landed);
2. the undoable publication of **every row** (deleted rows included, so the
   CPU rows are byte-identical to the front) as one history entry guarded by
   the other inputs' watches and the positions' own revision; non-finite rows
   are refused and nothing is published; a front equal to the CPU rows is
   `NoChange`;
3. inside that first publication, for a **1:1 domain** (point cloud, graph):
   `SpatialIndexCache::CommitGpuPositions` (the runtime commit seam of ADR 0030
   decision 1) hands the rows to `RenderExtractionCache::CommitAcceptedPositions`,
   which patches the block's shadow
   for the position channel only (`GpuWorld::CommitGeometryPositions`: the
   other channels are byte-identical, the fingerprint and content revision are
   refreshed, nothing is uploaded), the sidecar acknowledges the new revision
   and the preview ends without a restore, so the next extraction neither
   uploads nor restores (`PositionCommitsAcknowledged`). Fronts are identified
   by their residency-wide **publication** (`GpuPropertyView::Publication`, the
   observation stamp): a ring slot is reused with the same buffer, so only the
   publication says which bytes a copy holds. When the block's last copy read
   the accepted publication and no newer front or rewrite followed, the commit
   is complete at once; a block that never copied it, or copied an older front,
   gets one copy front -> block at the next culling head instead of a CPU
   upload (`CopyPending`; the slot is held one frame for it, and a CPU
   position upload landing first supersedes the copy). No dirty tag is set.
   A **mesh**, an entity without a resident 1:1 block, or a block that refuses
   the bytes is not acknowledged: the positions are marked dirty and the
   ordinary revision-delta upload applies (RUNTIME-295 routes it through the
   residency);
4. `BindRevision(key, revision, publication)`: the publication Accept read
   back becomes the canonical slot of the new revision, so the next GPU use of
   `v:position` (a method input or `SpatialIndexCache`) uploads zero bytes. The
   accepted front stays leased through the commit; a front published after the
   readback is neither committed to the block nor bound: the ring is discarded
   and the next GPU use uploads the revision once.

"Applied" is reported only after the CPU publication. Undo and redo restore
the rows through the ordinary path (`DirtyVertexPositions`; one upload each)
and move the CPU revision on, so the next GPU use uploads once. A run whose
positions changed since it began is stale: Accept is refused (before, or at
publication) and the caller discards the run. Discard is run-level
(`DiscardEditorGpuPositionRun`): it abandons the run, so a readback still in
flight publishes nothing, and releases only the ring the run acquired
(`Discard(key, generation)`); a terminal run discards nothing, so a later run's
ring on the same property is never touched. The residency's own `Discard` is
not the contract. Authored culling bounds move with the rows in the same
history entry: the **local** bounds of the live rows (deleted rows excluded)
are the history state, and the world bounds are derived from the entity's
world matrix at every mutation (Accept, undo, redo), so a transform edit in
between is never replayed from history. Rows that admit no finite bounds (an
overflowing extent) are refused before anything is written; an entity with
neither authored component keeps the extraction default (authored local bounds
alone are recomputed too, never left for propagation to turn into stale world
bounds). Dependent normals keep their CPU
state; a method that owns them republishes them itself.

### Vertex normals (RUNTIME-296)

The first method row of RUNTIME-294 ported to the residency (ADR 0030 decisions 8-9). The
Vulkan `mesh_face_weighted` and `mesh_face_normals` runs read the canonical `v:position` slot
and write the output's float3 ring (vertex or face rows) through the same transaction shape as the scalar run
(`EditorNormalTransaction`, phases `EditorGpuTransactionPhase` shared by every GPU
transaction). Two things are new:

- **Derived resident data.** The face rings and vertex->face incidences the kernels gather
  over are a `uint32` bundle under a derived key (`#vertex_normal_topology`, or
  `#face_normal_topology` with rings only for face normals; domain
  MeshFace, ValueKind UInt32) whose revision is a hash of the topology and deletion watches
  (every captured input but the positions). It is a canonical slot like any other: uploaded
  once per topology revision, an LRU cache entry, pruned with its entity, and counted by the
  residency's IO counters; a resident bundle describes its layout from the source counts, so
  a hit reconstructs nothing on the CPU. Method results report the bundle's bytes and reuse
  next to the positions' upload bytes.
- **No observation.** Vec3 normal rings are neither colormap scalars nor positions, so the
  observer does not bind them and the render block keeps the CPU normals until Accept
  ("GPU preview: no; commit via the normals transaction"). Accept publishes every row
  through the existing undoable normals entry, then `BindRevision` makes the front the
  canonical slot of the new revision; the accepted output then serves the next run's base
  copy without an upload.

### Outlier score and mask transactions

Outliers and resident scalar methods use double distance decisions and ordered
double reductions, with float rounding at property publication. Subnormal float output bit patterns are encoded by rounded integer stores,
so publication also avoids float FTZ. Float-subnormal
intermediates no longer require float32 denormal preservation; see
[outlier analysis](outlier-analysis.md) and [kernel density](kernel-density.md).
Nonzero subnormal position inputs remain refused because shared LBVH building
uses float arithmetic. Double-underflow kernel ranges fail closed.

Vulkan outlier analysis reads canonical positions through `SpatialIndexCache`;
indices with deleted rows gather their compact positions on the device. The
shared LBVH traversal supplies device statistical/radius/LDR scoring. A single
fixed-order double reduction computes the statistical threshold. The score uses
a float ring; the uint32 mask has a typed ring and float presentation ring.
Both stores first retain existing output bytes outside the live rows.
`EditorOutlierTransaction` uses `GpuFrontReadback` for Accept, the existing atomic
undoable two-field publisher, and `BindRevision` for both accepted fronts.
Discard is generation-scoped; stale input, deletion or either output refuses
Accept. Panel detach discards; batch/agent execution automatically accepts.
GPU preview: yes for score; commit via the outlier transaction. Cardinality-changing
removal remains the atomic CPU compaction stage, with no additional device readback.

## Property revisions

Every property storage and its owning registry carry a process-monotonic,
nonzero `Geometry::PropertyRevision`. Structural edits and mutable
`Vector()`, `Span()`, `Data()`, or element access conservatively mark the
storage modified. Repeated mutable accesses coalesce until a revision consumer
observes the current edit epoch, so an element loop does not perform one atomic
increment per vertex. Const access is side-effect free with respect to content
and never marks a mutation.

A mutable span, pointer, or reference is still a borrow. The common contract is
to finish its writes before the next method/render boundary. A caller that
retains a mutable borrow across a boundary must call `MarkModified()` after its
later writes. Property containers remain externally synchronized; revisions do
not make concurrent unsynchronized mutation safe.

Copies and moves receive fresh revision tokens, and erased descriptors expose
the per-property token. Re-basing a move is intentional: assigning older
prepared content into a newer render source must remain newer than the last
published dirty stamp. Consequently, replacing a whole `PropertySet` cannot
accidentally compare equal to, or look stale beside, the prior content.

## Rendering consumer

Runtime extraction is one independent revision consumer per resident render
lane. Its private sidecar remembers only the revisions and counts used by that
lane:

- mesh, graph, and point-cloud positions;
- resolved texcoord, normal, and color channel properties. Resolved texcoords
  follow `h:texcoord` then `v:texcoord`; resolved normals follow `h:normal`
  then `v:normal`, as defined in [geometry API
  style](geometry-api-style.md#normals-are-corner-domain-capable);
- exact topology properties consumed by the corresponding plan builder;
- vertex-channel binding generation; and
- mesh edge/vertex primitive-view inputs.

An explicit ECS dirty tag remains a precise compatibility hint. Revision deltas
are the correctness source: position-only changes request the existing partial
position update, resolved attribute changes request their channel, and count or
topology changes request full replacement. The sidecar acknowledges revisions
only after successful reconciliation, so a failed upload is retried.

A mesh whose UVs or normals are corner-owned is uploaded by emitting one GPU
vertex per distinct `(vertex, resolved UV, resolved normal)` tuple, carrying
positions and packed colors across the split. Render extraction tracks the
winning corner property revision, and property-texture bake uses the same split
so residency fingerprints agree. An explicit vertex-normal channel binding
overrides the default corner-over-vertex resolution in both consumers. The ECS
mesh is never split to satisfy the vertex buffer; the duplication belongs to
upload, not authoritative geometry.

### Topology-replacing operations and UVs

An editor operation that replaces a mesh's topology rebuilds the entity's
halfedge mesh and republishes its property sets wholesale, so a property the
rebuilt mesh does not carry is **removed**, not left stale. Each such operation
therefore owes an explicit decision, reported in its result as
`EditorMeshTexcoordOutcome` and named in its message when UVs are lost:

- **Preserve** when the output's corners have source corners to inherit from.
  Simplify is the case: a collapse removes corners and the survivors keep their
  own UVs. Corner attributes are forwarded through the canonical corner walk,
  because vertex numbering survives the GeometrySources → soup → halfedge round
  trip but halfedge numbering does not.
- **Discard, reported** when the output has corners no source UV describes.
  Remesh and subdivide are those cases; resampling UVs onto a re-tessellated
  surface is a separate capability, not a side effect of the command.

A silent discard is a defect, not a policy. See `BUG-146`.

Same-cardinality scalar and mask publication updates the named property's
revision and workspace snapshot, without setting geometry-wide GPU or vertex
attribute dirty tags. This also applies to undo/redo: visualization buffers
observe the changed property independently. Marking an unchanged mesh dirty
forces needless packing and geometry uploads. Topology/cardinality replacement
and actual geometry-channel edits retain their own dirty/update paths.

CPU-backed visualization recipes use the resolved property's revision as their
buffer dirty stamp. The graphics residency cache therefore reuses unchanged
property buffers and reuploads a changed scalar, label, color, vector, or
isoline property without an authored generation bump. Explicit dirty stamps
remain meaningful for external GPU-address sources that have no canonical CPU
property. A CPU fragment bake's texcoord stamp resolves through the same
corner-over-vertex order it reads the UVs by: watching only `v:texcoord` would
pin a seam-carrying mesh — which has no `v:texcoord` at all — to its authored
stamp, so corner-UV edits would never re-bake.

## Vulkan upload and lifetime

`GpuWorld` remains the sole packed render-geometry allocator. Its normal Vulkan
upload path submits affected byte ranges through `ITransferQueue`, whose
persistently mapped staging belt copies into device-local target buffers and
reclaims ring ranges only after the transfer timeline completes. The frame
records `TransferWrite -> ShaderRead` barriers before consumers. Each staged
overwrite first records a range-scoped `all prior reads/writes ->
TransferWrite` destination barrier; same-queue submission order without that
memory dependency is insufficient for Vulkan write-after-write/read safety. If
the bounded staging service rejects a submission, the legacy synchronous
device write is a correctness fallback, not the ordinary path.

The same `Graphics::SubmitBufferUpload` boundary is used by every current
device-local geometry compute backend (K-Means, Progressive Poisson, and the
LOP family). It copies resolved CPU input and state bytes into the staging belt
before returning; each backend records its existing transfer-to-compute barrier
and then stays GPU-local through its iterations. GPU inputs deliberately using
host-visible storage, such as property-texture baking, keep the cheaper
persistently mapped `WriteBuffer` path instead of staging an extra copy.

The promoted transfer service currently submits on the graphics queue. Queue
order protects an in-place target range from earlier-frame readers and orders
the copy before the later render submission, so destination double-buffering is
not required merely to avoid a CPU stall. A future dedicated transfer/compute
queue requires explicit producer completion tokens, queue-family ownership,
and either destination renaming or a proven non-overlap schedule before this
assumption may change.

GPU-to-CPU method completion uses the shared mapped readback ring. In-flight
method resources and staging/readback slots retire by completion token; they
are not destroyed by a guessed frame delay. No intermediate method iteration
crosses to the CPU.

## Ownership and failure rules

- `geometry` owns property storage and revision semantics; it imports no ECS,
  runtime, graphics, RHI, or Vulkan layer.
- `runtime` resolves live ECS property bindings, owns per-consumer observed
  revisions, method boundary validation/publication, and copied upload plans.
- `graphics/renderer` owns render residency and backend-neutral transfer/barrier
  requests plus the shared immediate staged-upload/fallback boundary; it
  receives no live ECS storage.
- `graphics/vulkan` owns staging memory, command submission, Sync2 lowering,
  timeline completion, and resource retirement.
- CPU publication is atomic at the method's existing transaction boundary. A
  failed or stale GPU result does not advance canonical CPU output revisions.
- Unchanged revisions do not upload every frame, and a property not bound to a
  consumer does not invalidate that consumer.

## Proof surface

The contract is exercised by geometry property revision tests, mesh/graph/
point-cloud no-dirty-tag extraction tests, visualization dirty-stamp tests,
`GpuWorld` transfer-staging tests, and the validation-enabled Vulkan LOP
publication-to-render-residency regression listed in the contract catalog.

### Resident density, spacing and density weights

`Graphics.PointScalarAnalysis` reads the canonical position slot through the
cached LBVH and writes a float scalar ring. Density/spacing use ordered double
reductions; compact density weights use source-index-ordered double sums and
shared double exponential evaluation. No neighborhood arrays cross to CPU.
The three adapters share `Runtime.PointScalarTransaction` for leases, stale
watches, generation-safe Discard and `GpuFrontReadback` Accept into
`PublishPointScalarField`, followed by publication-bound `BindRevision`.
Existing deleted rows copy the resident output base; new deleted rows are zero.
The float ring is directly observable by the colormap. Other scalar storage is
refused at Vulkan admission because this kernel's store/readback contract is float.
Panels expose Accept/Discard and discard on detach; batch/agent Apply accepts
automatically. Result/agent IO counters distinguish input uploads/cache hits
from the terminal scalar readback. Legacy neighborhood/CPU timing fields are zero
for resident runs; these counters are not a performance measurement. GPU parity
execution for this port is pending.

Initial compute-submit rejection is an immediate result and does not invoke the
completion sink. Once queued, terminal completion (including an Accept-submit
rejection) is delivered exactly once and releases the ring without CPU/history
publication. The scalar failure smoke restores ordinary samples after its radius
overflow fixture, rejects `Accept point scalar`, checks ring release, then runs
successfully again.

Scalar and outlier transactions validate captured ring generations before Accept
and job publication. A replacement ring invalidates the older transaction; its
cleanup cannot discard the replacement. A nonempty Accept callback replaces the
Start callback after admission succeeds, with one terminal delivery. Scalar
panels omit the Start callback and fold the terminal snapshot through the same
runtime result adapters before publishing their retained result once.

### Resident point-set PCA normals

`Graphics.PointNormals` reads the canonical position slot and cached LBVH, gathers
complete ordered neighborhoods, accumulates double covariance and solves the same
portable double eigensystem as `Geometry.PCA`. Only unoriented execution is admitted;
MST requires a CPU backend. No neighborhood arrays cross to the CPU. The vec3 ring
is not observed by the viewport. `EditorNormalTransaction` owns leases, stale and
ring-generation validation, Discard, and Accept through `GpuFrontReadback`, undoable
normal publication and publication-bound `BindRevision`. Existing deleted output
rows copy the resident base; absent deleted rows start at zero. Results and command
messages report upload bytes, input hits and diagnostic/Accept readback bytes.
The panel uses this same lifecycle; batch/agent commands accept automatically.
See [normal estimation](normal-estimation.md#resident-pca-vulkan_lbvh) for admission,
numerical limits and pending GPU parity evidence.

### Resident farthest-point sampling

`Runtime.PointSamplingOperations` resolves canonical position and optional weight
slots through `ResolveGpuPropertyInput`. `Graphics.FarthestPointSampling` retains
those views through completion and gathers live rows on the device. It applies
the captured world matrix, rounds to the public float position representation,
and converts to double working coordinates, preserving the CPU sampling contract.
Only row-map and transform metadata are written privately at start. Results and
agent/panel diagnostics report residency input uploads, cache hits and CPU-stage
readback bytes (zero for FPS); metadata is not a property-value upload.

Rounds remain bounded by the workspace's point-pair budget. Intermediate chunks
use zero-byte `QueueGpuCompute` submissions; Vulkan uses the existing transfer
queue timeline and callback with no staging allocation or copy. The framed
fallback waits past frame-fence reuse before reporting completion. A submission
refused after its recorder ran fails instead of replaying an advanced cursor.

GPU preview: no; commit via the existing atomic rank/selection publication on
completion (or the existing generated-point-cloud publication). Input, weight,
deletion, transform and output revisions guard publication. Cancellation and
admission refusal leave CPU properties unchanged. FPS has no output Accept ring
or `BindRevision`: its canonical inputs are unchanged by rank/mask publication.
See the [sampling backend contract](../methods/point-sampling.md) for verification.
