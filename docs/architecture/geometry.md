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
- `Geometry.Rotation` owns the shared SO(3) primitive surface: hat/vee, exp/log,
  geodesic and chordal distances, deterministic seeded random rotations,
  `ProjectOnSO3`, and optimal-rotation/Kabsch helpers for corresponded point
  sets. Rotation-valued routines fail closed to finite SO(3) sentinels rather
  than returning NaNs. `ProjectOnSO3` delegates the orthogonal projection to
  `Geometry.Linalg::ComputePolarDecomposition(...).Orthogonal` and applies a
  determinant correction to stay in SO(3). `Geometry.Registration` consumes this
  module for point-to-point alignment instead of keeping a private Kabsch copy.
- `Geometry.RotationAveraging` builds on `Geometry.Rotation` and
  `Geometry.Linalg` for deterministic CPU SO(3) averaging. Its chordal L2 mean
  uses the Markley 4x4 quaternion-moment matrix and
  `Geometry.Linalg::ComputeSymmetricEigen`; Karcher means iterate with
  `Log(meanᵀR_i)` / `Exp(delta)` in the tangent space; quaternion means use
  hemisphere-aligned linear quaternion accumulation; geodesic and quaternion L1
  medians use Weiszfeld inverse-distance reweighting. All five routines accept
  `RotationAverageOptions` (`Weights`, `MaxIterations`, `Tolerance`, optional
  `OutlierRejectionRadians`) and return `RotationAverageResult` with status,
  validity, convergence, iteration count, and residual radians. Empty input,
  weight-size mismatches, invalid weights, non-finite matrices, cut-locus
  two-sample degeneracy, invalid options, and non-convergence report explicit
  `RotationAverageStatus` values and return finite identity or last-iterate
  sentinels without asserts or NaNs. Single finite samples return unchanged with
  `SingleSample`. Registration weighting remains a GEOM-048 follow-up.
- `Geometry.Sparse` owns reusable CSR storage, COO-to-CSR building, matrix
  diagnostics, and conjugate-gradient diagnostics. `Geometry.DEC` aliases these
  sparse records so existing DEC/geodesic/parameterization callers keep their
  names while sharing the common implementation.
- Optional Spectra or SuiteSparse/CHOLMOD seams are deferred until CPU reference
  parity and benchmark manifests justify a second backend.
- `Geometry.PCA` exports the closed-form symmetric 3×3 eigensolver
  `Geometry::PCA::SymmetricEigen3` (and its `Geometry::PCA::Eigen3` result). It is
  the single shared solver behind both `ToPCA` and the curvature-tensor
  decomposition; consumers that need signed eigenvalues (the curvature tensor is
  not PSD) read them off the matrix via the Rayleigh quotient, because
  `SymmetricEigen3` clamps its returned eigenvalues non-negative for the
  covariance use case. No public Eigen types appear on the `Geometry.PCA` surface.

## API style, diagnostics, and numeric policy

New or materially changed geometry APIs must follow the
[Geometry API Style and Numeric Policy](geometry-api-style.md). The policy covers
module/file/namespace alignment, public state and mutability, count terminology,
failure reporting, deterministic diagnostics, numeric tolerances, and the current
`Geometry.LinearSolver` narrow-module decision.

### Property-system contracts

`Geometry.Properties` exposes property names as `std::string_view` borrows tied to
the owning property registry, so callers can inspect names without copying while
the property remains alive. Const property-set lookups return read-only property
handles and default-constructed `ConstPropertySet` values behave as safe empty
views. `PropertySet::Descriptors()` reports erased property metadata including
name, value kind, element count, and mutability so runtime/editor inspection can
enumerate geometry attributes without RTTI. `LiveElementRange` is the shared
handle iteration helper behind mesh, graph, point-cloud, and const domain-view
live-element accessors.

### Algorithm backend seams

`Geometry.KMeans` is the canonical geometry exemplar for the
[Algorithm Variant Dispatch Pattern](algorithm-variant-dispatch.md). The geometry
module owns the deterministic CPU reference path and does not import RHI. Its
`KMeansParams::Compute` field is the requested backend (`Backend::CPU` or
`Backend::GPU`), while `KMeansResult` reports `RequestedBackend`,
`ActualBackend`, and `FellBackToCPU` so a GPU request that runs on CPU is never
silent.

The RHI-visible integration hook lives in runtime:
`Extrinsic.Runtime.KMeansBackend::ClusterKMeans(...)` accepts
`Extrinsic::RHI::IDevice&`, evaluates `IDevice::IsOperational()` for GPU
requests, and currently falls back to the CPU reference because no KMeans GPU
kernel has landed. A real GPU backend must arrive as a separate parity-gated
task.

### Geometry IO coverage

`Geometry.HalfedgeMesh.IO` owns mesh OBJ/OFF/STL/PLY import and mesh
OBJ/OFF/STL/PLY export. The OFF path is symmetric at the geometry module level:
`WriteOFF` emits deterministic ASCII OFF records for finite meshes, rejects
empty or invalid topology, and round-trips through the hardened `LoadOFF`
reader.

`Geometry.PointCloud.IO` owns point-cloud XYZ/PTS/PWN/CSV/3D/TXT/PLY/PCD
import plus XYZ/PLY/PCD export. The PWN reader consumes a count header followed
by point rows and normal rows; CSV and 3D inputs may carry normals through their
six-column layouts; PTS and TXT validate their count/intensity/color/reflectance
columns and store supported color channels. The additional ASCII readers share
one strict scanner and fail closed on empty, malformed, wrong-column, or
non-finite inputs.

This is module-level geometry coverage. Asset/runtime routing remains a
separate layer concern and should not be inferred from the existence of a
geometry reader or writer.

### Remeshing, subdivision, and mesh topology utilities

`Geometry.HalfedgeMesh.AdaptiveRemeshing` exposes `ReferenceProjector`, a
frozen-surface nearest-face projector backed by `Geometry.MeshClosestFace`, and
supports both mean-curvature sizing and `SizingLaw::ErrorBoundedTaubin`.
Reference projection is also available to uniform remeshing through
`Geometry.Remeshing::RemeshingParams::ProjectToSurface` and related projection
limits.

`Geometry.MeshClosestFaceIndex` is the shared CPU exact nearest-face query for
mesh consumers. It builds a `Geometry.BVH` over per-face AABBs and evaluates
exact point-to-triangle distance for nearest, k-nearest, and radius queries;
polygon faces are fan-triangulated while faces with no finite non-degenerate
triangle are skipped. Results carry the `FaceHandle`, closest point, face normal,
fan-triangle primitive index, exact squared distance, explicit status, and
`Geometry.SpatialQueries` diagnostics. Adaptive remeshing reference projection,
implicit plane-field closest-point evaluation, simplification Hausdorff
redistribution, and `Geometry.HalfedgeMesh.Utils::NearestFace` all consume this
packaged query instead of private brute-force face scans.

`Geometry.Subdivision` implements Loop subdivision with optional feature-edge
preservation from a caller-selected boolean edge property, defaulting to
`e:feature`. Feature split edges remain tagged on output. `Geometry.SubdivisionSqrt3`
adds Kobbelt sqrt(3) subdivision for triangle meshes, including centroid split,
old-vertex relaxation, original interior-edge flips, and boundary handling.

`Geometry.HalfedgeMesh::Mesh` publishes core topology helpers for polygon
`Triangulate`, conservative `IsRemovalOk`, intrinsic `IsDelaunay`, conditional
`DelaunayFlip`, direct `EdgeLength`, and `UpdateEdgeLengths`. `UpdateEdgeLengths`
recomputes the canonical `e:length` `double` edge property; the cache is not
automatically invalidated, so callers that mutate topology or positions must
refresh it before consuming the property. `Geometry.MeshRepair` provides
deterministic connected-component labels (`v:component`, `f:component`),
component splitting, and keep-largest-component cleanup. `Geometry.HalfedgeMesh.Utils`
provides dual construction, triangle-adjacency index buffers with boundary edges
encoded by the triangle's own opposite vertex, and `NearestFace`, which delegates
to the accelerated `Geometry.MeshClosestFace` query.

### Graph and point-cloud query/noise utilities

`Geometry.Graph.Utils` publishes the graph-side `e:length` `float` cache through
`EnsureEdgeLengths` and the non-caching `FillEdgeLengths`. Both fail closed on
empty graphs, invalid edges, non-finite endpoints, zero-length edges, or
undersized caller buffers. Edge spatial queries (`ClosestEdge`, `KClosestEdges`,
`EdgesWithinRadius`, and `ClosestEdgeWithinOneRing`) use
`Geometry.Queries::ClosestPointSegment` for exact segment distance and a
`Geometry.BVH` over edge segment AABBs for candidate enumeration; returned sets
are ordered by squared distance with ascending edge handle tie-breaks.

Graph and point-cloud Gaussian augmentation is deterministic and true-Gaussian:
each element seeds its own RNG from `(Seed, element index)` and draws independent
per-component normal samples. Graph displacement standard deviation is
`StdDevFraction * vertex-AABB diagonal`; point-cloud displacement standard
deviation is `StdDevFraction * ComputeStatistics(...).AverageSpacing`. A zero
fraction is an identity operation, while empty input, non-finite positions,
negative/non-finite fractions, and non-zero requests with degenerate scale report
explicit status values.

`Geometry.PointCloud.SurfaceSampling` converts a triangle `HalfedgeMesh::Mesh`
into a deterministic dense `PointCloud::Cloud` by area-weighted face selection
and sqrt-corrected barycentric sampling. The API returns a result record rather
than throwing: invalid sample counts, empty meshes, and meshes with no valid
triangles fail closed with explicit status and diagnostics. Diagnostics count
requested/written samples, total/accepted faces, rejected non-triangle,
degenerate, and non-finite triangles, total accepted surface area, seed, and
whether normals came from interpolated source `v:normal` data or geometric face
fallbacks. Output clouds publish sampled positions as `v:point` and point
normals as the point-cloud built-in `p:normal`.

`Geometry.PointCloud.QualityMetrics` is the CPU numeric-analysis companion for
sampling papers and figure export. It accepts either `std::span<const glm::vec3>`
or a `PointCloud::Cloud` adapter and returns owned numeric arrays with explicit
status values; it performs no plotting, rasterization, file IO, GPU work, or
runtime/editor integration. The module reports nearest-neighbor distances,
nearest-neighbor histograms, mean/stddev/CV, measured minimum pairwise distance,
Poisson-disk ratio `min_pair_distance / target_radius`, and coverage as the
fraction of a reference point set whose nearest sample lies within a caller
radius. Invalid empty/one-point inputs, non-finite coordinates, non-positive
radii, invalid bin ranges, invalid domains, and out-of-domain points fail closed.

For radial distribution functions, `g(r)` is binned over a caller-provided or
inferred axis-aligned 2D/3D domain. Pair counts are accumulated as ordered
neighbors per shell and normalized by density times shell area/volume. Boundary
correction is deterministic: each point estimates the accessible shell fraction
with a fixed set of circular/spherical directions at the bin center, so figure
captions can cite a rectangular-domain shell-fraction correction rather than an
uncorrected raw pair histogram. Spectral metrics are intentionally 2D-only over
the `xy` domain. Periodograms evaluate the squared DFT magnitude on an integer
frequency grid after normalizing points into the domain; the DC bin is zeroed by
default. RAPS is the radially averaged profile of that 2D periodogram binned by
frequency radius. Inputs with non-planar `z` values are rejected as
`Requires2DInput`.

### Curvature tensor and principal directions

`Geometry.Curvature` estimates per-vertex discrete curvature on triangle meshes.
Scalar magnitudes (`v:mean_curvature` H, `v:gaussian_curvature` K,
`v:max_principal_curvature` κ₁, `v:min_principal_curvature` κ₂, and
`v:mean_curvature_normal`) follow the Meyer et al. (2003) operators and are
unchanged.

`ComputeCurvatureTensor` adds the per-vertex curvature tensor and principal
directions following Taubin (1995): each 1-ring edge contributes
`w_ij · κ_ij · T_ij T_ijᵀ` (area-derived weights summing to one, directional
curvature `κ_ij = 2 nᵢ·(x_j − x_i)/‖x_j − x_i‖²`, tangent direction `T_ij`), and the
accumulated symmetric tensor is decomposed with the shared
`Geometry::PCA::SymmetricEigen3`. The eigenvector aligned with the vertex normal
is discarded; the two tangent eigenvectors are published as the unit fields
`v:principal_dir1` (κ₁/max direction) and `v:principal_dir2` (κ₂/min direction),
with κ₁ = 3λ_a − λ_b and κ₂ = 3λ_b − λ_a recovered from the signed tensor
eigenvalues and aligned to their owning direction. `ComputeCurvature` additionally
publishes the two direction fields while leaving every scalar output bit-for-bit
intact. Flat 1-rings, boundary (open) vertices, and zero-area 1-rings fail closed
with the zero-vector sentinel and keep their scalar-derived principal curvatures;
empty / no-face meshes return `nullopt`.

### Discrete Laplacian edge-weight modes

`Geometry.DEC` assembles the stiffness (edge-weight) matrix `⋆1` and the weak
Laplacian `L = d0ᵀ ⋆1 d0` under a selectable `EdgeWeightMode`:

- `Cotan` (default) — `(cot α + cot β)/2`, the standard DEC weights.
- `HeatKernel` — `exp(−‖p_i − p_j‖²/4t)`, always positive, distance-adaptive.
- `Graph` — `w = 1`, the combinatorial Laplacian (topology only).
- `Fujiwara` — `w = 1/‖p_i − p_j‖`, inverse-distance; fails closed on
  zero-length / non-finite edges.
- `ModifiedNormal` — `(cot α + cot β)/2 · |n_i · n_j|^p` (feature exponent `p`,
  default 1), down-weighting edges across sharp dihedral features. The cotan
  term uses the clamped per-halfedge cotan so slivers cannot inject unbounded
  weights.

Every mode is symmetric with zero row sums and is validated by `AnalyzeLaplacian`.
`Geometry.HalfedgeMesh.Utils` publishes the standalone clamped per-halfedge cotan
as `MeshUtils::ClampedHalfedgeCotan` (`h:clamped_cotan`): the Heron/metric form
`cot = (a² + b² − c²)/(4·Area)` with magnitude clamped to `kHalfedgeCotanClamp`;
the per-edge cotan weight is the average of the two halfedge cotans. Boundary
halfedges and degenerate / non-finite triangles fail closed to zero.

The vertex-mass matrix `⋆0`/`Hodge0` is selectable via `MassMode`:

- `Voronoi` (default) — mixed-Voronoi area (Meyer et al., 2003); reproduces the
  previous `Hodge0` exactly.
- `Barycentric` — one third of each incident triangle area, `m_i = (1/3) Σ A_f`.
- `Sum` — lumped mass via row-sum lumping of the consistent (Galerkin) mass
  matrix; equals `Barycentric` for linear triangles.
- `Galerkin` — the full consistent mass matrix `(A/12)·[[2,1,1],[1,2,1],[1,1,2]]`
  per triangle, exposed as `DECOperators::ConsistentMass` (symmetric, SPD), with
  its row-sum lump in the diagonal `Hodge0`.

All lumped modes partition the surface area; degenerate / non-finite faces fail
closed (are skipped). `BuildConsistentMass` and the mass-mode `BuildHodgeStar0`
are deterministic.

### Halfedge mesh quantity accessors

`Geometry.HalfedgeMesh.Utils` owns the first-class mesh geometric-quantity
accessors shared by curvature, parameterization, geodesics, subdivision, and
mesh builders. Direct accessors are pure reads, while `Publish*` variants write
canonical property names:

- `FaceArea` / `PublishFaceAreas` write `f:area`; polygon faces use Newell
  area for planar loops, including concave loops, and fall back to triangle-fan
  surface area for genuinely folded non-planar loops.
- `FaceAreaVector` / `PublishFaceAreaVectors` write `f:area_vector`, the
  oriented Newell vector area.
- `FaceCentroid` / `PublishFaceCentroids` write `f:centroid`, the average of a
  face's own corner positions. This is distinct from `ComputeOneRingCentroid`,
  which averages a vertex neighborhood.
- `ComputeBarycentricVertexAreas` / `PublishBarycentricVertexAreas` write
  `v:barycentric_area`, the lumped incident face-area partition.
- `FaceScalarGradient`, `ComputeFaceScalarGradients`, and
  `PublishFaceScalarGradients` expose the unnormalized triangle-face gradient
  of a vertex scalar field and default to `f:scalar_gradient`. The heat method
  continues to normalize and negate the returned gradient locally.
- `VertexOneRingPCA` / `PublishVertexOneRingPCA` write `v:pca` with
  `Geometry::PCAResult`, using finite 1-ring neighbor positions and failing
  closed for deleted, isolated, non-finite, or underdetermined neighborhoods.

`Geometry.HalfedgeMesh.Builder::ProjectToUnitSphere` is also public: it
normalizes finite non-origin vertex positions in-place and leaves near-origin
vertices unchanged so no NaN/Inf values are introduced. `MakeMesh(Sphere)` and
subdivision-based sphere construction route through this public helper.

### Parameterization diagnostics

`Geometry.Parameterization.Diagnostics` is the shared CPU diagnostics surface for
UV parameterizations and surface-map quality checks. It evaluates a
`HalfedgeMesh::Mesh` plus per-vertex `glm::vec2` UV span without mutating mesh
storage and reports explicit status/count fields for missing UV coordinates,
non-triangle faces, non-finite positions or UVs, degenerate 3D triangles,
degenerate UV triangles, skipped faces, and flipped UV elements. The metric
record includes conformal distortion, identity-normalized conformal error,
area ratio and authalic area distortion, symmetric Dirichlet energy/excess,
stretch, deterministic boundary length distortion, and seam-discontinuity
placeholders for future chart/map records.

Existing LSCM quality fields in `Geometry.Parameterization` are now populated
from this shared evaluator, so future harmonic/Tutte, ARAP, atlas, and
map-storage work can compare against the same metric vocabulary.

### Halfedge Vertex Normal Recompute

`Geometry.HalfedgeMesh.Vertices.Normals` owns the geometry-layer CPU contract
for publishing count-matched `glm::vec3` vertex normals back to a
`HalfedgeMesh::Mesh` vertex property, defaulting to `v:normal`. The contract
returns the written `VertexProperty<glm::vec3>` plus deterministic status and
diagnostic counts. The selectable averaging modes are uniform face normals,
area-weighted face normals, angle-weighted face normals, `AreaAngleWeighted`
face normals, and Max-style sine/reciprocal-edge weighting. Degenerate faces,
invalid topology, non-finite face input, deleted slots, fallback writes, and
repaired fallback normals are reported through the result record.

### Graph Vertex Normal Recompute

`Geometry.Graph.Vertex.Normals` owns the graph-domain CPU contract for
publishing count-matched `glm::vec3` node normals back to a graph vertex
property, defaulting to `v:normal`. Graph normals are inherently ambiguous
without a surface, so the first contract uses incident-edge neighborhoods and
PCA local frames, orients normals toward the configured fallback direction, and
reports isolated vertices, degree-one vertices, collinear neighborhoods,
duplicate positions, non-finite positions, invalid edges, deleted slots, and
fallback writes. The module exposes both a `Graph::Graph` overload returning a
`VertexProperty<glm::vec3>` and a raw `Vertices`/topology-property overload
returning a `Property<glm::vec3>` for borrowed property-set callers.

### Point-Cloud Normal Recompute

`Geometry.PointCloud.Normals` replaces the former `Geometry.NormalEstimation`
module. It owns point-cloud PCA normal estimation and property-writing
recompute contracts, defaulting to canonical `v:normal` so mesh, graph, and
point-cloud domain views share the same editor/runtime publication target. The
default path builds a `Geometry.KDTree` over finite points and performs
deterministic KNN or radius neighborhood queries; overloads accept caller-owned
`Geometry.KDTree` and `Geometry.Octree` instances when an index already exists.
The span estimator is kept for geometry algorithms such as surface
reconstruction, while `PointCloud::Cloud` and raw `Vertices` overloads write or
get the output property and return the written normal handle with diagnostics
for non-finite input, too-few neighbors, collinear or duplicate neighborhoods,
fallback writes, orientation flips, and spatial-query work.

### UV atlas backend contract

`Geometry.UvAtlas` owns the backend-neutral UV atlas contract for generated
texture coordinates. Callers pass positions, triangle faces, optional authored
UVs, and optional read-only vertex properties through `UvAtlasInput`; the result
returns a `MeshSoup::IndexedMesh` with finite `v:texcoord`, source-vertex and
source-face xrefs, output chart IDs, provenance (`AuthoredPreserved` or
`Generated`), backend identity, atlas resolution, and GEOM-018 quality
diagnostics. The default backend is the repository-pinned `jpcy/xatlas` overlay
port, but callers can supply an `UvAtlasBackend` function to replace it without
importing runtime, assets, ECS, graphics, platform, or app layers.

Valid authored UVs are preserved by default when they are finite, count-matched,
and triangle-usable. Missing or invalid authored UVs fall through to the
selected backend unless the input mesh itself is invalid. Seam-split output may
duplicate vertices, and the `SourceVertexForOutputVertex` table is the
canonical way for runtime or future geometry consumers to remap normals, colors,
scalar/vector attributes, selection data, and bake sources.

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
  the graph's halfedge/edge storage (`h:connectivity`, `e:deleted`) lives on
  separate `PropertySet`s the cloud never holds, so it is unreachable through
  the cloud surface and the graph's `EdgesSize()`/`HalfedgesSize()` are
  untouched by cloud-side operations. The graph-domain `v:connectivity` slot,
  however, lives on the **shared** vertex `PropertySet` and stays physically
  reachable through generic `PointProperties()` access; the `Cloud` owns no
  connectivity accessor and never touches it, but mutating or clearing it
  through the cloud — including via `Cloud::Clear()`/`Cloud::GarbageCollection()`
  — is undefined behavior on an edge-bearing source graph, the same
  topology-mutation boundary as the other borrows. Type-level prevention of
  reaching graph-domain slots is provided by the Slice D `ConstGraphBackedCloudView`
  read-only view type described below, which exposes no mutable property
  access. The returned cloud owns its own deletion counter;
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
`Graph::Graph` is mutable because position writes go through the same type. The
compile-time-checked distinct read-only view types are provided by Slice D (see
below). A future face-free-only mutable-borrow factory remains owned by a later
GEOM-012 slice or follow-up. The source mesh must outlive the view, mirroring
`HalfedgeMesh::Mesh::CreateView`.

#### Read-only view types (`Const*View`)

GEOM-012 Slice D promotes the mutable-borrow rule from convention to type with
three read-only wrappers in `Geometry.DomainViews`:

- `Geometry::DomainViews::ConstMeshBackedGraphView` (constructed from a
  `HalfedgeMesh::Mesh&`),
- `Geometry::DomainViews::ConstMeshBackedCloudView` (constructed from a
  `HalfedgeMesh::Mesh&`),
- `Geometry::DomainViews::ConstGraphBackedCloudView` (constructed from a
  `Graph::Graph&`).

Each wraps the same shared-storage borrow produced by its mutable factory
(`BorrowMeshAsGraphReadOnly`, `BorrowMeshAsCloud`, `BorrowGraphAsCloud`) — reads
observe live source edits — but exposes **only** `const`-returning accessors.
There is no `Add*`, `Delete*`, `SetVertexPosition`, `Clear`,
`GarbageCollection`, `GetOrAdd*Property`, or mutable element-access member, so
mutating the borrowed source storage through these types is ill-formed at
compile time (asserted by the `static_assert` block in
`tests/unit/geometry/Test.SubmeshViewDomainBorrows.cpp`). Each exposes an
`AsGraph()` / `AsCloud()` accessor returning the underlying container as a
`const` reference for interop with algorithms that accept `const Graph::Graph&`
/ `const PointCloud::Cloud&`; the container's mutating methods are not reachable
through the const reference. Algorithms that allocate scratch properties on the
container (e.g. `ShortestPath::Dijkstra`, which takes a mutable
`Graph::Graph&`) are not const consumers and must use the mutable borrow.

Because `ConstGraphBackedCloudView` exposes no mutable property access and no
`Clear`/`GarbageCollection`, the shared graph-domain `v:connectivity` slot that
remains physically reachable through the mutable `BorrowGraphAsCloud` borrow
cannot be mutated or cleared through the read-only view: that documented-UB
boundary is closed by construction. The views are non-copyable and
non-movable — each is a borrow bound to its source at construction — and the
source must outlive the view. The constructors take a **mutable** source
reference: construction routes through the matching mutable `Borrow*` factory,
which lazily materializes the shared `v:point`/`p:deleted`/connectivity columns
via `EnsureProperties`/`GetOrAdd` on the source property set. A genuinely
`const`-qualified source is rejected at compile time rather than `const_cast`
into that mutating path (which would be undefined behavior); the view surface is
read-only once constructed.

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

### Hard-copy, move, and consume policy

The domain-view borrows above share storage; taking *independent ownership* of a
borrowed (or any) domain is a separate, explicit choice. GEOM-012 Slice E pins
the seams; no new conversion APIs are required because the existing surface
already covers every direction:

- **Same-domain hard copy:** the container **copy constructor**
  (`HalfedgeMesh::Mesh`, `Graph::Graph`, `PointCloud::Cloud`) is the canonical
  hard-copy seam. The copy constructor unconditionally allocates a fresh owning
  `PropertySet` and deep-copies, so copy-constructing a *borrowed* container
  (`Graph owned(borrow);` or `Graph owned = borrow;` from an lvalue) always
  produces an owning result that no longer observes source mutations and safely
  outlives source destruction. This is the supported way to "promote" a borrowed
  view into an independent container — there is no separate `BorrowX → ownedX`
  API to add. **Copy assignment** yields an independent owner *only when the
  destination already owns its storage* (e.g. a default-constructed container):
  the assignment operators copy into the destination's currently-bound reference
  members rather than reallocating, so copy-assigning *into a borrowed or
  submesh-view destination* writes through to the shared source storage
  (overwriting and resizing it) instead of producing an independent owner.
  Promote via copy construction, or assign only into a container you know owns
  its storage.
- **Cross-domain hard copy:** `Geometry.Mesh.Conversion` and
  `Geometry.PointCloud.Conversion` (the `To*`/`From*` result objects above) own
  the explicit cross-format hard copies that change topology/cardinality or the
  attribute layout, and report structured diagnostics.
- **Move / consume:** ownership transfer into a caller-owned result container
  uses move assignment (and, where declared, the move constructor). Move/consume
  is reserved for ownership transfer — never for temporary algorithm adaptation,
  for which a borrowed view is the correct tool. Because `Graph::Graph` and
  `PointCloud::Cloud` expose reference members and a user-declared copy
  constructor, move *construction* falls back to a deep copy; this is
  intentional and harmless (it never silently steals storage). Do **not**
  move-assign *from* a borrowed container: that would move the contents out of
  the shared source `PropertySet`, mutating the source. Move only owning
  containers.
- The read-only `Const*View` types are non-copyable and non-movable by
  construction (see above), so a read-only borrow can never be accidentally
  copied or consumed; promote through `AsGraph()`/`AsCloud()` plus copy
  construction of an owning container when independent ownership is needed.

## Robust predicates

`Geometry.RobustPredicates` is the narrow predicate foundation introduced by
[`GEOM-007`](../../tasks/done/GEOM-007-robust-predicates-intersection-classification.md)
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
introduced by [`GEOM-007`](../../tasks/done/GEOM-007-robust-predicates-intersection-classification.md)
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
