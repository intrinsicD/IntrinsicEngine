# GEOM-015 — Common method-package infrastructure

## Goal
- Add the shared engine-level types and modules that every paper-method package under `methods/geometry/` needs, so each method tasks reuses one canonical implementation instead of redefining diagnostics, RNGs, oracles, parallel transport, boundary conditions, provenance records, and small-matrix QEF solves.

## Non-goals
- No paper-method implementations in this task; this is infrastructure only.
- No public API for advanced numerical methods (eigensolvers, optimisation) beyond what already lands in [`GEOM-008`](GEOM-008-linear-algebra-solver-infrastructure.md).
- No GPU port of any of the new modules.
- No retroactive rewrite of existing geometry algorithms beyond the minimum needed to consume the new shared types.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only). A small subset (`Diagnostics`, `Random`) may live in `core` if the type has no geometry-specific content; otherwise it lives in `geometry`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) and the consolidated needs of
  [`METHOD-002`](../methods/METHOD-002-signed-heat-method-reference-backend.md),
  [`METHOD-003`](../methods/METHOD-003-closest-point-method-pde-reference-backend.md),
  [`METHOD-004`](../methods/METHOD-004-walk-on-spheres-reference-backend.md),
  [`METHOD-005`](../methods/METHOD-005-robust-mesh-boolean-reference-backend.md),
  [`METHOD-006`](../methods/METHOD-006-cross-field-design-reference-backend.md),
  [`METHOD-007`](../methods/METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md),
  [`GEOM-013`](GEOM-013-feature-preserving-dual-contouring.md), and
  [`GEOM-014`](GEOM-014-feature-aware-quadric-error-simplification.md).
- Audited gaps in `src/geometry`: no shared `Diagnostics` record, no central RNG (KMeans, Sphere.Sampling, PointCloud.Utils each re-roll their own), no closest-point oracle abstraction, parallel-transport logic is locked inside `Geometry.HalfedgeMesh.VectorHeatMethod`, no `BoundaryConditions` records, no `Provenance` records.

## Required changes

### 1. Diagnostics record (`Geometry::Diagnostics`)
- [ ] Add module `Geometry.Diagnostics` in `src/geometry/Geometry.Diagnostics.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry {
    struct Diagnostics {
      uint32_t iterations = 0;
      double final_residual = std::numeric_limits<double>::quiet_NaN();
      uint32_t degenerate_input_count = 0;
      uint32_t numerical_uncertain_count = 0;
      double wall_time_ms = 0.0;
      std::vector<std::pair<std::string,double>> counters;  // free-form named counters
      std::vector<std::pair<std::string,std::string>> notes;
    };
  }
  ```
- [ ] Re-export from `Geometry.cppm` umbrella (it is a public type that crosses every algorithm).
- [ ] Provide a small free function `MergeDiagnostics(const Diagnostics&, const Diagnostics&)` for composed pipelines.

### 2. Deterministic RNG (`Geometry::Random`)
- [ ] Add module `Geometry.Random` in `src/geometry/Geometry.Random.cppm` + `.cpp`.
- [ ] Implement two PRNGs:
  - `Geometry::Random::SplitMix64` for seed-stream derivation.
  - `Geometry::Random::PCG32` for per-stream sampling.
- [ ] Define a **seed-stream contract**: every algorithm that consumes randomness must derive its per-element streams as `SplitMix64(seed, element_index, walk_index, step_index)`. This guarantees reproducibility under thread re-ordering and varying thread counts.
- [ ] Provide canonical samplers: uniform `[0,1)`, uniform on `S^{n-1}`, Gaussian via Box-Muller, exponential, uniform inside a ball.
- [ ] Migrate `Geometry.KMeans` (`InitializeRandom`, `params.Seed`), `Geometry.Sphere.Sampling::SampleSurfaceRandom` / `SampleVolumeRandom`, and `Geometry.PointCloud.Utils::RandomSubsample` to `Geometry::Random` in the same task. (These are the only existing internal RNG users; auditing them now prevents drift.)

### 3. Closest-point oracle trait (`Geometry::ClosestPointOracle`)
- [ ] Add module `Geometry.ClosestPointOracle` in `src/geometry/Geometry.ClosestPointOracle.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry {
    struct ClosestPointResult { glm::dvec3 point; glm::dvec3 normal; double distance; uint32_t source_id; };
    struct ClosestPointOracle {
      virtual ~ClosestPointOracle() = default;
      virtual ClosestPointResult Query(glm::dvec3 x) const = 0;
      virtual double SignedDistance(glm::dvec3 x) const = 0;   // optional; returns NaN if unsupported
      virtual AABB Bounds() const = 0;
    };
  }
  ```
- [ ] Provide three concrete adapters in the same module (header-only):
  - `OracleFromHalfedgeMesh` — wraps `Geometry::BVH` over triangles + face normals.
  - `OracleFromPointCloud` — wraps `Geometry::KDTree` + per-point normals from `Geometry::NormalEstimation`.
  - `OracleFromSDF` — wraps `Geometry::SDF`; normal = normalised gradient.
- [ ] Document the source-id contract: implementations may use `source_id` to tag the source primitive (triangle id, point id, voxel id) for downstream provenance / attribute transfer.

### 4. Halfedge-mesh discrete connection (`Geometry::HalfedgeMesh::Connection`)
- [ ] Extract the parallel-transport / per-edge rotation machinery currently buried inside `Geometry.HalfedgeMesh.VectorHeatMethod` into a new module `Geometry.HalfedgeMesh.Connection` in `src/geometry/Geometry.HalfedgeMesh.Connection.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::HalfedgeMesh::Connection {
    struct PerFaceFrame { glm::dvec3 e1, e2, normal; };
    std::vector<PerFaceFrame> BuildFaceFrames(const Mesh&);
    // Per dual edge: rotation in the plane of the shared edge that maps frame_left -> frame_right.
    std::vector<double> EdgeTransportAngles(const Mesh&, std::span<const PerFaceFrame>);
    // Holonomy / vertex angle defects.
    std::vector<double> VertexAngleDefects(const Mesh&);
  }
  ```
- [ ] Refactor `Geometry::VectorHeatMethod` internals to consume the extracted module; preserve its public API and tests bit-for-bit.

### 5. Boundary conditions records (`Geometry::PDE::BoundaryConditions`)
- [ ] Add module `Geometry.PDE.BoundaryConditions` in `src/geometry/Geometry.PDE.BoundaryConditions.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::PDE {
    enum class BCKind { Dirichlet, Neumann, Robin, Pinned };
    struct ScalarBC {
      BCKind kind;
      // Element ids carrying the BC (interpretation depends on consumer: vertex/edge/face).
      std::vector<uint32_t> elements;
      std::vector<double> values;       // length == elements.size() or 1 (constant)
      double robin_alpha = 0.0;
    };
    struct VectorBC { BCKind kind; std::vector<uint32_t> elements; std::vector<glm::dvec3> values; };
  }
  ```
- [ ] Provide free helpers `MakeConstantDirichlet(std::span<uint32_t>, double)`, `MakeZeroNeumann(std::span<uint32_t>)`.

### 6. Provenance records (`Geometry::Provenance`)
- [ ] Add module `Geometry.Provenance` in `src/geometry/Geometry.Provenance.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry {
    struct PrimitiveSource { uint32_t source_mesh_id; uint32_t primitive_id; };
    struct OutputProvenance {
      // Per-output element (vertex / face / tet), the set of source primitives that contributed.
      std::vector<std::vector<PrimitiveSource>> per_element_sources;
      // Per-output vertex, barycentric weights against its source triangle (if applicable).
      std::vector<std::array<double,3>> per_vertex_barycentrics;
    };
  }
  ```
- [ ] Provide `AttributeTransfer` helpers in `Geometry.Provenance.cpp` that consume `OutputProvenance` + the source meshes' `Geometry::Properties` sets and produce interpolated output properties (linear interpolation for scalars / vectors, copy-from-most-confident for discrete attributes).

### 7. QEF small-matrix solver (`Geometry::QEFSolver`)
- [ ] Add module `Geometry.QEFSolver` in `src/geometry/Geometry.QEFSolver.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry {
    struct QEF3 {                  // x^T A x - 2 b^T x + c
      glm::dmat3 A = glm::dmat3(0.0);
      glm::dvec3 b = glm::dvec3(0.0);
      double c = 0.0;
      void AddPlane(glm::dvec3 point, glm::dvec3 normal);
      void AddPoint(glm::dvec3 point, double weight = 1.0);
      void Add(const QEF3& other);
    };
    // Solve via SVD with singular-value clamping; returns (point, residual, rank).
    struct QEFSolution { glm::dvec3 point; double residual; uint32_t effective_rank; };
    QEFSolution SolveQEF(const QEF3&, double singular_value_threshold,
                         glm::dvec3 fallback_point /* e.g. mass-weighted mean */);
  }
  ```
- [ ] This module backs both `GEOM-013` (per-cell vertex placement) and `GEOM-014` (collapse-position vertex placement).

### CMake and module-inventory housekeeping
- [ ] Register all new modules in `src/geometry/CMakeLists.txt`.
- [ ] Re-export `Geometry.Diagnostics`, `Geometry.Random`, `Geometry.ClosestPointOracle`, `Geometry.Provenance`, `Geometry.QEFSolver` from `Geometry.cppm` (these are public).
- [ ] Do **not** umbrella-export `Geometry.HalfedgeMesh.Connection` and `Geometry.PDE.BoundaryConditions` initially — they live behind `Geometry::HalfedgeMesh` / `Geometry::PDE` namespaces and are consumed by specific algorithm modules.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via the documented tool.

## Tests
- [ ] `tests/unit/geometry/Test.Diagnostics.cpp` — merge semantics, counter accumulation.
- [ ] `tests/unit/geometry/Test.Random.cpp` — bitwise reproducibility under thread-count variation, statistical sanity (mean / variance for uniform, Gaussian, sphere samples), seed-stream derivation determinism.
- [ ] `tests/unit/geometry/Test.ClosestPointOracle.cpp` — parity test: same surface (cube) fed through mesh / point-cloud / SDF adapters returns agreeing closest-point queries within `O(h)` for uniform sampling.
- [ ] `tests/unit/geometry/Test.HalfedgeMeshConnection.cpp` — vertex angle defects on cube / sphere / torus match analytic Euler-characteristic identities; transport around contractible loops returns to identity within tolerance.
- [ ] `tests/unit/geometry/Test.PDEBoundaryConditions.cpp` — record construction sanity, helper functions.
- [ ] `tests/unit/geometry/Test.Provenance.cpp` — attribute-transfer round-trip on a known source mesh + identity provenance reproduces the source attributes.
- [ ] `tests/unit/geometry/Test.QEFSolver.cpp` — analytic plane-intersection (3 perpendicular planes → exact corner), under-determined case (1 plane → fallback to projection), rank-deficient SVD path.
- [ ] Existing tests for `VectorHeatMethod`, `KMeans`, `Sphere.Sampling`, `PointCloud.Utils.RandomSubsample` must continue to pass after the migration to `Geometry.Random` and `Geometry.HalfedgeMesh.Connection`.

## Docs
- [ ] Update `docs/architecture/geometry.md` with a "Shared method infrastructure" section listing each new module and what it provides.
- [ ] Update [`GEOM-005` in `tasks/done`](../../done/GEOM-005-api-style-and-numeric-policy.md)-derived API style policy doc (or equivalent) to mandate that new method packages consume these shared types.
- [ ] Document the deterministic RNG seed-stream contract in `docs/methods/numerical-robustness-policy.md` (create if absent).
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] All seven new modules compile and pass their own unit tests.
- [ ] `Geometry::VectorHeatMethod` public API and tests are unchanged after refactor onto `Geometry::HalfedgeMesh::Connection`.
- [ ] `KMeans`, `Sphere.Sampling`, `PointCloud.Utils.RandomSubsample` consume `Geometry::Random` and remain deterministic.
- [ ] METHOD-002 through METHOD-007, GEOM-013, GEOM-014 explicitly cite this task as their dependency for shared infrastructure.
- [ ] `Geometry.cppm` umbrella re-exports the public subset; layering check passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Diagnostics|Random|ClosestPointOracle|Connection|BoundaryConditions|Provenance|QEFSolver|VectorHeatMethod|KMeans|SphereSampling|PointCloud' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- No new dependencies on `runtime`, `graphics`, `ecs`, `assets`, `platform`, or `app`.
- No GPU port in this task.
- No public Eigen types in any new module surface.
- No `std::rand` / `std::default_random_engine` / `srand` anywhere; the engine has one RNG policy.
- No silent change of existing public APIs — refactors that touch `VectorHeatMethod` / `KMeans` / `Sphere.Sampling` / `PointCloud.Utils` must preserve byte-for-byte test outputs.
