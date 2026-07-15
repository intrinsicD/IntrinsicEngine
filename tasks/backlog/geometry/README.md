# Geometry Backlog

Geometry IO parity, method-readiness seeds, and algorithm hardening.
`geometry -> core` only; `src/geometry/*` must not import
`assets`/`runtime`/`graphics`/`rhi`.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [GEOM-013 — Feature-preserving dual contouring](GEOM-013-feature-preserving-dual-contouring.md).
- [GEOM-024 — Sparse symmetric generalized eigensolver seam](GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md)
  (gates `methods/METHOD-006` variant B; depends on `GEOM-020`; promote when
  METHOD-006 is the next-priority method).
- [GEOM-058 — Gaussian mixture models and Anderson-accelerated EM seam](GEOM-058-gaussian-mixture-em-anderson-acceleration.md)
  (gates `methods/METHOD-015`; framework24 port-gap follow-up).
- [GEOM-059 — Kernel matrices, Nyström approximation, and Gaussian-process interpolation seam](GEOM-059-kernel-matrices-nystroem-gaussian-process.md)
  (framework24 port-gap follow-up).
- [GEOM-060 — Permutohedral lattice fast high-dimensional filtering seam](GEOM-060-permutohedral-lattice-highdim-filtering.md)
  (named future fast path for the METHOD-015 nonrigid optimized backend and
  bilateral-filter consumers; framework24 port-gap follow-up).
- [GEOM-061 — Point-cloud grid-downsampling reduction strategies](GEOM-061-grid-downsampling-reduction-strategies.md)
  (index-returning per-cell reduction extending retired GEOM-016's voxel
  downsampling; framework24 port-gap follow-up).
- [GEOM-062 — Point-set projection and weighting kernels seam](GEOM-062-point-set-projection-weighting-kernels.md)
  (reusable `Geometry.PointCloud.Kernels`: radial weights, LOP repulsion, WLOP
  density weights; gates the `methods/METHOD-016..018` LOP consolidation family).
- [GEOM-064 — Parameterization optimization kernels seam](GEOM-064-parameterization-optimization-kernels.md)
  (reusable `Geometry.Parameterization.Optimize`: local rotation fit,
  symmetric-Dirichlet energy/proxy, injective line search; gates ARAP
  `methods/METHOD-021` and SLIM `methods/METHOD-022`).
- [RORG-031E — Geometry and method-readiness backlog seed](RORG-031-geometry-method-readiness.md).

### bcg_code_base geometry-processing port gaps (seeded 2026-06-26)

Confirmed feature gaps from the `bcg_code_base` → IntrinsicEngine port-gap
review (features already present or better in IntrinsicEngine were excluded).
Grouped by cluster; each is `geometry -> core` only and targets `CPUContracted`.

All seeded primitive, statistics, and linear-algebra gaps in this cluster are
retired; see the retired entries below.

A second port-gap sweep (2026-07-07, direct comparison against the
`framework24` checkout of `bcg_framework`) seeded the remaining
research-numerics gaps that the 2026-06-26 review did not cover: `GEOM-058`
(Gaussian mixtures + Anderson-accelerated EM), `GEOM-059` (kernel
matrices/Nyström/Gaussian-process interpolation), `GEOM-060` (permutohedral
lattice filtering), and `GEOM-061` (grid-downsampling reduction strategies),
plus the method packages `methods/METHOD-015` (Coherent Point Drift family)
and `methods/METHOD-016` (LOP/WLOP consolidation) and the editor task
`ui/UI-034` (framework24 interaction/layout conventions). Each task file names
the exact `lib_bcg_framework`/`lib_bcg_viewer` source headers it ports and the
robustness/determinism contracts the port must add over the originals.

`GEOM-062` (`Geometry.PointCloud.Kernels`) is the follow-on shared-weighting
seam factored out of the LOP consolidation family so `methods/METHOD-016`
(WLOP/LOP), `methods/METHOD-017` (CLOP), and `methods/METHOD-018`
(EAR/anisotropic) reuse one tested radial-weight/repulsion/density-weight core
instead of each re-deriving the bcg density and repulsion math privately.

The indexed decrease-key heap that backs Dijkstra is a `core` container filed
under the runtime backlog as `CORE-004`; the paired editor/runtime integration
tasks are retired `UI-024`/`UI-025`/`UI-026`, and retired `GEOM-039` unblocks
the runtime SpatialDebug closest-face consumer in `RUNTIME-135`.

## Convergence

- The foundational tasks from the
  [`src/geometry` gap analysis](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md)
  — mesh/soup contracts (GEOM-006), robust predicates (GEOM-007), and linear
  algebra (GEOM-008) — are all retired. GEOM-009 (benchmark manifests) is
  retired in `tasks/done/` and provides the manifest-driven smoke harness
  future geometry method packages plug into.
- Retired GEOM-020 is the named follow-up to retired GEOM-008 for the direct
  sparse SPD factorization (LDLT/LLT) seam that GEOM-008 deferred but
  that retired METHOD-002 (step 2) and METHOD-003 (step 5) already
  reference as "the LDLT path from GEOM-008". Method tasks gated on the
  LDLT path can now promote against `Geometry.Sparse::SparseLDLT` /
  `SparseLLT`; remaining method gates are encoded in the method tasks'
  `depends_on` front-matter.
- Retired GEOM-023 is the named follow-up to retired GEOM-020 for the
  non-symmetric BiCGSTAB seam that METHOD-003 variant A needs for its
  closest-point-extension operator. METHOD-003 can now promote against
  `Geometry.Sparse::SparseBiCGSTAB`.
- GEOM-024 (generalized symmetric eigensolver, Spectra-backed, gated on
  GEOM-020) is the remaining solver-seam gap in this family and gates
  METHOD-006 variant B and the Spectral Conformal Parameterization method
  `methods/METHOD-024`. Promote it when either becomes the next-priority
  spectral method.
- Retired GEOM-063 and open GEOM-064 are the parameterization-family seams that realize
  Packs 3–4 of the
  [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).
  Retired GEOM-063 makes `Geometry.Parameterization` a typed CPU strategy dispatch
  surface consolidating the existing LSCM and Harmonic/Tutte solvers. The SOTA
  method tasks (ARAP/SLIM/BFF/SCP, `methods/METHOD-021..024`) add their concrete
  params types when implemented; backend policy remains with the later
  strategy-specific/runtime owners. GEOM-064 factors the
  local/global rotation-fit, symmetric-Dirichlet energy/proxy, and injective
  line search that ARAP (`METHOD-021`), SLIM (`METHOD-022`), and the optimized
  backend (`METHOD-025`) share, so no variant re-derives that nonlinear-solve
  core privately.
- GEOM-021 is a module-hygiene follow-up for retired GEOM-006: it keeps the
  `Geometry.MeshSoup` public module interface declarative by moving
  non-trivial validation/container bodies into a matching implementation unit
  and trimming interface-only includes/imports.
- GEOM-022 is the same module-interface hygiene pass for the remaining
  promoted geometry cleanup targets found by the 2026-06-06 implementation-body
  audit, excluding `Geometry.MeshSoup` which is owned by GEOM-021.
- GEOM-012 ensures mesh, graph, and point-cloud algorithms can share compatible
  property storage through explicit borrowed views instead of accidental copies.
- Retired GEOM-026 turns vertex normal recomputation into a geometry-owned CPU contract
  shared by the sandbox editor UI: `Geometry.HalfedgeMesh.Vertices.Normals`
  handles selectable face-normal averaging schemes,
  `Geometry.Graph.Vertex.Normals` handles edge-connectivity neighborhoods, and
  `Geometry.PointCloud.Normals` replaces the old `Geometry.NormalEstimation`
  point-cloud module with KDTree/default and supplied-index normal-generation
  overloads that return the written normal property.
- Retired GEOM-027 through GEOM-033 and GEOM-051 completed the
  `Geometry.Properties` cleanup found by the property-system review: name
  lifetimes, registry handle safety, `ConstPropertySet` validity,
  const-correct lookup, naming normalization, `bool` proxy-storage behavior,
  erased metadata descriptors, and live iterator/upload metadata seams. GEOM-034
  remains the separate architecture-documentation audit follow-up.
- GEOM-013 and retired GEOM-014 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md).
  Each lists explicit algorithm variants with a marked default (Manifold DC
  for GEOM-013, FA-QEM for GEOM-014). GEOM-013 (dual contouring) is a
  peer of the existing `Geometry.MarchingCubes`; retired GEOM-014 (FA-QEM) is
  the in-place extension of `Geometry.HalfedgeMesh.Simplification`. Retired GEOM-019
  supplies the harmonic/Tutte foundation consumed by GEOM-063.
- GEOM-054 is retired as Slice 0 of the registration-pipeline modularity roadmap in
  [`docs/architecture/geometry-pipeline-modularity.md`](../../../docs/architecture/geometry-pipeline-modularity.md):
  a behavior-preserving refactor of `Geometry.Registration::AlignICP` into named
  swappable stages, reusing the Algorithm-Variant-Dispatch idiom. Later slices
  (swappable correspondence/transform, global/coarse alignment, coarse-to-fine
  schedule, non-rigid *method* packages, and the schema-driven editor decoupling)
  open as each becomes the priority; named-paper stages follow retired GEOM-017's
  deferred method-package edge.
- GEOM-055 is retired as the registration-pipeline observability slice:
  `Geometry.Registration::AlignICP` accepts a null-default per-iteration
  observer and emits read-only transform/RMSE/inlier traces without changing the
  serializable `RegistrationParams` config. The editor ICP convergence
  visualization consumer retired under `UI-029`.
- RORG-031E is part of **Theme F — Architecture/runtime/UI foundation seeds**.
- Future geometry algorithm packages should follow
  [`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
  CPU reference first, correctness tests, benchmark harness, optimized CPU,
  GPU only after reference parity.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [DOCS-003 — Reconcile algorithm-variant-dispatch.md with reality and define the backend-seam template](../../archive/DOCS-003-reconcile-algorithm-variant-dispatch-doc.md) (done).
- [GEOM-020 — Sparse direct factorization solver seam (LDLT/LLT)](../../archive/GEOM-020-sparse-direct-factorization-seam.md) (done).
- [GEOM-023 — Sparse non-symmetric iterative solver seam (BiCGSTAB)](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md) (done).
- [GEOM-049 — Numeric/linear-algebra utilities (RPCA, Eigen map adapters)](../../archive/GEOM-049-numeric-linalg-utilities.md) (done).
- [GEOM-050 — Primitive and curve utilities (Bezier, triangle metrics, sphere fit, AABB)](../../archive/GEOM-050-primitive-curve-utilities.md) (done).
- [GEOIO-002 — Geometry IO parity hardening and exporters](../../archive/GEOIO-002-geometry-io-parity-hardening.md) (done).
- [GEOM-006 — Indexed mesh/soup container and conversion contracts](../../archive/GEOM-006-indexed-mesh-soup-conversion-contracts.md) (done).
- [GEOM-007 — Robust predicates and intersection classification foundation](../../archive/GEOM-007-robust-predicates-intersection-classification.md) (done).
- [GEOM-008 — Geometry linear algebra and solver infrastructure](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md) (done).
- [GEOM-009 — Geometry benchmark manifests, fixtures, and smoke benchmark](../../archive/GEOM-009-benchmark-manifests-fixtures-smoke.md) (done).
- [GEOM-010 — Point-cloud algorithm pack roadmap](../../archive/GEOM-010-point-cloud-algorithm-pack-roadmap.md) (done; roadmap: [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md)).
- [GEOM-011 — Parameterization and mapping roadmap](../../archive/GEOM-011-parameterization-mapping-roadmap.md) (done; roadmap: [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md)).
- [GEOM-012 — Symmetric mesh, graph, and point-cloud domain views](../../archive/GEOM-012-symmetric-domain-views-property-sharing.md) (done).
- [GEOM-014 — Feature-aware quadric error mesh simplification](../../done/GEOM-014-feature-aware-quadric-error-simplification.md) (done, `CPUContracted`, 2026-07-15).
- [GEOM-016 — Point-cloud filtering and density diagnostics contracts](../../archive/GEOM-016-point-cloud-filtering-density-contracts.md) (done).
- [GEOM-017 — Point-cloud descriptors and registration seams](../../archive/GEOM-017-point-cloud-descriptors-registration-seams.md) (done).
- [GEOM-018 — Parameterization distortion and map-quality diagnostics](../../archive/GEOM-018-parameterization-distortion-map-quality-diagnostics.md) (done).
- [GEOM-019 — Harmonic/Tutte parameterization and boundary constraints](../../done/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md) (done, `CPUContracted`, 2026-07-15).
- [GEOM-063 — Unified CPU parameterization strategy dispatch](../../done/GEOM-063-unified-cpu-parameterization-strategy-dispatch.md) (done, `CPUContracted`, 2026-07-15).
- [GEOM-015 — GJK termination diagnostics and scale-aware tolerance policy](../../archive/GEOM-015-gjk-termination-diagnostics.md) (done).
- [GEOM-021 — MeshSoup module implementation split](../../archive/GEOM-021-meshsoup-module-implementation-split.md).
- [GEOM-022 — Remaining geometry module implementation splits](../../archive/GEOM-022-remaining-geometry-module-implementation-splits.md).
- [GEOM-025 — UV atlas backend contract and xatlas default](../../archive/GEOM-025-uv-atlas-backend-xatlas.md) (done).
- [GEOM-026 — Cross-domain vertex normal recomputation contracts](../../archive/GEOM-026-cross-domain-vertex-normal-recompute.md) (done).
- [GEOM-027 — Property name lifetime contract](../../archive/GEOM-027-property-name-lifetime-contract.md) (done).
- [GEOM-028 — Property registry handle safety](../../archive/GEOM-028-property-registry-handle-safety.md) (done).
- [GEOM-029 — Const property set validity contract](../../archive/GEOM-029-const-property-set-validity-contract.md) (done).
- [GEOM-030 — Property set const lookup migration](../../archive/GEOM-030-property-set-const-lookup-migration.md) (done).
- [GEOM-031 — Property set naming normalization](../../archive/GEOM-031-property-set-naming-normalization.md) (done).
- [GEOM-032 — Bool property access contract](../../archive/GEOM-032-bool-property-access-contract.md) (done).
- [GEOM-033 — Erased property metadata catalog](../../archive/GEOM-033-erased-property-metadata-catalog.md) (done).
- [GEOM-034 — Geometry property API documentation audit](../../archive/GEOM-034-geometry-property-api-doc-audit.md) (done).
- [GEOM-037 — SO(3) rotation primitives (Lie machinery)](../../archive/GEOM-037-so3-rotation-primitives.md) (done).
- [GEOM-038 — Rotation averaging: SO(3) means and medians](../../archive/GEOM-038-rotation-averaging-means-medians.md) (done).
- [GEOM-039 — Accelerated mesh closest-face query and consumer adoption](../../archive/GEOM-039-accelerated-mesh-closest-face-query.md) (done).
- [GEOM-054 — Registration pipeline: extract named ICP stages](../../archive/GEOM-054-registration-pipeline-stage-extraction.md)
  (done, 2026-07-05, `Operational`): `Geometry::Registration::AlignICP` now
  runs through an internal `RunIcpLoop` stage sequence for correspondence,
  rejection, optional robust weighting, transform solve, and convergence, with
  no GEOM-054 public surface change.
- [GEOM-055 — Registration per-iteration observer](../../archive/GEOM-055-registration-iteration-observer.md)
  (done, 2026-07-05, `Operational`): `Geometry::Registration::AlignICP` now
  accepts a trailing null-default `IterationObserver` and emits an
  `IterationTrace` once per completed iteration; observed and unobserved runs
  remain numerically identical.
- [GEOM-040 — Mesh curvature tensor and principal directions](../../archive/GEOM-040-curvature-tensor-principal-directions.md) (done).
- [GEOM-041 — FEM Laplacian mass/stiffness variants and edge-weight modes](../../archive/GEOM-041-fem-laplacian-mass-stiffness-variants.md) (done).
- [GEOM-042 — Mesh normal-based bilateral denoiser](../../archive/GEOM-042-mesh-normal-bilateral-denoiser.md) (done).
- [GEOM-043 — Remeshing surface reprojection and error-bounded adaptive sizing](../../archive/GEOM-043-remeshing-reprojection-error-bounded-sizing.md) (done).
- [GEOM-044 — Sqrt-3 subdivision and Loop feature/crease masks](../../archive/GEOM-044-subdivision-sqrt3-loop-feature-masks.md) (done).
- [GEOM-045 — First-class mesh geometric-quantity accessors](../../archive/GEOM-045-first-class-mesh-quantity-accessors.md) (done).
- [GEOM-046 — Mesh topology utilities](../../archive/GEOM-046-mesh-topology-utilities.md) (done).
- [GEOM-047 — Graph and point-cloud query/noise utilities](../../archive/GEOM-047-graph-pointcloud-query-noise-utilities.md) (done).
- [GEOM-048 — Statistics accumulators and robust estimation kernels](../../archive/GEOM-048-statistics-robust-estimation-kernels.md) (done).
- [GEOM-051 — Property system enhancements](../../archive/GEOM-051-property-system-enhancements.md) (done).
- [GEOM-052 — Shared CPU/GPU backend seam + fix the KMeans phantom GPU exemplar](../../archive/GEOM-052-shared-cpu-gpu-backend-seam-kmeans-exemplar.md) (done).
- [GEOIO-003 — Mesh and point-cloud IO breadth (OFF writer, point-cloud readers)](../../archive/GEOIO-003-mesh-pointcloud-io-breadth.md) (done).
- GEOIO-002 is retired in [`tasks/done`](../../archive/GEOIO-002-geometry-io-parity-hardening.md)
  and contributed to **Theme E — Geometry IO completion** as the upstream gate
  for retired [`ASSETIO-001`](../../archive/ASSETIO-001-asset-model-texture-ingest-ownership.md)
  and asset-backed mesh residency in **Theme A — Shortest path to sandbox
  visible geometry** (`rendering/GRAPHICS-034`).
- [GEOM-005](../../archive/GEOM-005-api-style-and-numeric-policy.md) is retired in
  `tasks/done` and provides the canonical geometry API/numeric policy for future
  work.
- GEOM-010 is retired in [`tasks/done`](../../archive/GEOM-010-point-cloud-algorithm-pack-roadmap.md)
  and records the point-cloud method-compliant roadmap in
  [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md).
  Its first implementation packs are retired
  [`GEOM-016`](../../archive/GEOM-016-point-cloud-filtering-density-contracts.md)
  (filtering/density diagnostics) and
  [`GEOM-017`](../../archive/GEOM-017-point-cloud-descriptors-registration-seams.md)
  (descriptors/registration seams).
- GEOM-011 is retired in [`tasks/done`](../../archive/GEOM-011-parameterization-mapping-roadmap.md)
  and records the parameterization/mapping method-compliant roadmap in
  [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md).
  Its first implementation packs are retired GEOM-018
  (distortion/map-quality diagnostics) and retired GEOM-019
  (harmonic/Tutte parameterization and boundary constraints).
