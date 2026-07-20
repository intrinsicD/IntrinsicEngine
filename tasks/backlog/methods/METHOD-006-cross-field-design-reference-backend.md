---
id: METHOD-006
theme: I
depends_on: [GEOM-024]
maturity_target: CPUContracted
---
# METHOD-006 — Surface cross-field design CPU reference backend

## Goal
- Add a CPU reference backend that computes a smooth cross field (4-RoSy) on a triangle mesh given optional alignment constraints (boundary, sharp features, principal curvature). This is the foundational primitive for field-guided remeshing, quad meshing, and feature-aligned texture synthesis.

## Non-goals
- No quad-mesh extraction in this task. If prioritized, allocate a new unique
  task ID after this reference has evidence; `METHOD-006b` is not reserved.
- No GPU backend.
- No learned/neural backend or placeholder capability flag. Any learned
  formulation requires its own method intake, dataset/checkpoint contract, and
  task.
- No frame-field design (full 3D / hex-meshing) — only surface cross fields.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/cross_field/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #6.
- Gap analysis explicitly lists "vector-field design, cross fields, frame fields, and singularity indexing" as a P1 missing capability.
- Reuses `Geometry.Curvature` only for optional principal-curvature alignment,
  `Geometry.DEC` for mesh/mass conventions, and the CSR
  infrastructure from retired
  [`GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md).
  The existing Vector Heat module keeps its connection-angle construction
  file-local and vertex-based, while this method needs a face-frame
  connection; reproduce the paper's small face-frame formula file-locally
  rather than importing implementation internals or creating a premature
  transport framework. The selected formulation's generalized
  smallest-eigenvalue problem `A z = λ M z` is supplied by
  [`GEOM-024`](../geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md).

## Variants and default selection

- **Selected — globally optimal N-RoSy direction fields** (Knöppel, Crane,
  Pinkall, Schröder; SIGGRAPH 2013), instantiated as `N = 4`. Its generalized
  eigensolver is supplied by `GEOM-024`.
- **Prior-art comparison — MIQ cross-field formulation.** Record it in the
  intake; do not implement its mixed-integer period-jump machinery here.
- **Deferred — NeurCross/CrossGen.** These require separate learned-method
  contracts, datasets, checkpoints, and dependencies; no dormant capability
  flags are added.

## Slice plan

- **Slice A — intake and conventions.** Freeze the N-RoSy representation,
  tangent-frame/transport convention, constraint units, eigenpair
  normalization/sign rule, singularity-index convention, fixtures, tolerances,
  and failure diagnostics.
- **Slice B — unconstrained CPU reference.** Assemble/solve the smoothest
  `N = 4` field and verify topology on analytic closed surfaces.
- **Slice C — alignment constraints.** Add boundary/sharp/principal-curvature
  guidance only after the unconstrained oracle is stable.
- **Slice D — benchmark/docs.** Add executable correctness evidence and no
  quad-mesh or learned follow-up surface.

## Right-sizing

- Add one concrete cross-field module using the existing eigensolver/DEC
  seams. Do not add a general field-design framework, strategy registry, or
  neural capability flag.
- Keep the face-frame connection-angle helper private to
  `Geometry.CrossField.cpp`; do not reach into Vector Heat
  implementation internals or extract a transport abstraction without a
  second caller for that exact face-based contract.

## Backends

- Backend axis: deterministic `cpu_reference` only. Learned formulations are
  separate methods rather than backends of this contract; no optimized or GPU
  backend task is reserved.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/cross_field/`.
- [ ] Fill `method.yaml` (`id: geometry.cross_field`, metrics: `field_smoothness_dirichlet`, `singularity_count`, `alignment_residual`, `runtime_ms`).
- [ ] Fill `paper.md` with the selected objective, representation, constraints,
      tangent/transport and index conventions, units, normalization, numerical
      assumptions, and failure taxonomy.

### Public API in `src/geometry`
- [ ] Add module `Geometry.CrossField` in
      `src/geometry/Geometry.CrossField.cppm` + `.cpp`, with matching
      `Geometry::CrossField` namespace per the new-API naming policy.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::CrossField {
    struct Constraints {
      std::span<const FaceConstraint> aligned_faces;       // tangent direction per face
      std::span<const EdgeConstraint> sharp_edges;         // boundary / sharp
      bool align_to_principal_curvature = false;
    };
    struct Params { uint32_t n_rosy = 4; double smoothness_weight = 1.0; };
    struct Result {
      // Per-face complex representative z = exp(i N theta).
      std::vector<std::complex<double>> face_representative;
      // Integer numerator k for fractional index k / N at each vertex.
      std::vector<std::int32_t> vertex_singularity_index_numerator;
      Diagnostics diagnostics;
    };
    Core::Expected<Result> Compute(const HalfedgeMesh::Mesh&, const Constraints&, const Params&);

    // Convenience: convert face complex to two glm::dvec3 tangent vectors per face.
    std::vector<std::array<glm::dvec3, 2>> ExtractTangentPair(
        const HalfedgeMesh::Mesh&, std::span<const std::complex<double>>);
  }
  ```
- [ ] Register in `src/geometry/CMakeLists.txt`; do not umbrella-export initially.

### Implementation (selected globally optimal N-RoSy reference path)
- [ ] Step 1: build deterministic per-face orthonormal frames directly from
      triangle geometry under an explicitly frozen tangent-basis convention.
      Query `Geometry.Curvature` only when principal-curvature alignment is
      requested.
- [ ] Step 2: compute edge-based connection rotations between adjacent face
      frames with a file-local, convention-tested implementation of the
      selected paper's formula; do not import Vector Heat implementation
      internals.
- [ ] Step 3: assemble Dirichlet energy in `z = e^{iNθ}` per face; alignment constraints become soft / hard pins.
- [ ] Step 4: solve the smallest-eigenvalue generalized eigenproblem `A z = λ M z` via the sparse symmetric (generalized) eigensolver seam from [`GEOM-024`](../geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md) (LOBPCG or shift-invert).
- [ ] Step 5: compute singularity indices by accumulating period jumps around each vertex.

### Benchmark
- [ ] Add an executable deterministic smoke with stable ID
      `geometry.cross_field.reference.smoke`, built-in disk/sphere/torus
      fixtures, `intent: correctness`, explicit warmup/measured counts, and
      allowed metrics `runtime_ms` and `quality_error_l2`. Put smoothness,
      alignment residual, singularity counts/index sum, and eigensolver
      diagnostics in result JSON.

## Tests
- [ ] `tests/unit/geometry/Test.CrossField.cpp`.
- [ ] Flat disk: cross field with one boundary alignment → constant field, no interior singularities.
- [ ] Sphere: under the frozen fractional-index convention, total singularity
      index equals `χ(M) = 2`. Verify exactly.
- [ ] Torus: total singularity index `= 0`. Verify.
- [ ] Cube with sharp-edge alignment: singularities appear at the eight
      corners with stored numerator `+1` (fractional index `+1/4`) each.
- [ ] Determinism with fixed seed and tie-break rule for eigenvector sign.
- [ ] Fail closed on empty/non-triangle/non-manifold input, degenerate local
      frames, inconsistent hard constraints, non-finite values, and eigensolver
      non-convergence.

## Docs
- [ ] `methods/geometry/cross_field/README.md` — backend identity, selected
      globally optimal formulation, representation/index convention, and
      known limitations.
- [ ] Record quad-mesh extraction as unowned future work without reserving an
      invalid task ID.
- [ ] Record NeurCross/CrossGen only in the prior-art/out-of-scope table; do not
      present either as a planned engine backend.
- [ ] Register/document the stable correctness-smoke ID and built-in dataset.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] The globally optimal `N = 4` formulation is the sole public strategy;
      MIQ and learned alternatives remain documentation-only.
- [ ] Under the frozen fractional-index convention, the singularity index sum
      equals `χ(M)` per Poincaré–Hopf on all closed-surface tests.
- [ ] Alignment constraint test (cube corners) passes.
- [ ] Smoke manifest/result validate with the frozen singularity-index
      convention and analytic topology checks.
- [ ] No Eigen types leak through the public API.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'CrossField|Curvature|SparseEigensolver|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- No quad-mesh extraction in this task.
- No neural backend in this task.
- No external ML framework dependency, checkpoint, learned-backend token, or
  placeholder capability flag.
- No GPU backend before reference parity.

## Maturity
- Target: `CPUContracted`. The CPU reference backend is the correctness oracle for any later optimized/GPU backend.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.
