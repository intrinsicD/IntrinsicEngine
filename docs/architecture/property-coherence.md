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

This deliberately does not require a method to compute in `GpuWorld`'s packed
render allocation. Method buffers and render buffers have different layouts,
lifetimes, and ownership. Sharing them is a later measured optimization, not a
correctness prerequisite. The stable boundary is:

```text
CPU method: const CPU input -> CPU kernel -> canonical CPU output/revision
GPU method: CPU input -> staging/upload -> GPU-only iterations
                                      -> terminal readback -> CPU output/revision
Rendering:  CPU revision delta -> copied upload plan -> staging/copy -> GPU draw
```

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
