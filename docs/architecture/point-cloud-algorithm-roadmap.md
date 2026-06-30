# Point-cloud Algorithm Roadmap

Status: roadmap / planning note for [`GEOM-010`](../../tasks/done/GEOM-010-point-cloud-algorithm-pack-roadmap.md).

This document splits point-cloud algorithm work into reviewable packs for `src/geometry` and `methods/geometry`. It describes intended task boundaries; it does not claim the listed algorithms are already implemented.

## Layer and method boundaries

- Generic containers, deterministic CPU kernels, adapters, diagnostics, and public algorithm APIs belong in `src/geometry` and must preserve the `geometry -> core` dependency rule from [`AGENTS.md`](../../AGENTS.md).
- Paper-specific method contracts, paper claim capture, backend parity, and comparison reports belong under `methods/geometry` and follow the [method workflow](../agent/method-workflow.md): paper intake, CPU reference backend, correctness tests, benchmark harness, optimized CPU backend, then optional GPU backend.
- Benchmark smoke coverage belongs under `benchmarks/geometry` and must use stable manifest IDs as described in [`benchmarks/geometry/README.md`](../../benchmarks/geometry/README.md).
- Runtime, graphics, ECS, platform, app, and asset-ingest integration are outside these point-cloud algorithm packs unless a later task explicitly owns those layer handoffs.

## Existing foundation

The current promoted geometry layer already provides useful point-cloud foundations:

- `Geometry.PointCloud` owns the `Geometry::PointCloud::Cloud` container with canonical `v:point` positions and optional normal/color/radius attributes.
- `Geometry.PointCloud.Utils` includes statistics, voxel downsampling, random
  subsampling, deterministic seeded Gaussian noise scaled to mean spacing, radius
  estimation, bilateral filtering, outlier-score estimation, and kernel-density
  estimation.
- `Geometry.PointCloud.Normals` provides KDTree-backed PCA normal estimation,
  MST orientation, supplied-index overloads, and property-writing `v:normal`
  recompute contracts.
- `Geometry.PCA`, `Geometry.KMeans`, `Geometry.KDTree`, and `Geometry.Octree` provide reusable numerical/spatial building blocks.
- `Geometry.Registration` provides point-to-point and point-to-plane ICP over borrowed point spans.
- `Geometry.SurfaceReconstruction` provides Hoppe-style implicit reconstruction through Marching Cubes.
- `Geometry.PointCloud.IO` and `Geometry.PointCloud.Conversion` provide point-cloud IO and explicit conversion seams.
- `GEOM-005` through `GEOM-009` provide the style/numeric policy, mesh/soup conversion contracts, robust predicates, reusable numerical infrastructure, and benchmark manifest groundwork future packs should use.

The gaps below come from the [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md), especially the point-cloud, reproducibility, diagnostics, and benchmark sections.

## Pack 1 — Filtering, downsampling, and density diagnostics

Follow-up task: [`GEOM-016`](../../tasks/done/GEOM-016-point-cloud-filtering-density-contracts.md).

Scope:

- Harden existing voxel downsampling, random subsampling, bilateral filtering, outlier-score estimation, kernel-density estimation, and radius-estimation APIs into a consistent diagnostic contract.
- Add explicit statistical-outlier-removal and radius-outlier-removal result surfaces rather than only score publication.
- Preserve deterministic output order and seed-controlled randomized subsampling.

Primary home: `src/geometry`.

Required structures and dependencies:

- `Geometry.PointCloud` borrowed/owning cloud semantics and canonical `v:point` positions.
- `Geometry.KDTree` / `Geometry.Octree` for k-nearest and radius queries.
- `Geometry.RobustPredicates` scale-aware checks from `GEOM-007` / `GEOM-015` for degenerate radii, duplicate points, and near-zero neighborhoods.
- `Geometry.Linalg` covariance helpers from `GEOM-008` only where dense diagnostics are needed; do not expose Eigen types through public point-cloud APIs.

Correctness fixtures:

- Uniform grid with known voxel occupancy and deterministic centroid order.
- Two-cluster cloud plus isolated outliers for statistical and radius removal.
- Duplicate/coincident points, empty cloud, one-point cloud, and non-finite input rejection.
- Seeded random subsampling fixture proving identical selected indices for identical `(points, seed, target)`.

Diagnostics:

- Input point count, output point count, deleted/rejected count, occupied voxel count, empty-neighborhood count, non-finite input count, duplicate/coincident count, used radius or k-neighbor policy, and deterministic seed when applicable.

Benchmark manifests:

- Smoke: small built-in grid/noise fixture with `benchmark_id` similar to `geometry.pointcloud.filtering.smoke`.
- Metrics: `runtime_ms`, output-count ratio, outlier precision/recall on synthetic labels, and mean displacement for smoothing.
- Heavy/nightly follow-up: larger scanned point clouds and scale-varied synthetic noise fields; not part of the smoke task.

Forbidden shortcuts:

- Do not silently mutate input clouds for APIs documented as filters returning owned results.
- Do not use wall-clock randomness or unordered-container iteration order as observable output order.
- Do not add renderer/runtime/asset dependencies.

## Pack 2 — Keypoints, descriptors, matching, and coarse alignment

Follow-up task: [`GEOM-017`](../../tasks/backlog/geometry/GEOM-017-point-cloud-descriptors-registration-seams.md).

Scope:

- Add generic keypoint and descriptor infrastructure that can feed robust registration and reconstruction tasks.
- Define stable descriptor storage for ISS/Harris-style keypoints, FPFH/SHOT-like local descriptors, correspondence pairs, and match diagnostics.
- Add a deterministic feature-based coarse-alignment seam that prepares ICP or method-specific robust global registration backends.

Primary home: generic descriptor/correspondence records in `src/geometry`; paper-specific robust global registration variants in `methods/geometry` once the generic seam exists.

Required structures and dependencies:

- `Geometry.PointCloud` / domain views from `GEOM-012` for borrowed point and normal spans.
- `Geometry.PointCloud.Normals` for normal prerequisites and normal-confidence diagnostics.
- `Geometry.KDTree` / `Geometry.Octree` for local support neighborhoods.
- `Geometry.Linalg` covariance/eigen helpers from `GEOM-008` for keypoint saliency and local reference frames.
- `Geometry.Registration` for ICP refinement after coarse alignment.
- `GEOM-009` benchmark manifests for descriptor repeatability and registration quality smoke cases.

Correctness fixtures:

- Translated/rotated cube-corner or sphere-with-landmarks fixture where expected keypoints are stable under rigid transforms.
- Partial-overlap pair with known rigid transform and injected outliers.
- Degenerate coplanar/collinear local neighborhoods that must produce diagnostics rather than unstable descriptors.
- Descriptor matching fixture with deterministic tie-breaking.

Diagnostics:

- Keypoint count, suppressed candidate count, degenerate neighborhood count, descriptor dimension, invalid-normal count, match count, mutual-match count, rejected-correspondence count, estimated transform, inlier ratio, residual history, and backend identity.

Benchmark manifests:

- Smoke: small synthetic rigid transform with deterministic outliers, reporting transform error and inlier ratio.
- Heavy/nightly follow-up: real scan-pair fixtures and broader overlap/noise sweeps.

Forbidden shortcuts:

- Do not bake TEASER/CPD or other paper-specific claims into `src/geometry` before a method package captures paper assumptions and parity metrics.
- Do not require normals without an explicit precondition or normal-estimation path.
- Do not make ICP the only registration path; the descriptor/matching seam must allow coarse alignment and future robust backends.

## Pack 3 — Robust/global and multiway registration methods

Scope:

- Implement robust global registration methods after Pack 2 provides descriptors and correspondences.
- Add pairwise-to-multiway registration graph records, pose-node diagnostics, and loop-closure residual metrics.

Primary home: `methods/geometry` for paper-specific TEASER-style, CPD, generalized ICP, or colored-ICP variants; generic pose/correspondence records may be promoted to `src/geometry` only when multiple methods need them.

Dependencies:

- Pack 2 descriptor/correspondence seam.
- `Geometry.Registration` ICP refinement.
- Future optimization framework from the gap analysis for CPD, colored ICP, or nonlinear pose graph refinement.

Correctness and benchmarks:

- Known-transform synthetic pairs with controlled overlap/outlier ratios.
- Three-node loop graph with known closure residual.
- Backend parity reports comparing CPU reference output to any optimized backend.

Reproducibility requirements:

- All RANSAC-style sampling must accept an explicit seed and report sample count, consensus threshold, consensus set size, and early-exit reason.
- Tie-breaking for equally good correspondences or transforms must be deterministic.

## Pack 4 — Smoothing and robust normal orientation

Scope:

- Add MLS/RIMLS smoothing and robust normal-orientation variants beyond the existing PCA + MST baseline.
- Clarify which smoothing APIs mutate a borrowed cloud and which return owned filtered positions/normals.

Primary home: `src/geometry` for classical generic smoothing/orientation kernels; `methods/geometry` for paper-backed RIMLS variants if a task adopts a specific paper.

Dependencies:

- Pack 1 density/outlier diagnostics.
- `Geometry.PointCloud.Normals`, `Geometry.KDTree`, and `Geometry.Linalg`.

Correctness and benchmarks:

- Noisy plane/sphere fixtures with expected normal variance reduction.
- Sharp-edge fixture proving feature-aware filters avoid over-smoothing across discontinuities.
- Degenerate neighborhood diagnostics for low-density or duplicated points.

## Pack 5 — Primitive fitting and segmentation

Scope:

- Add RANSAC plane/sphere/cylinder primitive fitting with explicit model diagnostics and deterministic sampling.
- Add region-growing or clustering seams that operate on point attributes and normal/descriptor fields.

Primary home: `src/geometry` for generic primitive records and deterministic RANSAC kernels; method-specific segmentation papers under `methods/geometry`.

Dependencies:

- `Geometry.RobustPredicates` for scale-aware inlier tests.
- `Geometry.KDTree` / `Geometry.Octree` for neighborhoods.
- `Geometry.Linalg` least-squares helpers for model refinement.
- Seeded reproducibility state shared with Pack 1 and Pack 3.

Correctness and benchmarks:

- Synthetic plane/sphere/cylinder fixtures with known inliers and outliers.
- Scale-varied fixtures proving tolerance policy is expressed relative to scene scale.
- Deterministic RANSAC replay from `(seed, max_samples, threshold)`.

## Pack 6 — Reconstruction alternatives

Scope:

- Add reconstruction algorithms beyond the existing Hoppe-style implicit reconstruction: screened Poisson CPU reference, ball pivoting, alpha shapes, Delaunay/restricted-Delaunay candidates, and RIMLS implicit surfaces.

Primary home: `src/geometry` for generic reconstruction APIs and non-paper classical references; `methods/geometry` for paper-specific variants or comparison packages.

Dependencies:

- Pack 4 robust normals/smoothing.
- `Geometry.SurfaceReconstruction`, `Geometry.MarchingCubes`, `Geometry.Grid`, `Geometry.SDF`, `Geometry.MeshSoup`, and halfedge conversion contracts.
- `Geometry.Sparse` / `Geometry.Linalg` from `GEOM-008` for screened Poisson systems.
- `GEOM-007` robust predicates for alpha-shape/Delaunay boundary decisions.

Correctness and benchmarks:

- Oriented sphere and box point-cloud fixtures with distance-to-surface and topology diagnostics.
- Open-surface and noisy/outlier fixtures where failures are reported with structured diagnostics.
- Smoke manifests report face/vertex count, watertightness where applicable, signed-distance residuals, and runtime.

Forbidden shortcuts:

- Do not adopt learned reconstruction models as the default reference backend.
- Do not claim quality or performance improvements without baseline comparisons against the existing Hoppe/Marching-Cubes path.

## Cross-pack reproducibility policy

Point-cloud algorithms must make stochastic and order-sensitive behavior explicit:

- Randomized APIs accept a seed or deterministic RNG-state record in params and report the seed used in diagnostics.
- RANSAC, random subsampling, stochastic clustering, and feature-match sampling must produce repeatable results for identical inputs and parameters.
- APIs using unordered maps/sets internally must sort or otherwise stabilize externally visible output ordering.
- Diagnostics should distinguish invalid input, insufficient support, numerical degeneracy, no consensus/no convergence, and successful completion.
- Benchmark smoke fixtures must be small, checked in or built in, CPU-only, deterministic in numerical metrics, and free of renderer/runtime/asset dependencies.

## Initial priority

The first two implementation packs are:

1. [`GEOM-016`](../../tasks/done/GEOM-016-point-cloud-filtering-density-contracts.md) — filtering, downsampling, outlier removal, and density diagnostics. This is the smallest unblocked pack because much of the public surface already exists and needs hardening, tests, diagnostics, and smoke benchmarking.
2. [`GEOM-017`](../../tasks/backlog/geometry/GEOM-017-point-cloud-descriptors-registration-seams.md) — keypoints, descriptors, matching, and coarse registration seams. This unlocks robust/global registration and reconstruction work without prematurely importing paper-specific TEASER/CPD assumptions into `src/geometry`.

Later packs should not begin until their prerequisites are either retired to `tasks/done/` or recorded as explicit out-of-scope assumptions in the candidate task file.
