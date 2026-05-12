# `src/geometry` Gap Analysis (2026-05-12)

## Scope

This review covers the promoted `src/geometry` layer as it exists on 2026-05-12. It focuses on:

- style and API inconsistencies that make paper-method integration harder;
- missing reusable geometry data structures;
- missing general algorithms commonly needed by modern geometry-processing papers; and
- a prioritized implementation roadmap that preserves the repository layering contract.

This review does not implement code. It is intended to seed follow-up tasks under `tasks/backlog/` or method-specific tasks under `methods/`.

## Current capability inventory

`src/geometry` is already a broad geometry-processing layer. The current CMake target `IntrinsicGeometry` exposes 77 C++23 module interfaces, with implementation units for the heavier algorithms.

### Strong existing foundations

- **Primitive geometry and queries:** `AABB`, `OBB`, `Sphere`, `Capsule`, `Cylinder`, `Ellipsoid`, `Triangle`, `Segment`, `Ray`, `Plane`, frustum tests, containment, overlap, raycast, support mappings, GJK/EPA, contact manifolds, and SDF contacts.
- **Topology containers:** halfedge mesh, graph, point cloud, shared property arrays, circulators, submesh/subcloud view concepts, and shared graph/mesh connectivity records documented in `docs/architecture/geometry.md`.
- **Spatial acceleration:** BVH, KD-tree, octree, generic spatial query result types, KNN/radius queries in KD-tree, and octree node properties.
- **Mesh processing:** boundary extraction, analysis, quality, repair, smoothing, simplification, subdivision, Catmull-Clark, isotropic/adaptive remeshing, booleans, curvature, parameterization, DEC, scalar heat-method geodesics, and vector heat method.
- **Point cloud processing:** point cloud container, point cloud utilities, normal estimation, PCA, K-means, registration, and point cloud IO.
- **Implicit and volumetric surface tools:** grid, implicit plane field, SDF, marching cubes, and Hoppe-style implicit reconstruction.
- **IO metadata and import/export:** mesh OBJ/OFF/PLY/STL, point cloud XYZ/PCD/PLY, and graph TGF/edge-list metadata.
- **Tests:** the geometry unit suite currently lists 50 geometry-focused test sources in `tests/CMakeLists.txt` under the `unit;geometry` labels.

### Important limitations in the current foundation

- The benchmark surface for geometry is effectively empty: `benchmarks/geometry/Bench_ExampleSmoke.cpp` is a placeholder and `benchmarks/geometry/README.md` has no manifests, datasets, baselines, or method IDs.
- `Geometry.LinearSolver` is a public module interface in `src/geometry/CMakeLists.txt` and `docs/api/generated/module_inventory.md`, but it is not re-exported by `Geometry.cppm`. It is used internally by `Geometry.Sphere.cpp` only. This should be made explicitly public or explicitly internal.
- The layer has many useful specialized algorithms, but lacks several general abstractions needed to implement papers consistently: robust predicates, reusable sparse algebra, optimization scaffolding, exact/intersection kernels, datasets/bench manifests, and standard mesh/point-cloud conversion contracts.

## Style and API inconsistencies

The issues below are not correctness bugs by themselves. They are consistency gaps that will compound as more papers are integrated.

### 1. Module names, file names, and namespaces are not consistently aligned

Examples:

- `Geometry.HalfedgeMesh.DEC.cppm` exports `module Geometry.DEC` and namespace `Geometry::DEC`.
- `Geometry.HalfedgeMesh.Curvature.cppm` exports `module Geometry.Curvature` and namespace `Geometry::Curvature`.
- `Geometry.HalfedgeMesh.Boolean.cppm` exports `module Geometry.Boolean` and namespace `Geometry::Boolean`.
- `Geometry.Graph.IO.cppm` exports namespace `Geometry::GraphIO`, while graph algorithms use `Geometry::Graph` and shortest path uses `Geometry::ShortestPath`.
- `Geometry.HalfedgeMesh.IO.cppm` exports namespace `Geometry::MeshIO`; `Geometry.PointCloud.IO.cppm` exports namespace `Geometry::PointCloudIO`; neither follows a nested `Geometry::HalfedgeMesh::IO` / `Geometry::PointCloud::IO` pattern.
- `Geometry.Sphere.Sampling.cppm` exports namespace `Geometry::Sampling`, which is not sphere-scoped.

Why this matters: paper implementations often compose many modules. Mismatched file/module/namespace names make API discovery difficult and increase the chance of importing the broad `Geometry` umbrella rather than narrow modules.

Recommended direction:

- Define a naming policy for new geometry modules before adding more paper methods.
- Avoid mechanical renames mixed with semantic changes.
- If renaming existing modules, do it as dedicated compatibility-shim tasks with generated inventory updates.

### 2. Public/private state exposure differs across equivalent containers

Examples:

- `Geometry::BVH` and `Geometry::KDTree` keep node/element arrays private and expose const accessors.
- `Geometry::Octree` exposes `m_Nodes`, `NodeProperties`, and `ElementAabbs` publicly, while also exposing some getter-style methods.
- `HalfedgeMesh::Mesh`, `PointCloud::Cloud`, and `Graph::Graph` all use property sets, but their public accessor names and mutability contracts differ.

Why this matters: methods that need acceleration structures, persistent cache invalidation, or benchmark instrumentation cannot rely on one uniform inspection/mutation contract.

Recommended direction:

- Introduce a geometry-container API style guide: public state only for plain data records; containers expose const spans/accessors and explicit mutation operations.
- Normalize octree public state behind accessors or document it as intentional low-level storage.

### 3. Failure reporting is inconsistent

Examples:

- Many algorithms return `std::optional<Result>`.
- `Octree::Build` returns `bool`.
- IO loaders return `Core::Expected<T>`.
- IO writers return status enums.
- Many mutating mesh operations return `bool`, a handle, or `std::optional<Handle>` depending on the operation.
- Some invalid states are enforced by `assert`, for example in smoothing and property access paths.

Why this matters: method code needs to distinguish invalid input, non-convergence, topological precondition failure, numerical singularity, and IO failure. `std::optional` and `bool` discard too much diagnostic information for reproducible paper-method workflows.

Recommended direction:

- Define a `Geometry::Status` / `Geometry::ErrorCode` / `Core::Expected<T>` convention for new algorithms.
- Keep `std::optional` only for trivial lookup-style APIs or legacy compatibility.
- Add structured diagnostics to solvers and algorithms that can fail due to topology or numerical conditioning.

### 4. Naming style varies within the same layer

Examples:

- `PropertySet::Shrink_to_fit()` uses snake/camel mixture while nearby APIs use `ShrinkToFit`, `FreeMemory`, `Reserve`, and `Clear`.
- Some accessors are `GetMaxBvhDepth`, `GetElementIndices`; others are `Nodes()`, `ElementAabbs()`, `VerticesSize()`, `VertexCount()`.
- Count/size semantics are split between logical live count and storage size, but not uniformly named across mesh, graph, point cloud, and acceleration structures.
- Several comments have stale wording, such as `} // namespace engine::geometry` in `Geometry.Properties.cppm`.

Recommended direction:

- Use `Size()` for storage slots, `Count()` for live non-deleted elements, and `Capacity()` only for reserved storage where needed.
- Prefer noun accessors without `Get` for cheap accessors unless an existing subsystem style requires `Get`.
- Keep stale comments and naming cleanups mechanical and isolated.

### 5. Numeric precision and tolerance policy is implicit

Examples:

- Primitives and containers are mostly `glm::vec3` / `float` oriented.
- DEC, geodesics, vector heat, remeshing parameters, and many solvers use `double` internally.
- Some algorithms expose tolerances; others hard-code or infer them.
- Robust predicates and exact arithmetic are absent.

Why this matters: modern geometry papers often fail on degeneracies, scale variation, nearly coincident intersections, thin triangles, and ill-conditioned linear systems. Without a common numeric policy, individual methods will grow ad-hoc tolerances.

Recommended direction:

- Add a documented geometry numeric policy covering coordinate precision, tolerances, units/scale normalization, degeneracy handling, and reproducibility.
- Introduce robust predicate utilities before expanding booleans, remeshing, arrangement, or reconstruction work.

### 6. Scratch memory and allocation policy are not standardized

Examples:

- Many queries allocate local `std::vector` stacks or result buffers.
- Some APIs accept output vectors; others allocate result vectors internally.
- There is no shared geometry scratch/context type for repeat solves, nearest-neighbor queries, or iterative algorithms.

Why this matters: paper implementations often run expensive algorithms repeatedly in parameter sweeps or benchmarks. Uncontrolled allocations make performance and reproducibility harder to reason about.

Recommended direction:

- Introduce optional scratch/context objects for hot algorithms.
- Prefer caller-provided spans/vectors for inner-loop outputs, with allocation-owning convenience overloads only at the API edge.

### 7. Exported internal namespaces leak implementation details

Examples:

- `Geometry.GJK.cppm` and `Geometry.EPA.cppm` export `Geometry::Internal` data used by contact/collision internals.

Recommended direction:

- Keep reusable low-level types public only if they have stable semantics and tests.
- Otherwise move implementation helpers into implementation partitions or non-exported namespaces.

## Missing reusable data structures

The current layer has mesh/graph/point-cloud containers, but modern papers repeatedly need the following reusable structures.

### P0 — foundational gaps blocking many methods

1. **Indexed triangle/polygon soup**
   - Lightweight `positions + indices + attributes` container for import, reconstruction, GPU upload staging, and algorithms that do not require halfedge connectivity.
   - Conversion contracts: soup ↔ halfedge mesh, soup ↔ point cloud, soup ↔ graphics upload descriptors.
   - Validation reports for duplicate vertices, non-manifold edges, winding, degenerate faces, and attribute arity.

2. **Symmetric mesh/graph/point-cloud domain views**
   - Mesh, graph, and point cloud should be treated as peer geometry domains, not as a mesh-first hierarchy.
   - Algorithms should request the least structured domain they need: point samples, graph traversal, or mesh faces/topological editing.
   - Richer domains passed to less-structured algorithms should use explicit borrowed views when semantic properties and lifetime are compatible; otherwise they should use explicit hard-copy conversions with diagnostics.
   - Mesh-backed graph views can already share `v:point`, `v:connectivity`, `h:connectivity`, and edge property sets. Mesh/graph-backed point-cloud views should also reuse canonical `v:point` storage and must not allocate a separate legacy `p:position` property when borrowing source storage.

3. **Robust predicate and exact-kernel utilities**
   - Orientation, incircle/insphere, segment/triangle intersection, barycentric classification, projection predicates, and epsilon/scale-aware comparisons.
   - Needed for booleans, remeshing, tetrahedralization, arrangements, collision, and reconstruction.

4. **Reusable sparse linear algebra layer**
   - Current DEC CSR is useful but narrow. Missing general CSR/COO builders, transpose/multiply composition, block matrices, sparse direct or factorization seams, iterative solver interfaces, residual diagnostics, and preconditioner abstractions.
   - Needed for parameterization, geodesics, deformation, PDE methods, spectral methods, registration, and FEM.

5. **Dense small/medium matrix utilities**
   - Current code contains local 3x3 eigensolvers and a tiny fixed-size Gaussian solver.
   - Missing reusable SVD, QR, polar decomposition, symmetric eigensolver, covariance accumulation, least-squares helpers, and robust PCA diagnostics.

6. **Optimization framework**
   - Common energy/minimization scaffolding: objective callbacks, gradients, finite-difference checks, line search, Gauss-Newton, L-BFGS, Levenberg-Marquardt, projected constraints, convergence reports.
   - Needed for SLIM/ARAP, registration, deformation, fitting, reconstruction, and inverse problems.

7. **Geometry diagnostics and provenance records**
   - Standard result diagnostics for preconditions, degeneracy counts, residuals, iteration histories, topology changes, and timing counters.
   - Needed to compare method variants rigorously and satisfy method-workflow documentation.

### P1 — broad enablement gaps

1. **Feature/constraint sets**
   - Stable representation for constrained vertices/edges/faces, sharp features, boundaries, landmarks, correspondences, pinned UV vertices, and region masks.

2. **Attribute schema and interpolation policies**
   - A common way to describe attributes, domains, interpolation rules, merge/split/collapse policies, units, and defaults across mesh, graph, point cloud, and soup containers.

3. **Dynamic and approximate spatial indexes**
   - Current BVH/KD-tree/octree are useful, but missing dynamic updates, spatial hashing, approximate nearest neighbor seams, and batched query APIs.

4. **Multiresolution and hierarchy structures**
   - Mesh pyramids, progressive meshes, simplification histories, prolongation/restriction operators, and wavelet/multigrid-friendly hierarchy records.

5. **Field data structures**
   - Scalar/vector/tensor fields over vertices, edges, faces, cells, and samples with interpolation, differential operators, and boundary-condition records.

6. **Volumetric and cell-complex containers**
   - Tetrahedral meshes, hexahedral grids, general cell complexes, sparse voxel grids, narrow-band level sets, and dual/primal complexes.

### P2 — specialized but recurring in modern papers

1. **Intrinsic triangulation / common subdivision structures** for robust intrinsic Delaunay, geodesics, and surface maps.
2. **Arrangement/BSP data structures** for robust booleans, fracture, remeshing, and intersection-heavy papers.
3. **Curve and spline geometry** for boundary curves, paths, skeletons, NURBS/Bézier fitting, and sweep surfaces.
4. **Sampling sets and stochastic reproducibility state** for blue-noise sampling, farthest-point sampling, Monte Carlo estimators, and benchmark repeatability.
5. **Correspondence graphs and map representations** for functional maps, point-to-plane constraints, landmark graphs, and cross-surface transfer.

## Missing general algorithms for modern geometry papers

### Numerical and solver algorithms

- Sparse direct solve seam, Cholesky/LDLT where available, incomplete Cholesky/Jacobi/SSOR preconditioners, MINRES/GMRES/BiCGSTAB, LSQR/CGNR, and robust least-squares wrappers.
- Eigen/spectral algorithms: Lanczos, LOBPCG, shift-invert seam, generalized eigenproblems `Lx = λMx`, and spectral basis caching.
- Generic nonlinear optimization: line search/trust region, Gauss-Newton/LM, L-BFGS, projected constraints, box/simplex constraints, and convergence diagnostics.
- Automatic or finite-difference derivative checks for method validation.

### Linear algebra library recommendation

The best fit is a **hybrid GLM + Eigen3 policy**:

- Keep **GLM** as the public geometric storage type for positions, directions, transforms, renderer-facing data, and existing container attributes such as `glm::vec3` positions. This avoids a broad public API migration and preserves current graphics/runtime interoperability.
- Add **Eigen3** as the CPU linear-algebra backend for `src/geometry` numerical kernels: dense decompositions, sparse matrices, sparse factorizations, iterative solvers, least-squares systems, and optimization internals.
- Hide Eigen behind geometry-owned adapters/modules at first. Public algorithm APIs should continue to accept spans of GLM/scalar data and return geometry result/diagnostic records; Eigen types should not leak through the broad `Geometry` umbrella unless a deliberately named advanced numerical module is introduced.
- Convert or map at algorithm boundaries. Small fixed-size values can copy between `glm::vec*` and `Eigen::Vector*`; large contiguous scalar buffers can be exposed through `Eigen::Map` where alignment/stride is explicit. Avoid assuming arbitrary GLM aggregate layouts are valid Eigen matrices without an adapter contract.
- Do not implement a full linear algebra stack in-house. Implement only geometry-specific wrappers, diagnostics, matrix assembly helpers, conversion utilities, and solver policy objects. Eigen should own the mature numerical kernels.
- Treat **Spectra** as a later optional add-on for large sparse eigenproblems once spectral methods need it. It complements Eigen but should not be the first dependency.
- Treat **SuiteSparse/CHOLMOD, BLAS/LAPACK, MKL, oneMKL, or CUDA solvers** as optional optimized backends behind capability flags, not default geometry dependencies. They add platform, license, packaging, or backend complexity that is not appropriate before CPU-reference parity and benchmark manifests exist.
- Avoid broad geometry frameworks such as libigl/CGAL as core dependencies for linear algebra. They can inform algorithms or serve as external comparison references, but depending on them directly would blur layer ownership and make method provenance harder to review.

This policy gives the engine mature numerical capability without replacing GLM as the engine-facing geometry math/storage vocabulary.

### Mesh topology, repair, and robust operations

- Robust triangle-triangle and segment-triangle intersection with classification.
- Exact or filtered predicates for orientation/incircle and robust barycentric tests.
- Robust mesh boolean kernel with arrangement provenance and attribute transfer.
- Non-manifold topology representation or explicit rejection/repair adapters.
- Hole filling, self-intersection removal, duplicate/near-duplicate welding, connected components, genus/Euler diagnostics, and orientation repair as reusable APIs.

### Remeshing, simplification, and multiresolution

- Feature-preserving anisotropic remeshing.
- Intrinsic Delaunay triangulation and edge flips by intrinsic criteria.
- Progressive mesh records, simplification hierarchies, mesh decimation with reversible operation logs.
- Quad/hex-dominant remeshing and field-guided remeshing.
- Multigrid/prolongation/restriction operators for PDE methods.

### Parameterization and mapping

- Tutte/harmonic embedding, ABF/ABF++, ARAP parameterization, SLIM, authalic/area-preserving maps, orbifold/symmetric maps, atlas segmentation, seam creation, and chart packing.
- Distortion metrics as standalone diagnostics: conformal, authalic, symmetric Dirichlet, stretch, flipped elements, boundary distortion.
- Surface-to-surface maps, barycentric map storage, functional maps, and map-quality metrics.

### Geometry processing PDEs and fields

- Poisson solve wrappers for scalar/vector fields with boundary conditions.
- Harmonic/biharmonic fields, Hodge decomposition, vector-field design, cross fields, frame fields, and singularity indexing.
- Curvature-flow, mean-curvature-flow, bilateral/anisotropic diffusion, and feature-aware smoothing variants.
- Boundary-condition data structures: Dirichlet, Neumann, mixed, pinned, and soft constraints.

### Geodesics and intrinsic geometry

- Exact or high-accuracy geodesics such as MMP/Chen-Han, fast marching, fast sweeping, and intrinsic triangulation-backed heat methods.
- Geodesic path extraction, not only distance fields/log maps.
- Geodesic Voronoi, farthest-point sampling, and intrinsic Delaunay utilities.

### Point cloud algorithms

- Voxel grid/downsampling, statistical/radius outlier removal, MLS/RIMLS smoothing, bilateral normal filtering, ISS/Harris keypoints, FPFH/SHOT descriptors, RANSAC primitive fitting, and robust normal orientation variants.
- Registration beyond basic ICP: point-to-plane ICP, generalized ICP, colored ICP, CPD, TEASER-style robust global registration seam, feature-based coarse alignment, and multiway registration graphs.
- Surface reconstruction alternatives: screened Poisson, ball pivoting, alpha shapes, Delaunay-based reconstruction, RIMLS implicit surfaces.

### Volumetric, simulation-adjacent, and FEM geometry

- Tetrahedralization/import, tetrahedral mesh quality metrics, boundary extraction, volumetric remeshing, signed-distance narrow-band operations, CSG over implicit fields, and marching tetrahedra/dual contouring.
- FEM helper structures: element gradients, mass/stiffness assembly seams, boundary conditions, and material-region attributes.

### Sampling, descriptors, segmentation, and learning-adjacent utilities

- Uniform surface/volume sampling, blue-noise sampling, Poisson disk sampling, stratified sampling, and reproducible random streams.
- Shape descriptors: HKS, WKS, spin images, curvature histograms, spectral signatures, geodesic descriptors.
- Segmentation/clustering: region growing, spectral clustering, graph cuts seam, watershed on meshes/point clouds, skeletonization, medial-axis approximations.

## Paper-method integration gaps

These gaps are not geometry algorithms themselves, but they are necessary to implement many papers rigorously.

1. **Benchmark manifests are missing.** Geometry benchmarks need stable IDs, datasets, smoke/heavy split, metrics, and machine-readable outputs.
2. **Reference datasets and fixtures are sparse.** Tests use small fixtures, but method comparison needs canonical meshes, point clouds, degenerate cases, and scale-varied inputs.
3. **Method result diagnostics are not standardized.** Many current algorithms return only success/convergence and a few counters.
4. **No standard parity protocol exists inside `src/geometry`.** Method workflow requires CPU reference first, correctness tests, benchmarks, optimized CPU, then GPU. Geometry should expose seams that make this easy.
5. **No reproducibility contract for randomized algorithms.** K-means and future sampling/registration methods need seed/state control and deterministic test fixtures.

## Recommended roadmap

### Phase 0 — policy and consistency cleanup

- Define a `src/geometry` API style guide covering module/namespace naming, error handling, mutability, count/size naming, numeric tolerances, and diagnostics.
- Decide whether `Geometry.LinearSolver` is public. Re-export it from `Geometry.cppm` if public; otherwise make it explicitly internal.
- Add a geometry benchmark manifest format and replace `Bench_ExampleSmoke.cpp` with a real smoke benchmark.
- Add a generated or scripted inventory check for module/file/export namespace drift.

### Phase 1 — core reusable infrastructure

- Add indexed mesh/soup and conversion contracts.
- Add robust predicates and intersection classification utilities.
- Generalize DEC CSR into reusable sparse algebra with builders, diagnostics, and solver interfaces.
- Add dense numerical utilities for SVD/eigen/least-squares/polar decomposition.
- Add standard geometry diagnostics/result records.

### Phase 2 — method-enabling algorithm packs

- Parameterization pack: harmonic/Tutte, ARAP, SLIM, atlas metrics.
- Point-cloud pack: filtering/downsampling, descriptors, robust/global registration, Poisson reconstruction.
- Intrinsic geometry pack: intrinsic Delaunay, fast marching, path extraction, geodesic sampling.
- PDE/field pack: boundary conditions, Poisson wrappers, harmonic/biharmonic fields, vector-field design.
- Remeshing pack: feature-preserving anisotropic remeshing, progressive mesh hierarchy, multiresolution operators.

### Phase 3 — advanced structures and optimized backends

- Tetrahedral/volumetric containers and FEM assembly seams.
- Sparse voxel/narrow-band SDF structures and dual contouring.
- Arrangement/boolean robustness overhaul.
- Optional optimized CPU/GPU backends only after CPU reference parity, tests, and benchmark manifests exist.

## Suggested follow-up tasks

- `GEOM-005`: Geometry API style guide and naming/error/numeric policy.
- `GEOM-006`: Indexed mesh/soup container and conversion contracts.
- `GEOM-007`: Robust predicates and intersection classification foundation.
- `GEOM-008`: General sparse/dense solver infrastructure for geometry methods.
- `GEOM-009`: Geometry benchmark manifests, fixtures, and smoke benchmark.
- `GEOM-010`: Point-cloud filtering/descriptors/registration roadmap task pack.
- `GEOM-011`: Parameterization and mapping roadmap task pack.
- `GEOM-012`: Symmetric mesh/graph/point-cloud domain views and property sharing.

## Validation notes

- This is a documentation-only review; no C++ behavior changed.
- Source inspection covered `src/geometry/CMakeLists.txt`, `Geometry.cppm`, representative mesh/graph/point-cloud/spatial/DEC/geodesic/reconstruction/IO modules, `tests/CMakeLists.txt`, `benchmarks/geometry`, and existing geometry architecture docs.
- Documentation link validation should be run after adding this file.



