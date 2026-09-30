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

Position observation and Accept for positions, and routing render uploads
through the residency, follow (GRAPHICS-156, RUNTIME-293, RUNTIME-295).

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
