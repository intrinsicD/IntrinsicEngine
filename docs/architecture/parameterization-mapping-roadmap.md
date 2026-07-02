# Parameterization and Mapping Roadmap

Status: roadmap / planning note for [`GEOM-011`](../../tasks/done/GEOM-011-parameterization-mapping-roadmap.md).

This document splits parameterization, atlas, distortion, and surface-map work into reviewable packs for `src/geometry` and `methods/geometry`. It describes future task boundaries; it does not claim the listed algorithms are already implemented.

## Layer and method boundaries

- Generic parameterization records, deterministic CPU kernels, diagnostics, mesh-domain adapters, and public algorithm APIs belong in `src/geometry` and must preserve the `geometry -> core` dependency rule from [`AGENTS.md`](../../AGENTS.md).
- Paper-specific method contracts, paper claim capture, parity reports, and optional comparison backends belong under `methods/geometry` and follow the [method workflow](../agent/method-workflow.md): paper intake, CPU reference backend, correctness tests, benchmark harness, optimized CPU backend, then optional GPU backend.
- Benchmark smoke coverage belongs under `benchmarks/geometry` and must use stable manifest IDs as described in [`benchmarks/geometry/README.md`](../../benchmarks/geometry/README.md).
- Renderer/material/UV asset-pipeline integration is outside these packs. `src/geometry` may produce UVs, charts, maps, and diagnostics, but it must not import assets, ECS, graphics, runtime, platform, or app layers.

## Existing foundation

The current promoted geometry layer already provides useful parameterization and mapping foundations:

- `Geometry.Parameterization` provides LSCM for disk-topology triangle meshes, including pinned-vertex selection, UV output, conformal distortion summaries, flipped-triangle counts, and optional mesh-backed `v:texcoord` / `v:lscm_pinned` properties.
- `Geometry.Parameterization.Diagnostics` provides the reusable diagnostics record for mesh positions plus per-vertex UVs, including evaluated/skipped counts, invalid-input classification, flipped elements, conformal/area/symmetric-Dirichlet/stretch metrics, deterministic boundary length distortion, and seam-discontinuity placeholders.
- `Geometry.UvAtlas` provides the backend-neutral UV atlas seam with authored-UV preservation, source xrefs for seam splits, chart/seam-cut records, GEOM-018 quality diagnostics, a method selector for `XAtlas` versus `FastStaged`, and `jpcy/xatlas` as the default concrete CPU backend through the repository vcpkg overlay. `FastStaged` now has a conservative built-in backend that cuts one chart per triangle, performs local isometric flattening, and grid-packs finite non-overlapping UVs; xatlas remains the promoted default until multi-face charting, a faster packer, and benchmark quality gates justify promotion.
- `Extrinsic.Runtime.AssetMeshNormals` consumes `Geometry.UvAtlas` from the runtime layer for imported renderable meshes, preserving valid authored UVs or generating atlas UVs before generated texture bakes. Geometry stays independent of assets, ECS, graphics, runtime, platform, and app layers.
- `Geometry.DEC` and the reusable `Geometry.Sparse` seam provide sparse matrix and conjugate-gradient infrastructure that future parameterization solvers can share.
- `Geometry.Linalg` provides dense decomposition, covariance, least-squares, and GLM/Eigen adapter utilities behind an explicit geometry-owned numerical module.
- `Geometry.HalfedgeMesh.Boundary`, `Geometry.HalfedgeMesh.Analysis`, `Geometry.HalfedgeMesh.Quality`, and mesh/soup conversion contracts provide topology and fixture utilities for disk topology, boundary loops, degenerate faces, and validation.
- `Geometry.HtexPatch` provides patch metadata and simple atlas layout helpers, but it is not a complete UV atlas segmentation or chart-packing system.
- `GEOM-005` through `GEOM-009` provide the style/numeric policy, mesh/soup conversion contracts, robust predicates, reusable numerical infrastructure, and benchmark manifest groundwork future packs should use.

The gaps below come from the [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md), especially the parameterization/mapping, diagnostics, reproducibility, and benchmark sections.

## Pack 1 — Distortion and map-quality diagnostics

Follow-up task: [`GEOM-018`](../../tasks/done/GEOM-018-parameterization-distortion-map-quality-diagnostics.md).

Scope:

- Add standalone diagnostics for UV parameterizations and surface maps before adding more solvers.
- Compute conformal, authalic/area, symmetric Dirichlet, stretch, flipped-element, boundary-distortion, seam-continuity, and invalid-UV metrics where the required inputs are available.
- Normalize diagnostics so LSCM, harmonic/Tutte, ARAP, SLIM, atlas, and map-storage tasks can report comparable metrics.

Primary home: `src/geometry`.

Required structures and dependencies:

- `Geometry.Parameterization` result records and mesh-backed `v:texcoord` data.
- `Geometry.HalfedgeMesh` face/edge traversal and boundary-loop helpers.
- `Geometry.RobustPredicates` for scale-aware degenerate triangle and signed-area classification where predicates are sufficient.
- `Geometry.Linalg` only behind geometry-owned APIs for small Jacobian/SVD helpers; do not expose Eigen types through public diagnostics.
- `GEOM-009` benchmark manifests for stable smoke metrics.

Correctness fixtures:

- Single triangle and square disk fixtures with identity UVs and known zero/near-zero distortion.
- Stretched rectangle fixture with analytically predictable stretch and area ratio.
- Flipped UV triangle fixture that reports flipped elements without crashing.
- Degenerate 3D and degenerate UV triangle fixtures that report invalid/skipped counts.
- Boundary loop fixture proving boundary length/angle distortion is deterministic.

Diagnostics:

- Face count evaluated, skipped degenerate 3D faces, skipped degenerate UV faces, flipped UV face count, mean/max conformal distortion, mean/max area ratio, symmetric-Dirichlet mean/max, stretch mean/max, boundary-length ratio range, seam discontinuity count, and status/failure reason.

Benchmark manifests:

- Smoke: small built-in disk fixtures with `benchmark_id` similar to `geometry.parameterization.diagnostics.smoke`.
- Metrics: `runtime_ms` and `quality_error_l2` in the current smoke schema, with deterministic diagnostic fields for conformal distortion, area distortion, stretch, evaluated faces, and flipped elements. Larger error-vector reporting remains a future benchmark-schema extension.
- Heavy/nightly follow-up: larger atlas/map corpora; not part of the smoke task.

Forbidden shortcuts:

- Do not treat a solver's internal quality summary as the only diagnostics surface.
- Do not silently ignore flipped or degenerate elements; report counts and status.
- Do not add renderer/material/asset dependencies.

## Pack 2 — Harmonic/Tutte embedding and boundary constraints

Follow-up task: [`GEOM-019`](../../tasks/backlog/geometry/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md).

Scope:

- Add the first new solver family after diagnostics: harmonic/Tutte parameterization for disk-topology meshes with explicit boundary-condition records.
- Define reusable boundary mapping policies for circle, square, arc-length, fixed/pinned vertices, and caller-provided boundary UVs.
- Use Pack 1 diagnostics to validate output quality and flipped-triangle behavior.

Primary home: `src/geometry`.

Required structures and dependencies:

- `Geometry.Parameterization` module or a narrow sibling module if the public surface grows beyond LSCM.
- `Geometry.HalfedgeMesh.Boundary` for boundary-loop extraction and disk-topology checks.
- `Geometry.Sparse` / `Geometry.DEC` for Laplacian assembly and sparse solves.
- `Geometry.RobustPredicates` for boundary degeneracy and signed-area checks.
- Pack 1 diagnostics for acceptance tests and benchmark quality metrics.

Correctness fixtures:

- Convex square disk with fixed square boundary; interior vertex should land at the expected harmonic average.
- Circle-boundary disk fixture with deterministic arc-length boundary placement.
- Non-disk topology, closed mesh, multiple-boundary, degenerate-boundary, and insufficient-vertex fixtures that return explicit diagnostics.
- Flipped-triangle regression fixture proving the selected boundary policy and weights avoid flips under documented preconditions.

Diagnostics:

- Boundary loop count, selected boundary policy, fixed/pinned vertex count, interior unknown count, sparse-system dimensions/nonzeros, solver convergence reason, residual, iteration count, flipped triangle count, and Pack 1 distortion summary.

Benchmark manifests:

- Smoke: small disk mesh with analytic or symmetry-based expected UVs.
- Metrics: `runtime_ms`, `quality_error_l2` for expected UV positions, and distortion metrics from Pack 1.

Forbidden shortcuts:

- Do not add ARAP/SLIM nonlinear optimization to the harmonic/Tutte task.
- Do not silently fall back from invalid topology to arbitrary projection.
- Do not expose Eigen types in the public module interface.

## Pack 3 — ARAP parameterization and local/global optimization

Scope:

- Add ARAP parameterization after Pack 1 diagnostics and Pack 2 boundary records exist.
- Define local/global iteration records, rotation-fit diagnostics, and convergence status.

Primary home: `src/geometry` for the generic ARAP solver; paper-specific variants under `methods/geometry` when tied to a paper contract.

Dependencies:

- Pack 1 distortion diagnostics.
- Pack 2 boundary constraints and initial embedding.
- `Geometry.Linalg` for per-triangle local rotations/polar decomposition.
- Future generic optimization framework if ARAP/SLIM share enough nonlinear solve structure to justify it.

Correctness and benchmarks:

- Planar disk fixture where ARAP preserves a known low-distortion embedding.
- Deformed boundary fixture with decreasing energy across iterations.
- Degenerate triangle and singular local-step fixtures with explicit failure diagnostics.

## Pack 4 — SLIM and advanced distortion energies

Scope:

- Add SLIM-style injective mapping and advanced distortion energies only after the ARAP/local-global seam and diagnostics are in place.
- Treat SLIM paper-specific claims as method-workflow work when parity against a reference implementation is required.

Primary home: `methods/geometry` for paper claim capture and parity; reusable energy records may be promoted to `src/geometry` if shared by ARAP, SLIM, and map-quality diagnostics.

Dependencies:

- Pack 1 diagnostics.
- Pack 3 local/global optimization seam.
- Robust invalid/flipped element policy.

Correctness and benchmarks:

- Injectivity/regression fixtures with known difficult boundaries.
- Energy monotonicity and line-search diagnostics where applicable.
- Backend parity reports before any optimized backend is added.

## Pack 5 — Atlas segmentation, seam generation, and chart packing

Implementation tasks:

- [`GEOM-025`](../../tasks/done/GEOM-025-uv-atlas-backend-xatlas.md) established the backend-neutral UV atlas contract and xatlas default.
- [`GEOM-057`](../../tasks/active/GEOM-057-fast-uv-atlas-charting-and-packing.md) opens the fast staged replacement path while keeping xatlas as the visible fallback.

Scope:

- Add geometry-owned chart records, seam cuts, atlas segmentation, and CPU chart packing suitable for later renderer/material consumers without depending on those layers. Current promoted state is the `Geometry.UvAtlas` backend contract with xatlas as the default concrete CPU backend and a requestable `FastStaged` method backed by deterministic per-triangle charting, local flattening, grid packing, chart records, seam records, and explicit xatlas fallback diagnostics for failing caller-supplied fast backends. Future GEOM-057 slices should replace the per-triangle lower-bound backend with PartUV-inspired multi-face chart proposals, per-chart LSCM/harmonic parameterization where topology allows, and a TABI-inspired fast packer before making the fast method the default.
- Clarify how `Geometry.HtexPatch` patch metadata relates to UV charts and atlas tiles.

Primary home: `src/geometry`.

Dependencies:

- Pack 1 diagnostics for per-chart and whole-atlas quality.
- Boundary and mesh analysis helpers for seams, connected charts, and non-manifold rejection.
- Mesh/soup conversion contracts where atlas generation consumes imported polygon soup or emits charted mesh data.
- `INFRA-001` vcpkg manifest mode for the pinned `xatlas` overlay port.
- The TABI paper (`arXiv:2602.07782`) for the future packing stage and the PartUV paper (`arXiv:2511.16659`) for the future chart proposal policy. The generic engine implementation must remain deterministic and geometry-owned; paper-specific parity reports belong under `methods/geometry` if needed.

Correctness and benchmarks:

- Mesh with known seams and chart count.
- Packing fixture with deterministic tile positions and no overlap.
- UV-seam continuity/discontinuity diagnostics.
- Fixtures proving the built-in fast staged backend emits finite non-overlapping UVs, chart records, seam cuts, and property xrefs, plus fallback fixtures proving `RequestedMethod`, `ActualMethod`, and `UsedFallback` distinguish xatlas compatibility from the fast staged method actually running.

Forbidden shortcuts:

- Do not make `graphics` or `assets` own chart packing.
- Do not assume material or texture import semantics in geometry-owned atlas records.

## Pack 6 — Surface-to-surface map storage and functional maps

Scope:

- Add map representation records before implementing advanced map solvers.
- Support barycentric point-to-surface maps, landmark/correspondence graphs, map composition diagnostics, and map-quality metrics.
- Defer functional-map solvers to method packages unless a generic CPU reference task defines the contract first.

Primary home: generic map records in `src/geometry`; paper-specific functional-map algorithms under `methods/geometry`.

Dependencies:

- Domain-view semantics from `GEOM-012` for mesh/graph/point-cloud inputs.
- `Geometry.KDTree` / `Geometry.BVH` for closest-point initialization where needed.
- `Geometry.Linalg` / `Geometry.Sparse` for functional-map spectral bases only after a method task declares the backend contract.

Correctness and benchmarks:

- Identity map fixture over the same mesh.
- Barycentric map fixture over a triangle/square mesh with known interpolation.
- Composition/inverse-consistency diagnostics on small synthetic correspondences.

## Cross-pack correctness policy

Parameterization and mapping tasks must make topology, boundary, and degeneracy behavior explicit:

- Solvers report unsupported topology instead of projecting arbitrary meshes into UV space.
- Boundary-condition records are explicit and deterministic.
- Degenerate 3D triangles, degenerate UV triangles, flipped elements, singular systems, non-convergence, and invalid caller-provided pins are all reported through diagnostics.
- Public APIs keep GLM and geometry-owned records as the storage vocabulary; Eigen remains behind `Geometry.Linalg` / implementation units.
- Benchmark smoke fixtures are small, CPU-only, deterministic in numerical metrics, and free of renderer/material/asset dependencies.

## Initial priority

The first two implementation packs are:

1. [`GEOM-018`](../../tasks/done/GEOM-018-parameterization-distortion-map-quality-diagnostics.md) — distortion and map-quality diagnostics. This pack gives all later parameterization and mapping solvers a shared acceptance vocabulary and can harden the existing LSCM quality path without adding a new solver.
2. [`GEOM-019`](../../tasks/backlog/geometry/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md) — harmonic/Tutte embedding and boundary constraints. This is the smallest new solver family after diagnostics and provides an initialization path for ARAP/SLIM.

Later packs should not begin until their prerequisites are retired to `tasks/done/` or recorded as explicit out-of-scope assumptions in the candidate task file.
