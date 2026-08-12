# Geometry API Style and Numeric Policy

This document is the canonical policy for new `src/geometry` APIs. It turns the
current findings from the [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md)
into rules that future geometry tasks can cite without refactoring existing code
opportunistically.

The layer contract remains `geometry -> core` only. Geometry code must not import
or depend on assets, ECS, graphics, RHI, runtime, platform, or app ownership.

## Scope

Applies to new or materially changed public geometry APIs, modules, diagnostics,
and method-integration seams. Existing APIs may keep their current shape until a
dedicated compatibility or migration task changes them.

Do not mix mechanical renames, module moves, or namespace normalization with
semantic algorithm changes. When existing APIs need alignment, create a focused
follow-up task that records compatibility expectations and inventory updates.

## Module, file, and namespace style

- Prefer one narrow module per coherent geometry concept or algorithm family.
- Name module interface files as `Geometry.<Concept>[.<Subconcept>].cppm` and use
  an exported module name that matches the file stem.
- Place public symbols under `Geometry::<Concept>` or another explicitly
  documented namespace that matches the module concept.
- Use the broad `Geometry` umbrella only for stable, commonly composed APIs.
  Advanced or experimental numerical modules may remain narrow imports until
  their public role is intentionally expanded.
- Do not add broad umbrella exports merely for convenience; callers should import
  the least-specific module set they need.
- Keep implementation helpers non-exported where possible. Exported `Internal`
  namespaces are compatibility debt unless the helper types have stable semantics
  and tests.

When a new task must deviate from these rules to preserve compatibility, the task
record should name the deviation and include a removal or normalization follow-up.

## Public state and mutability

- Plain data records may expose public fields when they are simple values with no
  invariants beyond their documented types.
- Owning containers and acceleration structures should expose cheap const access
  through spans or noun accessors and reserve mutation for explicit operations.
- Mutable borrowed views must document the source storage they mutate and the
  lifetime requirements of that borrow.
- Algorithms should request the least structured data they need. A point-set
  kernel accepts a typed property/span of samples on any element domain; it
  does not require a point-cloud container or vertex handles. Traversal kernels
  additionally request graph adjacency, and face/topology editors request the
  corresponding mesh topology.
- Use explicit hard-copy conversions when topology/cardinality changes, when
  independent lifetime is required, or when attribute layout conversion is
  necessary. Those conversions should return diagnostics rather than silently
  dropping data.

## ECS element-domain source contract

The canonical physical materialization contract is organized by element domain,
not only by import provenance:

| Element domain | Compatible entity provenance |
| --- | --- |
| `Vertices` | point cloud, graph, mesh |
| `Halfedges` | graph, mesh |
| `Edges` | graph, mesh |
| `Faces` | mesh |

The implementation matches this matrix. `PopulateFromGraph` materializes the
graph's existing vertex, halfedge, and edge property sets as `Vertices`,
`Halfedges`, and `Edges`, then stamps `HasGraphTopology` as provenance. Graph
halfedges preserve the count-matched `h:connectivity` property
(`Geometry::Graph::HalfedgeConnectivity`, containing target vertex plus
next/previous halfedge handles), while graph vertices retain their
`v:connectivity` representative. Garbage collection reconstructs the surviving
vertex-star rings from compacted edge pairs, so neither representative nor
next/previous links can retain a deleted edge. They do not fabricate mesh face
adjacency or a `Faces` component. Runtime's canonical logical property domains
therefore include `GraphNode`, `GraphHalfedge`, and `GraphEdge`, each gated by
graph provenance and resolved to its corresponding shared physical source.
`HARDEN-087` explicitly supersedes the bounded graph-only
`Nodes` layout recorded by `HARDEN-065` and retains the provenance-versus-
capability separation introduced by `HARDEN-083` with one physical `Vertices`
capability.

This representation follows the reduced halfedge model described by Kettner,
where undirected graphs need not carry face records, and the independent
per-element property model used by OpenMesh. See Kettner's
[generic halfedge design](https://doi.org/10.1016/S0925-7721(99)00007-3), the
[CGAL `HalfedgeDS` description](https://doc.cgal.org/Manual/3.3/doc_html/cgal_manual/HalfedgeDS/Chapter_main.html),
and the [OpenMesh paper](https://www.graphics.rwth-aachen.de/publication/03130/).

The rows are a physical availability matrix, while method substitutability is
defined by typed properties plus the least required topology. A point-set
kernel consumes a compatible `Property<T>`/`ConstProperty<T>` (or its span) on
*any* resolved element-domain `PropertySet`: mesh face centers in the Faces
property set are as valid as positions in the Vertices property set. The
handle-indexed `VertexProperty`, `FaceProperty`, and related wrappers are
container conveniences, never eligibility requirements. A graph kernel adds
the exact `Halfedges`/`Edges` adjacency and connectivity properties its
contract names; a mesh satisfies that graph contract through its existing
vertex/halfedge/edge sources. A mesh-only requirement is valid only when faces
or surface topology are actually semantic inputs.

Consequently, point-set methods are available on graphs and meshes, and graph
methods are available on meshes, whenever the requested property and topology
contract resolves. Exact provenance checks are valid only when provenance
itself changes algorithm semantics, never as a shortcut for asking whether a
property or adjacency source exists. Borrowing any mesh/graph property as a
sample span is a view of existing data, not a conversion.

Runtime owns the ECS-to-method binding and must use canonical property
references, catalogs, and geometry-source availability rather than rebuilding
provenance switches in each method. UI actions derive readiness and domain
placement from that same property/topology preflight. A method result publishes
named output properties to the originating element domain when cardinality is
preserved. Topology or cardinality changes are separate, explicit owning
operations with diagnostics and history semantics; they must not silently
replace a topology-rich entity merely because a kernel consumed point samples.

Method records name semantic input/output slots, while `GeometryPropertyRef`
names the physical property bound to each slot. The two names are deliberately
independent: a `Position` slot may bind `f:centroid` on `MeshFace`, and the
runtime passes that typed property/span directly. Do not copy or alias the
property into `v:position`, and do not create a PointCloud entity merely to
satisfy method vocabulary. Compatibility is determined from value kind,
component shape, element count/correspondence, mutability, finite-value policy,
and any topology the method explicitly requires—not from the property prefix.

## Property API contract

Geometry properties expose names as `std::string_view` borrowed from the owning
property storage. Property handles may copy the view cheaply, but callers must
not retain it past the lifetime of the owning `PropertySet`/domain object.
Use the canonical domain prefixes in property names: `v:` for vertex/node/point
attributes, `e:` for edges, `h:` for halfedges, `f:` for faces, and `c:` for
corners when a corner domain is explicitly present. Prefer the established
semantic names (`v:point`, `v:normal`, `e:length`, `f:area`, etc.) over local
aliases; compatibility aliases belong in explicit conversion code, not in
borrowed views.

Mutable property lookup returns `Property<T>`; const lookup returns
`ConstProperty<T>` and never grants mutable access through a const domain view.
Default-constructed and failed lookups are invalid handles that report empty
storage and no typed span. Public algorithms should test property validity
before reading or writing optional channels.

Public kernels that only need values should prefer generic
`Property<T>`/`ConstProperty<T>` or spans. `VertexProperty<T>`,
`FaceProperty<T>`, and other handle-indexed aliases are appropriate only when
the algorithm genuinely indexes with those handle types; runtime adapters must
not use the wrapper type to exclude an otherwise compatible property domain.

Typed scalar and vector properties expose contiguous `Data()`/`Span()` access
when their storage is valid. `bool` properties intentionally do not expose a
typed span because `std::vector<bool>` uses proxy references; use indexed
`Get()`/`Set()` for boolean channels such as feature masks. Erased property
inspection goes through descriptors that report the stable name, value kind,
type metadata, element count, and mutability without exposing writable erased
storage.

Every registry and typed storage also exposes a process-monotonic content
revision. Mutable element/vector/span/data access conservatively begins an edit
epoch; const access does not. Callers retaining a mutable borrow across a
method/render boundary must call `MarkModified()` after later writes. Runtime
and graphics consume these tokens according to the canonical
[geometry property CPU/GPU coherence contract](property-coherence.md); geometry
itself remains GPU-agnostic.

## Texture coordinates are corner-domain capable

Texture coordinates are not a vertex-only channel. A UV seam requires two or
more distinct UVs at one vertex, so a mesh that carries seams stores them as
`h:texcoord` on the halfedge/corner domain; `v:texcoord` remains correct — and
is what import publishes — only for meshes with no seam. This is the
representation OBJ already uses natively (`f v/vt/vn`); the vertex-domain
assumption was the lossy one.

The canonical resolution order is **corner over vertex**, so meshes migrate
incrementally rather than through a flag day:

1. a correctly sized `h:texcoord` wins;
2. otherwise a correctly sized `v:texcoord` applies;
3. otherwise the mesh has no UVs.

A property whose size does not match its domain is ignored rather than trusted,
so a stale buffer can never be read out of range.
`Geometry::MeshUtils::ResolveTexcoordDomain`, `TryGetCornerTexcoord`,
`HasTexcoordSeams`, and `CountTexcoordSplitVertices` implement this order and
are the only supported way to read a mesh's UVs. Do not write a new method,
exporter, or binding that reads `v:texcoord` directly.

The seam is a **UV fact, not a topology fact**. Publishing UVs must never change
element-domain cardinality: a closed manifold stays a closed manifold with the
same vertex/edge/halfedge counts, and the duplication an indexed GPU vertex
buffer needs happens once at upload, where one GPU vertex is emitted per
distinct `(vertex, UV)` pair. See `BUG-137`.

Two consequences for consumers:

- **A boundary is not a seam.** An operation that must know where the seams are
  reads them: on corner-owned UVs a seam vertex is one whose incident corners
  disagree on their UV. Treating boundary vertices as seams is a proxy that is
  only valid for vertex-owned UVs, where one vertex holds exactly one UV and an
  atlas has no choice but to cut the surface open. On a corner-owned mesh that
  proxy finds nothing, because there is no boundary to find — which is exactly
  how FA-QEM's `PreserveUvSeams` silently stopped protecting anything.
- **Every UV producer publishes over the source topology, not the unwrapper's.**
  An unwrapper emits a fresh output vertex per `(chart, source vertex)` pair, so
  its output mesh carries the seam as duplicated topology. That output is an
  intermediate: recover its per-corner UVs against the source faces and publish
  them over the mesh that kept its own topology. Publishing the output mesh
  itself converts a manifold into a triangle soup. This applies to every entry
  point — asset import and the editor's UV regeneration command alike — not
  only the one that was fixed first. See `BUG-137` and `BUG-147`.
- **Leave exactly one authority behind.** A producer that publishes UVs on one
  domain must retire the other domain's property when its result supersedes it.
  A parameterization computes one UV per vertex, so publishing `v:texcoord`
  beside a surviving `h:texcoord` would leave the corner property winning the
  resolution order and the result read by nothing. Undoable operations capture
  both domains so the retirement is reversible.

## Normals are corner-domain capable

Authored surface normals follow the same ownership rule as UV seams. OBJ
`f v/vt/vn` assigns a normal to each face corner, so a position referenced with
different normal values stores those values as count-matched `h:normal`; normal
identity must never enter an owning-topology remap key. The loader retains the
compact `v:normal` representation only for the unambiguous lockstep convention
where there is one `vn` per `v` and every corner refers to the normal with the
same index as its position.

Absent an explicit runtime channel override, normal consumers resolve **corner
over vertex**:

1. a correctly sized `h:normal` wins, with invalid or degenerate samples using
   the consumer's documented fallback;
2. otherwise a correctly sized `v:normal` applies under the same sample policy;
3. otherwise the consumer uses its documented derived/fallback normal policy.

Runtime materialization maps flattened payload corners onto live mesh
halfedges, and scene persistence retains that property on the halfedge domain.
Rendering and property-texture baking use the same canonical face/corner walk
and emit one GPU vertex per distinct `(mesh vertex, resolved UV, resolved
normal)` tuple. This split is render data only: the authoritative mesh keeps its
position indices, connectivity, manifold status, and every unrelated property.
See `BUG-154`.

## Naming and count terminology

- Prefer `PascalCase` for public functions and methods, matching the dominant
  promoted geometry style.
- Prefer noun accessors such as `Nodes()` or `Elements()` for cheap views. Use
  `Get*` only when matching an existing local API family or when the operation is
  not a trivial accessor.
- Use `Size()` for storage slots, including deleted or inactive slots when a
  container has sparse handles.
- Use `Count()` for live logical elements.
- Use `Capacity()` only for reserved storage capacity.
- Name conversion APIs to reveal ownership: use `View`/`Borrow` for no-copy
  adaptation, `To*`/`From*` for owning copies, and `Consume` only for intentional
  ownership transfer.

## Failure reporting and diagnostics

New public APIs should preserve enough information for deterministic tests,
method comparisons, and paper-result diagnostics:

- Use `Core::Expected<T>` for operations that can fail with caller-visible error
  conditions and need a result value.
- Use structured result records for algorithms that can partially succeed,
  iterate, converge, reject preconditions, or produce diagnostics such as counts,
  residuals, thresholds, or topology changes.
- Use status or error enums only when the result type is otherwise obvious and the
  enum values are documented with stable meanings.
- Use `std::optional<T>` only for trivial lookup-style APIs where absence is the
  sole expected outcome and no additional diagnostic is useful.
- Avoid `bool` failure returns for new algorithms unless the operation is a local
  predicate and cannot usefully report why it failed.
- Assertions may guard programmer errors and internal invariants, but malformed
  input, numerical singularity, unsupported topology, and non-convergence should
  be reported through public diagnostics.

Diagnostics should be deterministic: avoid locale-dependent formatting, hidden
randomness, or dependence on traversal order unless that order is specified.

## Numeric policy

- Use float `glm::vec*` storage for public and persisted geometry vector
  properties, including positions, centroids, directions, normals, colors, and
  renderer/method-facing data. Convert at publication rather than exposing an
  internal `glm::dvec*` representation. A deliberate public double-vector
  property requires a scoped API decision plus typed catalog/serialization/UI
  support; it is never the incidental default.
- Use `double`/`glm::dvec*` internally for numerical kernels where conditioning,
  accumulation, residuals, or predicate stability matters.
- Expose tolerances as named parameters or policy records when callers may need to
  reproduce results across datasets or scales.
- Choose tolerances from documented scale assumptions. Prefer scale-normalized
  thresholds for algorithms that operate on arbitrary model units.
- Report degeneracy, rejected elements, singular pivots, residuals, and iteration
  limits through diagnostics when they affect results.
- Randomized algorithms must accept deterministic seed/state input and document
  whether outputs are stable across platforms.
- Future robust-predicate work should provide orientation, incircle/insphere,
  intersection, barycentric, and epsilon/scale-aware comparison utilities before
  expanding boolean, remeshing, arrangement, or reconstruction kernels.
  Orientation 2D/3D, signed-distance, in-plane triangle barycentric
  classification, and scale-aware epsilon helpers landed in
  `Geometry.RobustPredicates` (see [Geometry Architecture →
  Robust predicates](geometry.md#robust-predicates)). The records-only
  sibling `Geometry.IntersectionClassification` adds the segment/segment,
  segment/triangle, ray/triangle, triangle/triangle, and point/triangle
  result vocabulary (see [Geometry Architecture → Intersection
  classification records](geometry.md#intersection-classification-records)).
  Incircle/insphere predicates and adaptive-exact escalation remain open
  under [`GEOM-007`](../../tasks/archive/GEOM-007-robust-predicates-intersection-classification.md):
  incircle/insphere are out of the current slice plan and tracked as a
  follow-up; adaptive-exact escalation is Slice 4.

The geometry numerical policy is hybrid GLM + Eigen3: GLM remains the public
geometry storage vocabulary, while Eigen is available behind geometry-owned
adapters for CPU linear-algebra kernels. `Geometry.Linalg` is the explicit narrow
import for Eigen-backed fixed-size adapters, row-major map helpers, and dense
decomposition wrappers. Do not expose Eigen types through the broad `Geometry`
umbrella or existing geometry containers without a separate API-review task.

## `Geometry.LinearSolver` policy

`Geometry.LinearSolver` is currently a narrow public module interface listed in
`src/geometry/CMakeLists.txt` and the generated module inventory, but it is not
re-exported by the broad `Geometry` umbrella. Treat it as an advanced narrow
import for the current small fixed-size solver helper, not as the canonical public
solver infrastructure.

Reusable sparse solver work lives in `Geometry.Sparse`, with `Geometry.DEC`
aliases preserving the existing DEC names. Do not broaden or remove
`Geometry.LinearSolver` as part of unrelated algorithm changes.

## Compatibility and migration

Existing geometry inconsistencies are compatibility debt, not permission for
opportunistic cleanup. Follow these rules:

- Record style migrations as separate tasks when they touch public names,
  modules, namespaces, or generated inventories.
- Keep stale comments and local naming cleanups mechanical and isolated.
- Do not change behavior while moving files or renaming modules.
- Update `docs/api/generated/module_inventory.md` whenever module surfaces change.
- Update architecture and task docs in the same change when a policy decision or
  dependency boundary changes.
