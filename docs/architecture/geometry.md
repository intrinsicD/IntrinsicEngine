# Geometry Architecture

`geometry` is the canonical home for geometry-processing algorithms and mesh-domain operations.

## Responsibilities

- Deterministic geometric kernels and data transformations.
- Robust handling of degenerate/non-ideal input cases.
- Integration seams for method packages and benchmark harnesses.

## Dependencies

- Allowed: `core`.
- Disallowed: runtime/app-specific ownership and rendering backend internals.

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

## Migration note

As of RORG-093, canonical Geometry code is promoted to `src/geometry`. Remaining `src/legacy` geometry shims (if any) must be temporary, tracked, and removed via follow-up migration tasks.

## Related reviews

- [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md) records current style/API inconsistencies, missing reusable data structures, and algorithm gaps for modern geometry-processing paper work.

