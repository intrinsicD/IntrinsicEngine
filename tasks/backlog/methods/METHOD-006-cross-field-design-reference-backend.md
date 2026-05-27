# METHOD-006 — Cross-field / frame-field design reference backend

## Goal
- Add a CPU reference backend that computes a smooth cross field (4-RoSy) on a triangle mesh given optional alignment constraints (boundary, sharp features, principal curvature). This is the foundational primitive for field-guided remeshing, quad meshing, and feature-aligned texture synthesis.

## Non-goals
- No quad-mesh extraction in this task (that is a separate METHOD-006b follow-up; this task only produces the field).
- No GPU backend.
- No learned / neural backend as the default (may be added later behind a capability flag; see Variants C/D).
- No frame-field design (full 3D / hex-meshing) — only surface cross fields.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/cross_field/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 2 #6.
- Gap analysis explicitly lists "vector-field design, cross fields, frame fields, and singularity indexing" as a P1 missing capability.
- Reuses `Geometry.HalfedgeMesh.Curvature` (principal curvature alignment), `Geometry.HalfedgeMesh.DEC` (Laplacian), `Geometry.HalfedgeMesh.VectorHeatMethod` (parallel transport on the surface), and the CSR builder / CG iterative solver from retired [`GEOM-008`](../../done/GEOM-008-linear-algebra-solver-infrastructure.md). **Solver gap (Step 4):** variant B's generalized smallest-eigenvalue problem `A z = λ M z` needs a sparse symmetric eigensolver (LOBPCG / shift-invert). That seam is **not** shipped by GEOM-008 (which ships only CG / shifted CG) and is **not** owned by the LDLT follow-up [`GEOM-020`](../geometry/GEOM-020-sparse-direct-factorization-seam.md). A separate eigensolver follow-up (likely adding Spectra as a dependency) must be filed and retired before this task can promote on variant B; on variants A / C / D the gap may differ.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become opt-in capabilities or follow-up tasks.

- [ ] **A — Mixed-Integer Quadrangulation cross fields (Bommes, Zimmer, Kobbelt; SIGGRAPH 2009).** Period-jump / integer-rotation formulation; mature, deterministic, no ML. Most faithful to the engine's "CPU reference first" policy. Recommended default for a numerical reference.
- [ ] **B — N-RoSy via globally optimal direction fields (Knöppel, Crane, Pinkall, Schröder; SIGGRAPH 2013).** Sparse generalized eigenvalue problem; tighter integration with the existing DEC / vector-heat stack. Recommended default if alignment-with-the-rest-of-the-engine matters more than direct paper-match.
- [ ] **C — NeurCross (Dong et al., arXiv:2405.13745).** Jointly optimises a cross field and a neural SDF, principal-curvature-aligned. Pulls in a neural training step; not a reference-first backend.
- [ ] **D — CrossGen (Liu et al., arXiv:2506.07020).** Feed-forward generative model in a joint latent space; ≤1 s per shape; depends on a pretrained model checkpoint.

Default recommendation: **B** (Knöppel et al. globally-optimal direction fields) — it reuses the DEC and parallel-transport infrastructure the engine already has, has a clean sparse-eigenvalue formulation, and is deterministic. Variants C and D belong in a later "optimised neural backend" task once a reference exists.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/cross_field/`.
- [ ] Fill `method.yaml` (`id: geometry.cross_field`, metrics: `field_smoothness_dirichlet`, `singularity_count`, `alignment_residual`, `runtime_ms`).
- [ ] Fill `paper.md` for the selected variant.

### Public API in `src/geometry`
- [ ] Add module `Geometry.HalfedgeMesh.CrossField` in `src/geometry/Geometry.HalfedgeMesh.CrossField.cppm` + `.cpp`.
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
      // Per-face complex representative of the N-RoSy field (variant B convention).
      std::vector<std::complex<double>> face_representative;
      // Singularity index per vertex (variant A/B both expose this).
      std::vector<int8_t> vertex_singularity_index;
      Diagnostics diagnostics;
    };
    Core::Expected<Result> Compute(const HalfedgeMesh::Mesh&, const Constraints&, const Params&);

    // Convenience: convert face complex to two glm::dvec3 tangent vectors per face.
    std::vector<std::array<glm::dvec3, 2>> ExtractTangentPair(
        const HalfedgeMesh::Mesh&, std::span<const std::complex<double>>);
  }
  ```
- [ ] Register in `src/geometry/CMakeLists.txt`; do not umbrella-export initially.

### Implementation (variant B reference path)
- [ ] Step 1: build per-face local frames using `Geometry.HalfedgeMesh.Curvature` tangent basis.
- [ ] Step 2: compute edge-based parallel-transport rotations between adjacent face frames (reuse `Geometry.HalfedgeMesh.VectorHeatMethod` connection internals).
- [ ] Step 3: assemble Dirichlet energy in `z = e^{iNθ}` per face; alignment constraints become soft / hard pins.
- [ ] Step 4: solve the smallest-eigenvalue generalized eigenproblem `A z = λ M z` via a sparse symmetric (generalized) eigensolver (LOBPCG or shift-invert). Neither retired GEOM-008 nor follow-up GEOM-020 ship this seam; this step is blocked on a separate sparse-eigensolver follow-up (Spectra-backed) that must be filed before this slice is implementable.
- [ ] Step 5: compute singularity indices by accumulating period jumps around each vertex.

## Tests
- [ ] `tests/unit/geometry/Test.CrossField.cpp`.
- [ ] Flat disk: cross field with one boundary alignment → constant field, no interior singularities.
- [ ] Sphere: any 4-RoSy field must have total singularity index `= 2 * χ(M) = 4`. Verify exactly.
- [ ] Torus: total singularity index `= 0`. Verify.
- [ ] Cube with sharp-edge alignment: singularities appear at the eight corners with index `+1/4` each.
- [ ] Determinism with fixed seed and tie-break rule for eigenvector sign.

## Docs
- [ ] `methods/geometry/cross_field/README.md` — backend identity, default variant.
- [ ] Add a roadmap follow-up for quad-mesh extraction (`METHOD-006b-quad-mesh-extraction-from-cross-field.md`, not created in this task).
- [ ] Document neural variants (C, D) as future optimised backends in the README with a clear "not yet implemented" status.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Singularity index sum equals `2χ` per the Poincaré–Hopf theorem on all closed-surface tests.
- [ ] Alignment constraint test (cube corners) passes.
- [ ] No Eigen types leak through the public API.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CrossField|Curvature|VectorHeatMethod' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No quad-mesh extraction in this task.
- No neural backend as the default until the reference exists.
- No external ML framework dependency in production code (PyTorch, ONNX runtime) — variants C/D require their own enabling task and capability flag.
- No GPU backend before reference parity.
