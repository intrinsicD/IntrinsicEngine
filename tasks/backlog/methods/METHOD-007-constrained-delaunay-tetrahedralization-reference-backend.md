---
id: METHOD-007
theme: I
depends_on: [GEOM-007]
maturity_target: CPUContracted
---
# METHOD-007 — Constrained Delaunay tetrahedralization reference backend

## Goal
- Add a robust constrained Delaunay tetrahedralization (CDT) reference backend and a `Geometry.TetMesh` container, enabling volumetric meshing of closed triangle-mesh boundaries for FEM / soft-body simulation / volumetric remeshing.

## Non-goals
- No quality-driven tet refinement / sizing field in this task (follow-up).
- No hexahedral meshing.
- No GPU backend.
- No FEM solver assembly — this task ships only the container, the builder, and basic quality metrics.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/constrained_delaunay_tet/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #4.
- **Hard prerequisite:** retired
  [`GEOM-007`](../../archive/GEOM-007-robust-predicates-intersection-classification.md)
  supplies filtered orientation/uncertainty diagnostics, but it did **not**
  land `inSphere`, exact/adaptive escalation, or indirect predicates. This
  task owns the selected CDT method's LNC implicit-point representation and
  formula-specific indirect `orient3d`/`inSphere` escalation; it must not
  claim those kernels already exist.
- Closes the P1 "volumetric and cell-complex containers" gap from `docs/reviews/2026-05-12-src-geometry-gap-analysis.md`.

## Variants and default selection

- **Selected — Constrained Delaunay Tetrahedrization: A Robust and Practical
  Approach** (Diazzi, Panozzo, Vaxman, Attene; arXiv:2309.09805). This task
  independently implements only the bounded reference contract derived during
  intake.
- **Prior-art comparisons — fTetWild and TetGen-style incremental CDT.** They
  inform robustness/quality threats but are not runtime dependencies or
  strategy tokens.

## Slice plan

- **Slice A — intake and container contract.** Freeze accepted boundary
  topology, robust-predicate assumptions, tet orientation/indexing, region and
  boundary semantics, quality units, fixture split, tolerances, resource caps,
  and failure taxonomy.
- **Slice B — minimal `TetMesh`.** Land only the value container and quality
  functions required by the selected builder and its tests.
- **Slice C — CPU reference builder.** Implement insertion/recovery and
  classification with deterministic ordering, analytic tests, and explicit
  cap/non-convergence diagnostics.
- **Slice D — evidence/docs.** Add the bounded checked-in regression smoke and
  volumetric architecture documentation. Tet IO and quality refinement remain
  separate tasks when a real consumer requires them.

## Right-sizing

- `TetMesh`, the selected builder, and directly used quality functions are the
  complete surface. Do not add an IO module, meshing backend registry, generic
  cell-complex framework, or refinement policy in this task.
- `METHOD-027` is a present second consumer of the landed Delaunay/TetMesh
  contract, but does not justify additional abstractions beyond the exact
  operations both tasks need.

## Backends

- Backend axis: deterministic `cpu_reference` only. Quality refinement,
  optimized CPU, and GPU paths require separate tasks after the implicit-point
  oracle and materialization diagnostics are proven.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/constrained_delaunay_tet/`.
- [ ] Fill `method.yaml` (`id: geometry.constrained_delaunay_tet`; metrics:
      `fixture_pass_count`, `fixture_case_count`, `volume_error`,
      `min_dihedral_angle`, `max_aspect_ratio`, `runtime_ms`). Report the
      bounded fixture set per case/count, not as a percentage or broad
      Thingi10k success metric.
- [ ] Fill `paper.md` with the selected algorithm boundary, assumptions,
      topology/region contract, coordinate and quality units, deterministic
      tie rules, resource caps, and failure taxonomy.

### Public API in `src/geometry`
- [ ] Add `Geometry.TetMesh` container in `src/geometry/Geometry.TetMesh.cppm` + `.cpp`. Mirror the property-set design used by `Geometry.HalfedgeMesh`:
  ```cpp
  namespace Geometry::TetMesh {
    struct VertexHandle { uint32_t id; };
    struct TetHandle    { uint32_t id; };
    class Mesh {
      // SoA: vertex positions, tet vertex indices, optional region tags.
      // Property sets: v:point, t:region, f:boundary, ...
    public:
      size_t VertexCount() const; size_t TetCount() const;
      std::span<const glm::dvec3> VertexPositions() const;
      std::span<const std::array<uint32_t,4>> TetVertices() const;
      // Boundary extraction returns a HalfedgeMesh::Mesh.
      HalfedgeMesh::Mesh ExtractBoundary() const;
    };
  }
  ```
- [ ] Add `Geometry.ConstrainedDelaunayTet` builder module in `src/geometry/Geometry.ConstrainedDelaunayTet.cppm` + `.cpp`:
  ```cpp
  namespace Geometry::ConstrainedDelaunayTet {
    struct Input { const HalfedgeMesh::Mesh& boundary; /* must be closed, watertight */ };
    struct Params {
      std::uint64_t max_steiner_points = 1'000'000;
      std::uint64_t max_recovery_steps = 4'000'000;
    };
    struct Result { TetMesh::Mesh mesh; Diagnostics diagnostics; };
    Core::Expected<Result> Build(const Input&, const Params&);
  }
  ```
- [ ] Add quality metrics module `Geometry.TetMesh.Quality` with dihedral-angle, aspect-ratio, inradius / circumradius ratio.
- [ ] Register modules in `src/geometry/CMakeLists.txt`; export `Geometry.TetMesh` from `Geometry.cppm` (container is a public type), keep the builder module out of the umbrella.

### Implementation (selected implicit-Steiner CDT reference path)
- [ ] Step 1: incremental Delaunay vertex insertion over the boundary vertices.
- [ ] Step 2: missing-segment and missing-face recovery with LNC implicit
      Steiner points. Use GEOM-007's filtered explicit-point orientation as an
      initial diagnostic where applicable, but implement the paper's
      formula-specific indirect `orient3d`/`inSphere` signs and symbolic
      perturbation for all explicit/implicit combinations; never guess an
      uncertain sign with an epsilon.
- [ ] Step 3: use the paper's modified gift-wrapping fallback when cavity
      expansion fails, with deterministic cap/non-convergence diagnostics.
- [ ] Step 4: classify interior/exterior tetrahedra by ghost-tet flood fill
      across PLC facets, preserving the input boundary exactly in the implicit
      representation.
- [ ] Step 5: if materializing floating-point Steiner coordinates for
      `TetMesh`, validate orientation/boundary conformity after rounding and
      fail closed when no valid materialization is found; do not hide the
      paper's floating-point representability limitation.

### Benchmark
- [ ] Add an executable deterministic smoke with stable ID
      `geometry.constrained_delaunay_tet.reference.smoke`, built-in cube/sphere
      plus a small checked-in adversarial dataset, `intent: correctness`,
      explicit warmup/measured counts, and allowed metrics `runtime_ms`,
      `quality_error_l2`, and `quality_error_linf`. Put tet/Steiner counts,
      volume, boundary/topology checks, quality summaries, and failure status
      in result JSON.
- [ ] Keep broad Thingi10k evaluation outside PR-fast; no external dataset or
      corpus-wide success claim is required by this task.

## Tests
- [ ] `tests/unit/geometry/Test.TetMesh.cpp` — container roundtrip, property set sanity, boundary extraction Euler check.
- [ ] `tests/unit/geometry/Test.ConstrainedDelaunayTet.cpp`.
- [ ] Unit cube boundary → tet mesh; verify volume equals 1.0 within tolerance; min dihedral > documented threshold.
- [ ] Sphere boundary → tet mesh; verify volume vs `4/3 π r³` within mesh-refinement error.
- [ ] At most ten minimized Thingi10k-derived closed-manifold fixtures under
      `tests/data/cdt/`, each with source URL, model/license provenance,
      transformation/minimization notes, and explicit expected status; report
      per-case outcomes.
- [ ] Degenerate inputs (non-manifold edge, open surface, self-intersecting boundary) — verify diagnostics report the failure mode explicitly (no crashes, no asserts).
- [ ] Verify deterministic ordering and cap exhaustion; freeze scale-relative
      volume/quality tolerances before implementing assertions. Zero caps,
      indirect-predicate escalation failure, and invalid floating-point
      materialization fail closed with distinct diagnostics.

## Docs
- [ ] `methods/geometry/constrained_delaunay_tet/README.md`.
- [ ] Add `docs/architecture/geometry-volumetric.md` (new) introducing `Geometry.TetMesh` as the canonical volumetric container; link from `docs/architecture/geometry.md`.
- [ ] Update `tasks/backlog/geometry/README.md` to reference the new container.
- [ ] Register/document the stable correctness-smoke ID and distinguish the
      bounded checked-in fixtures from later heavy corpus evidence.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] The Diazzi et al. implicit-Steiner formulation is the sole public
      builder strategy; fTetWild/TetGen remain out-of-build comparisons.
- [ ] Volume tests within tolerance for cube and sphere.
- [ ] Every declared checked-in regression fixture has an explicit expected
      success or expected failure result; broad Thingi10k success remains an
      unclaimed external study.
- [ ] Diagnostics surface non-manifold / open / self-intersecting inputs without crashing.
- [ ] Smoke manifest/result validate and report geometric/topological quality,
      not runtime alone.
- [ ] `GEOM-007` is closed before this task starts.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'TetMesh|ConstrainedDelaunay|RobustPredicates|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No TetGen/fTetWild runtime dependency or dormant alternative-strategy token.
  Out-of-build oracle comparisons require explicit license/provenance notes.
- No quality-driven refinement loop in this task (separate follow-up).
- No tet IO module in this task.
- No FEM solver coupling.
- No coupling to `runtime` / `graphics` / `ecs` / `assets` / `platform`.

## Maturity
- Target: `CPUContracted`. The CPU reference backend and `Geometry.TetMesh` container are the correctness oracle for any later optimized/GPU backend.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.
