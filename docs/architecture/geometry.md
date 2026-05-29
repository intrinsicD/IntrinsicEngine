# Geometry Architecture

`geometry` is the canonical home for geometry-processing algorithms and mesh-domain operations.

## Responsibilities

- Deterministic geometric kernels and data transformations.
- Robust handling of degenerate/non-ideal input cases.
- Integration seams for method packages and benchmark harnesses.

## Dependencies

- Allowed: `core`, GLM, and Eigen3 for geometry-owned CPU numerical kernels.
- Disallowed: runtime/app-specific ownership and rendering backend internals.

## Linear algebra policy

- GLM remains the public storage vocabulary for geometry containers, primitive
  records, and renderer-facing data.
- Eigen3 is available only behind geometry-owned numerical modules. The broad
  `Geometry` umbrella exports `Geometry.Sparse` because sparse CSR builders and
  diagnostics are engine-facing geometry utilities, but it does not re-export the
  Eigen-backed `Geometry.Linalg` module; callers that need fixed-size
  GLM/Eigen adapters or dense decompositions must import `Geometry.Linalg`
  explicitly.
- `Geometry.Linalg` is the narrow advanced numerical surface for CPU kernels. It
  exposes GLM round-trip adapters, explicit row-major `Eigen::Map` helpers for
  contiguous scalar buffers, and dense decomposition wrappers that return
  geometry-owned diagnostics rather than raw Eigen solver state.
- `Geometry.Sparse` owns reusable CSR storage, COO-to-CSR building, matrix
  diagnostics, and conjugate-gradient diagnostics. `Geometry.DEC` aliases these
  sparse records so existing DEC/geodesic/parameterization callers keep their
  names while sharing the common implementation.
- Optional Spectra or SuiteSparse/CHOLMOD seams are deferred until CPU reference
  parity and benchmark manifests justify a second backend.

## API style, diagnostics, and numeric policy

New or materially changed geometry APIs must follow the
[Geometry API Style and Numeric Policy](geometry-api-style.md). The policy covers
module/file/namespace alignment, public state and mutability, count terminology,
failure reporting, deterministic diagnostics, numeric tolerances, and the current
`Geometry.LinearSolver` narrow-module decision.

## Topology connectivity ownership

- `Geometry::Graph::VertexConnectivity` and `Geometry::Graph::HalfedgeConnectivity`
  are the canonical face-free traversal records for vertex-to-halfedge and
  halfedge-to-vertex/next/prev links.
- `Geometry::HalfedgeMesh` reuses those graph traversal records for
  `v:connectivity` and `h:connectivity`, so mesh-backed graph views can share
  topology storage without compatibility-copy properties.
- Mesh-only face incidence is stored separately in
  `Geometry::HalfedgeMesh::HalfedgeFaceConnectivity` under `h:face`; graph
  connectivity must not grow face ownership fields.

## Mesh, graph, and point-cloud domain views

`Geometry::HalfedgeMesh::Mesh`, `Geometry::Graph::Graph`, and
`Geometry::PointCloud::Cloud` should be treated as peer geometry domains. New
algorithms should request the least structured domain they need:

- point-sample algorithms use point-cloud/domain-position views;
- edge/topology traversal algorithms use graph views;
- face/topological editing algorithms use mesh views.

When a richer domain is passed to a less-structured algorithm, prefer an
explicit borrowed view over a hard copy when all required semantic properties are
already present and lifetime is clear. For example, mesh-backed graph algorithms
can share mesh vertex, halfedge, and edge property sets because mesh traversal
connectivity reuses the canonical graph connectivity records.

Borrowed views must be explicit and documented as either read-only or mutable:

- read-only algorithms should use const/borrowed views and must not mutate source
  storage;
- mutable algorithms may borrow source storage only when mutation is the
  documented primary effect;
- algorithms that change topology/cardinality, require independent lifetime, or
  need different attribute layouts should perform an explicit hard-copy
  conversion and report conversion diagnostics;
- move/consume APIs are reserved for ownership transfer into result containers,
  not for temporary adaptation.

Mesh, graph, and point-cloud vertex positions use the canonical `v:point`
`glm::vec3` property. Point-cloud adapters must not allocate a separate
`p:position` property on shared vertex storage; any legacy `p:position` data
must be handled by an explicit compatibility/conversion path rather than by a
borrowed view.

### Domain-view module: `Geometry.DomainViews`

The named bridge for symmetric, no-copy domain views lives in
`Geometry.DomainViews`. It depends on `Geometry.HalfedgeMesh`, `Geometry.Graph`,
and `Geometry.PointCloud`:

- `Geometry::DomainViews::BorrowMeshAsGraphReadOnly(const HalfedgeMesh::Mesh&) -> Graph::Graph`
  returns a `Graph::Graph` sharing the source mesh's vertex, halfedge, and edge
  `PropertySet`s and the deleted-vertex/edge counters. The canonical `v:point`,
  `v:connectivity`, `h:connectivity`, `v:deleted`, and `e:deleted` slots are
  reused — no `*_graph_*` compatibility-copy slots are allocated. Face storage
  (`h:face`, `f:connectivity`, `f:deleted`, `Mesh::FacesSize()`, and
  `Mesh::DeletedFaceCount()`) is **not** part of the view.
- `Geometry::DomainViews::BorrowMeshAsCloud(HalfedgeMesh::Mesh&) -> PointCloud::Cloud`
  returns a `PointCloud::Cloud` sharing the source mesh's vertex
  `PropertySet`. The canonical `v:point` slot is reused — no `p:position`
  compatibility-copy slot is allocated. Existing per-vertex attributes (for
  example `v:normal`) are reachable through the cloud's `GetVertexProperty<T>`
  accessor over the shared `PropertySet`. The returned cloud owns its own
  deletion counter; cloud-side deletes mark `p:deleted` on the shared
  `PropertySet` but do **not** increment `mesh.DeletedVertexCount()`, so the
  mesh's `VertexCount()` and `HasGarbage()` continue to reflect only
  mesh-side `v:deleted` semantics. The cloud's `p:deleted` marker is
  independent from the mesh's `v:deleted` and the mesh never reads
  `p:deleted`. Route topology-aware deletion through `Mesh::DeleteVertex` /
  `Mesh::GarbageCollection`; calling `Cloud::GarbageCollection` on a
  mesh-backed borrow is undefined behavior on face-bearing source meshes
  because it physically reshuffles and resizes vertex slots and would
  invalidate mesh halfedge/edge connectivity that references vertex indices.
  `Cloud::AddPoint` appends a row to the shared vertex `PropertySet`; the
  new vertex is isolated (no incident halfedges) so face-bearing source
  meshes are not corrupted. `Cloud::CreateView` is well-defined on the
  returned cloud: subrange clamping and the returned view's bound storage
  both follow the mesh-backed `v:point` data rather than the cloud's empty
  owning `Properties`.
- `Geometry::DomainViews::BorrowGraphAsCloud(Graph::Graph&) -> PointCloud::Cloud`
  is the symmetric companion that returns a `PointCloud::Cloud` sharing the
  source graph's vertex `PropertySet`. The canonical `v:point` slot is reused —
  no `p:position` compatibility-copy slot is allocated — and existing per-vertex
  attributes (for example `v:normal`) are reachable through the cloud's
  `GetVertexProperty<T>` accessor. Only the vertex `PropertySet` is borrowed:
  the graph's halfedge/edge storage (`h:connectivity`, `e:deleted`) and the
  graph-domain `v:connectivity` slot are **not** exposed through the cloud
  surface, and the graph's `EdgesSize()`/`HalfedgesSize()` are untouched by
  cloud-side operations. The returned cloud owns its own deletion counter;
  cloud-side deletes mark `p:deleted` on the shared `PropertySet` but do
  **not** touch the graph's `v:deleted` counter, so the graph's
  `VertexCount()` and `HasGarbage()` continue to reflect only graph-side
  semantics. Route topology-aware deletion through `Graph::DeleteVertex` /
  `Graph::GarbageCollection`; calling `Cloud::GarbageCollection` on a
  graph-backed borrow is undefined behavior on an edge-bearing source graph
  because it physically reshuffles and resizes vertex slots and would
  invalidate graph halfedge/edge connectivity that references vertex indices.
  `Cloud::AddPoint` appends a row to the shared vertex `PropertySet`; the new
  vertex is isolated (no incident halfedges) so edge-bearing source graphs are
  not corrupted.

The factory accepts face-bearing meshes for graph-domain reads and vertex-
position writes (e.g. `Geometry::ShortestPath::Dijkstra`, `SetVertexPosition`).
Topology mutation through the returned graph — `AddVertex`, `AddEdge`,
`DeleteVertex`, `DeleteEdge`, `GarbageCollection`, `Clear`, `SetNextHalfedge`,
`SetPrevHalfedge`, `SetVertex`, and `SetHalfedge` — updates only the
vertex/halfedge/edge property sets and the deleted-vertex/edge counters; it
cannot observe or update face incidence and would corrupt a face-bearing
source mesh by leaving `h:face`/`f:connectivity`/`f:deleted`/`FacesSize()`
stale. Route topology changes through the mesh's own
`Mesh::DeleteEdge`/`DeleteVertex`/`DeleteFace`/`GarbageCollection`
operations, which cascade through face incidence. Vertex-position writes do
not change topology and are explicitly allowed.

The const-reference parameter is the safety intent signal; the returned
`Graph::Graph` is mutable because position writes go through the same type. A
compile-time-checked distinct read-only view type, and any future
face-free-only mutable-borrow factory, are owned by later GEOM-012 slices
(D and B/C respectively). The source mesh must outlive the view, mirroring
`HalfedgeMesh::Mesh::CreateView`.

## Indexed mesh and polygon-soup staging

`Geometry::MeshSoup::IndexedMesh` is the lightweight owning container for
algorithms and import/reconstruction stages that need positions plus indexed
triangle or polygon faces but do not require halfedge connectivity. The
container stores canonical vertex positions in the `v:point` `PropertySet`
entry and owns separate `PropertySet` domains for vertices, faces, and corners.
`Geometry::MeshSoup::Validate` returns structured diagnostics for duplicate
vertices, invalid indices, degenerate faces, non-manifold edges, inconsistent
winding, and property-domain arity mismatches.

No-copy adaptation must be named as borrowing, such as
`Geometry::MeshSoup::BorrowView`, while topology-changing or lifetime-owning
conversion APIs should use explicit `To*`/`From*` names and report diagnostics.
`Geometry.Mesh.Conversion` owns the explicit MeshSoup ↔ HalfedgeMesh conversion
surface so the core container modules do not import each other only for
adaptation convenience. `Geometry::Mesh::Conversion::ToHalfedgeMesh` and
`ToIndexedMesh` return result objects with structured conversion diagnostics
rather than silent `bool`/`std::optional` failure. Soup validation errors stop
soup-to-halfedge conversion before topology is built; halfedge-to-soup
conversion preserves canonical `v:point` positions and reports warnings when
deleted source elements force compaction or when generic attributes stay on the
source container.

`Geometry.PointCloud.Conversion` owns the explicit MeshSoup ↔ PointCloud
conversion surface for cases where positions are the only shared shape (no
topology, no face/corner domain). `Geometry::PointCloud::Conversion::ToIndexedMesh`
copies cloud positions into a face-less soup and reports
`DeletedPointsOmitted` and `AttributeRemapSkipped` warnings when the source
cloud has deleted points or generic per-point attributes;
`Geometry::PointCloud::Conversion::ToPointCloud` copies soup positions into a
cloud, reports `FacesDropped` when topology is discarded, and reports
`AttributeRemapSkipped` when generic vertex/face/corner attributes remain on
the source soup. Renderer-upload staging remains a planned geometry-owned
data-shape contract; geometry must not import assets, graphics, runtime, ECS,
platform, or app layers to satisfy renderer staging needs.

## Robust predicates

`Geometry.RobustPredicates` is the narrow predicate foundation introduced by
[`GEOM-007`](../../tasks/active/GEOM-007-robust-predicates-intersection-classification.md)
Slice 1. It is **not** re-exported by the broad `Geometry` umbrella; callers
must `import Geometry.RobustPredicates;` explicitly. Surface:

- `Sign` (`Negative`/`Zero`/`Positive`) and `Certainty`
  (`Certain`/`Uncertain`) diagnostic enums.
- `SignedResult { Value, Sign, Certainty, FilterBound }` for orientation and
  signed-distance predicates.
- `Orientation2D` and `Orientation3D` Shewchuk-style filtered predicates
  evaluated in `double` from `glm::vec*<float>` inputs.
- `SignedDistanceToPlane(origin, unitNormal, query)`.
- `ClassifyTriangleBarycentric(a, b, c, query)` returning
  `BarycentricResult { Region, WA, WB, WC, PlaneDistance }` with
  `BarycentricRegion ∈ {VertexA/B/C, EdgeAB/BC/CA, Interior, Outside, Degenerate, Uncertain}`.
- Scale-aware helpers `ScaledEpsilon(scale, relative)` and `ApproxEqual`.

Numerical policy and limitations:

- The current implementation is filtered double precision. Inputs landing
  inside the filter band are reported with `Certainty::Uncertain`; callers
  must not silently coerce uncertain results into a hard sign.
- Exact / adaptive Shewchuk-style escalation is a `GEOM-007` Slice 4
  follow-up. Mesh-boolean and arrangement kernels needing guaranteed signs
  must add snap-rounding or symbolic-perturbation pre-passes until that
  slice lands.
- Intersection-classification result records (segment/segment,
  segment/triangle, ray/triangle, triangle/triangle, point/edge/face
  incidence) are deferred to `GEOM-007` Slice 2 and will live in a sibling
  module rather than being added to `Geometry.RobustPredicates`.
- Callsite adoption (e.g. `Geometry.Raycast`, `Geometry.Overlap`,
  `Geometry.Containment`, `Geometry.GJK`) is gated on Slices 1–2 and tracked
  as `GEOM-007` Slice 3; this Slice 1 add deliberately does not migrate
  existing callers in order to keep foundation-add and semantic refactor
  separate per the contract in [`AGENTS.md`](../../AGENTS.md) §5.

## GJK tolerance contract

`Geometry.GJK` uses a single dimensionless convergence tolerance
`Geometry::Internal::Config::GJK_EPSILON = 1e-6f` (`src/geometry/Geometry.GJK.cppm`)
for every termination / progress / duplicate-membership / segment-degeneracy
test in the simplex evolution.
[`GEOM-015`](../../tasks/done/GEOM-015-gjk-termination-diagnostics.md)
Slice 3 pins the contract for that constant:

- **Normalized workspace.** `GJK_Boolean` and `GJK_Intersection` compute
  `invScale = 1 / |initial support|` from the first Minkowski-difference
  support point and multiply every subsequent support by `invScale` before
  it enters any `GJK_EPSILON`-bearing predicate. The simplex therefore
  lives in `~unit` space and `GJK_EPSILON` is a dimensionless tolerance on
  that workspace, not a length / magnitude in original shape space.
- **Why not thread a per-call scale into the driver.** The Slice 2
  callsite audit (recorded in the GEOM-015 task file) confirmed that all
  seven `GJK_EPSILON` consumers operate in this normalized workspace and
  none of them want an original-space magnitude. Threading a per-call
  scale into the GJK driver would double-normalize (the driver already
  factors the scale out via `invScale`) and re-introduce the scale
  dependence Slice 2 just removed. The decision is to keep `GJK_EPSILON`
  as a normalized-space constant and document the contract explicitly,
  rather than thread a redundant scale through the driver.
- **Where scale-aware tolerances live.** Original-shape-space zero-vector
  guards (Capsule / Cylinder / Ellipsoid / SDFContact) live in
  `Geometry.Support` and `Geometry.SDFContact`, not in `Geometry.GJK`.
  Those guards were migrated to
  `Geometry::RobustPredicates::ApproxZeroSq` in GEOM-015 Slice 2 with
  primitive-local `scale` choices recorded in the task notes; they do
  not flow through `GJK_EPSILON`.
- **Static pin.** A `static_assert(GJK_EPSILON > 0 && GJK_EPSILON < 1)` in
  `Geometry.GJK.cppm` forces future edits to keep the value inside the
  dimensionless `(0, 1)` band that the normalized-workspace contract
  requires. Any future migration to a magnitude tolerance must revisit
  this section and the GEOM-015 callsite audit before changing the
  constant.
- **Termination diagnostics.** GEOM-015 Slice 4 surfaces the GJK
  iteration count and termination reason via the
  `Geometry::Internal::GJKDiagnostics` out-param (with fields
  `iterations` and `reason ∈ { Converged, EarlyOutNegativeSupport,
  NoSimplexProgress, MaxIterationsHit }`). Both `GJK_Boolean` and
  `GJK_Intersection` gained a four-argument overload taking the
  diagnostics by reference; the existing two- and three-argument entry
  points stay as thin wrappers and produce byte-identical boolean
  outcomes. Callers that only want overlap continue using the existing
  entry points; callers that need to distinguish a geometric "no
  overlap" (`EarlyOutNegativeSupport`) from a numerical fallback
  (`NoSimplexProgress`, `MaxIterationsHit`) opt in via the
  diagnostic-bearing overload without re-running GJK. A parity test
  battery in `tests/unit/geometry/Test_GJK.cpp` exercises boolean
  outcomes across small (`~1e-3`) and large (`~1e3`) shape scales,
  including a touching-sphere case and a near-touching-separation case
  that exercises the previously-fragile sub-millimetre regime; an
  iteration-budget regression test pins the practical iteration count
  on the standard primitive corpus well below
  `Config::GJK_MAX_ITERATIONS`.

## Intersection classification records

`Geometry.IntersectionClassification` is the records-only sibling module
introduced by [`GEOM-007`](../../tasks/active/GEOM-007-robust-predicates-intersection-classification.md)
Slice 2. Like `Geometry.RobustPredicates`, it is **not** re-exported by the
broad `Geometry` umbrella; callers must `import
Geometry.IntersectionClassification;` explicitly. The module ships data
records only — no intersection algorithm implementations — so it can land
without rewriting existing callers. Surface:

- `Kind` (`None`/`Proper`/`Touching`/`Overlap`/`Coplanar`/`Coincident`/
  `DegenerateInput`/`Uncertain`) is the shared intersection-outcome vocabulary.
- `SegmentFeature`, `RayFeature`, and `TriangleFeature` enums identify which
  boundary feature of an operand is involved in a `Touching`/`Coplanar`
  outcome.
- Result records:
  - `SegmentSegmentResult { Kind, OnA, OnB, ParamA, ParamB, Point, OverlapStart, OverlapEnd }`,
  - `SegmentTriangleResult { Kind, OnSegment, OnTriangle, SegmentParam, WA/WB/WC, Point, OverlapStart, OverlapEnd }`,
  - `RayTriangleResult { Kind, OnRay, OnTriangle, RayParam, WA/WB/WC, Point }`,
  - `TriangleTriangleResult { Kind, OnA, OnB, ContactStart, ContactEnd, IsCoplanar }`,
  - `PointTriangleResult { Kind, Feature, PlaneSide, PlaneSideCertainty, SignedPlaneDistance, WA/WB/WC }`.
- Free helpers: `HasIntersection(Kind)`, `IsAmbiguous(Kind)`, and
  `TriangleFeatureFromBarycentric(BarycentricRegion)`.

Defaulting policy:

- Every record defaults to `Kind::Uncertain`, every feature field to
  `…::None`, every scalar to a `kUnspecified` quiet-NaN, and every point to
  the origin. Callers can therefore never accidentally consume an unwritten
  record as a valid intersection.
- All records are trivially copyable (POD-shaped data envelopes); future
  benchmark/serialization callers can rely on this.

Numerical-uncertainty propagation:

- When the underlying `Geometry.RobustPredicates` evaluation cannot decide
  an outcome inside the filter band, the implementation should set
  `Kind::Uncertain` and leave the geometric fields at their defaults rather
  than guess. `PointTriangleResult` additionally carries the
  `PlaneSide`/`PlaneSideCertainty` pair so callers can inspect the
  underlying predicate diagnostic without re-running the predicate.

Callsite adoption (`Geometry.Raycast`, `Geometry.Overlap`,
`Geometry.Containment`, `Geometry.GJK`, etc.) is tracked as `GEOM-007`
Slice 3; this Slice 2 add deliberately does not migrate existing callers in
order to keep foundation-add and semantic refactor separate per the
contract in [`AGENTS.md`](../../AGENTS.md) §5.

### Callsite adoption (Slice 3)

Slice 3 migrates existing geometry callsites onto the Slice 1 predicates and
Slice 2 records one at a time. The general adoption pattern, demonstrated
first by `Geometry::RayTriangle_Classify`, is:

- Keep the existing entry point in place and unchanged (e.g.
  `RayTriangle_Watertight`).
- Add a sibling classifying entry point whose name mirrors the existing one
  with a `_Classify` suffix and which returns the appropriate
  `Intersection::*Result` record.
- Implement the classifying entry point by reusing the existing numerical
  kernel — typically by calling the legacy function and folding its output
  into the result record — so geometric fields (parameters, weights, hit
  position) are bit-exact identical between the two paths.
- Map the legacy "missing" / "degenerate" / "out of range" return states
  onto `Kind::None` / `Kind::DegenerateInput` / `Kind::None` respectively
  so callers can distinguish "no intersection" from "unanswerable question".
- Add a parity test file `Test.<Caller>Classify.cpp` that pins the bit-exact
  geometric agreement plus boundary classification against the same inputs
  the legacy tests use.

Existing callers (typically in `src/legacy/` for now) stay on the legacy
entry point until their own per-callsite Slice 3.x commit replaces the call.
The legacy entry point is removed only when every caller has migrated.

## Migration note

As of RORG-093, canonical Geometry code is promoted to `src/geometry`. Remaining `src/legacy` geometry shims (if any) must be temporary, tracked, and removed via follow-up migration tasks.

## Related reviews

- [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md) records current style/API inconsistencies, missing reusable data structures, and algorithm gaps for modern geometry-processing paper work.
- [`Point-cloud algorithm roadmap`](point-cloud-algorithm-roadmap.md) splits the
  point-cloud gaps into method-compliant filtering, descriptor/registration,
  fitting, smoothing, and reconstruction packs without claiming those packs are
  already implemented.
- [`Parameterization and mapping roadmap`](parameterization-mapping-roadmap.md)
  splits UV parameterization, atlas, distortion, and surface-map gaps into
  method-compliant diagnostics, harmonic/Tutte, ARAP/SLIM, charting, and map
  representation packs without claiming those packs are already implemented.

