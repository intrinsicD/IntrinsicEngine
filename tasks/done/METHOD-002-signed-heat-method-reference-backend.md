---
id: METHOD-002
theme: none
depends_on: [GEOM-020]
completed_on: 2026-06-28
---
# METHOD-002 — Signed Heat Method reference backend

## Goal
- [x] Add a CPU reference backend for surface Variant A of Feng & Crane's
  Signed Heat Method: per-vertex signed geodesic distance from an oriented
  halfedge curve on a triangle mesh, mirroring the API style of the existing
  scalar heat method (`Geometry.Geodesic`) and vector heat method
  (`Geometry.VectorHeatMethod`).

## Non-goals
- No GPU backend.
- No optimized CPU backend until reference parity tests exist against the CPU
  reference.
- No replacement of the existing scalar/vector heat-method modules; this is a
  peer module, not a rewrite.
- No new IO formats; the implementation consumes existing halfedge mesh
  containers.
- Point-cloud and volumetric variants are deferred until point-cloud/grid
  Laplacian assembly tasks justify them.

## Context
- Status: retired at `CPUContracted`.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Add signed heat reference backend`).
- Owning subsystem/layer: `geometry` and `methods/geometry` (paper package).
- Method package: [`methods/geometry/signed_heat/`](../../methods/geometry/signed_heat/).
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md)
  Tier 1 #1, against gaps in
  [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md)
  (geodesics + intrinsic geometry pack; tolerant input for noisy / broken
  boundary data).
- Reuses `Geometry.DEC` cotan Laplacian / mass matrix assembly and the direct
  sparse SPD factorization seam from retired
  [`GEOM-020`](GEOM-020-sparse-direct-factorization-seam.md). The implementation
  calls `Geometry.Sparse::SparseLDLT` for both heat and Poisson solves.
- Reference C++ implementation exists in geometry-central's `signed_heat_method`
  module and remains a parity oracle, not a dependency.
- Pathfinder method per retired [`METHODS-001`](METHODS-001-signed-heat-pathfinder.md):
  this is the first method driven through paper intake, CPU reference,
  correctness tests, benchmark harness, and docs.

## Variants and default selection

- Default: **A — Signed Heat Method on surfaces (Feng & Crane, SIGGRAPH 2024).**
  Solves signed geodesic distance to a curve / region boundary on a surface mesh
  and reuses the existing halfedge DEC pipeline.
- Deferred: **B — Signed Heat Method on point clouds (Feng & Crane, SIGGRAPH
  2024).** Requires point-cloud Laplacian assembly from a follow-up to the
  [`GEOM-010` point-cloud roadmap](../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Deferred: **C — Generalized signed distance in R^n (Feng & Crane).** Requires
  `Geometry.Grid` Laplacian assembly.

## Required changes

### Method package scaffolding
- [x] Scaffold `methods/geometry/signed_heat/` with the standard method package
  structure.
- [x] Fill `method.yaml` (`id: geometry.signed_heat`, status `reference`, paper
  citation, backend `cpu_reference`, metrics `quality_error_l2` and
  `runtime_ms`).
- [x] Fill `paper.md` with claim decomposition, governing equations (heat
  diffusion of normals -> unit vector field -> Poisson recovery of scalar), and
  degenerate-input behavior.
- [x] Write `README.md` documenting backend identity (`cpu_reference`), selected
  default variant, and known limitations.

### Public API in `src/geometry`
- [x] Add `src/geometry/Geometry.HalfedgeMesh.SignedHeatMethod.cppm` + `.cpp`
  exporting module `Geometry.SignedHeatMethod`.
- [x] Mirror the existing heat-method namespace style with
  `Geometry::SignedHeatMethod`.
- [x] Public surface exposes `SignedHeatParams`, `SignedHeatDiagnostics`,
  `SignedHeatResult`, `SignedHeatStatus`, and
  `ComputeSignedDistance(HalfedgeMesh::Mesh&, std::span<const HalfedgeHandle>,
  const SignedHeatParams&)`.
- [x] Register the module in `src/geometry/CMakeLists.txt` and re-export it
  from `Geometry.cppm`.

### Implementation
- [x] Assemble cotan Laplacian `L` and lumped vertex mass `M` via
  `Geometry.DEC`.
- [x] Diffuse oriented boundary-normal impulses for `t = h^2` by solving
  `(M + tL) X = X_0` with `Geometry.Sparse::SparseLDLT`.
- [x] Normalize the diffused vector field per triangle face.
- [x] Solve the regularized Poisson system `(L + epsilon M) phi = div(X_hat)`,
  flip sign by gradient alignment, and shift the weighted boundary mean to zero.
- [x] Keep all matrix assembly behind geometry-owned sparse/Eigen adapters; no
  Eigen type appears in the public API.

## Tests
- [x] Add `tests/unit/geometry/Test.SignedHeatMethod.cpp` with CTest labels
  `unit;geometry`.
- [x] Analytic flat-grid case: signed distance to an oriented square boundary,
  with positive interior, negative exterior, finite output, and
  `quality_error_l2 < 0.40`.
- [x] Orientation regression: reversing the boundary halfedges flips the sign.
- [x] Robustness: dropping one boundary halfedge reports
  `DegenerateBoundaryInput` while preserving finite output.
- [x] Invalid-input coverage: empty mesh, empty boundary, and invalid params fail
  closed.
- [x] Determinism: identical input produces bitwise identical signed-distance
  values and diagnostics across two runs.
- [x] Add a PR-fast smoke benchmark:
  [`benchmarks/geometry/manifests/signed_heat_reference_smoke.yaml`](../../benchmarks/geometry/manifests/signed_heat_reference_smoke.yaml).

## Docs
- [x] [`methods/geometry/signed_heat/README.md`](../../methods/geometry/signed_heat/README.md)
  documents the method contract, backend status, and known limitations.
- [x] [`docs/methods/index.md`](../../docs/methods/index.md) links the method
  package and notes METHOD-002 as the pathfinder result.
- [x] [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md)
  documents the public signed heat module and its vertex-based approximation.
- [x] [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md)
  cross-links the landed method package.
- [x] Regenerate [`docs/api/generated/module_inventory.md`](../../docs/api/generated/module_inventory.md)
  with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- [x] Regenerate [`tasks/SESSION-BRIEF.md`](../SESSION-BRIEF.md).

## Acceptance criteria
- [x] Variant A is the public default; point-cloud and volumetric variants are
  explicitly deferred.
- [x] CPU reference produces signed distance within the documented tolerance on
  the analytic flat-square baseline and preserves sign under boundary
  orientation.
- [x] Diagnostics distinguish invalid input, heat/Poisson factorization and solve
  failures, degenerate input boundaries, and non-finite results.
- [x] The smoke benchmark emits `runtime_ms`, `quality_error_l2`, source counts,
  degeneracy counts, max absolute distance, and mean boundary offset in
  schema-valid JSON.
- [x] Layering check passes (`geometry -> core` only; no Eigen leakage through
  `Geometry.cppm`).

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'SignedHeatMethod' --timeout 60
./build/ci/bin/IntrinsicBenchmarkSmoke /tmp/intrinsic_signed_heat_bench
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic_signed_heat_bench --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/validate_method_manifests.py --root methods --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- No GPU backend before reference parity.
- No optimized CPU backend in this task.
- No replacement of `Geometry.Geodesic` or `Geometry.VectorHeatMethod`.
- No public Eigen types in `Geometry.HalfedgeMesh.SignedHeatMethod.cppm`.
- No dependency on geometry-central or libigl in production code paths.

## Maturity
- Target: `CPUContracted`; achieved by the CPU reference backend, unit tests,
  smoke benchmark, method manifest, and docs.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU
  backends open as separate method tasks per `AGENTS.md` §6 once reference
  parity work is selected.
