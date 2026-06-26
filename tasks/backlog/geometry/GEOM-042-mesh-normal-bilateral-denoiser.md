---
id: GEOM-042
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-042 — Mesh normal-based bilateral denoiser

## Goal

- [ ] Add a feature-preserving mesh denoiser to the geometry layer: a two-stage
      algorithm that (1) bilaterally filters per-face normals, then (2) updates
      vertex positions by integrating ("projecting" toward) the filtered face
      normals.
- [ ] Expose the denoiser as a new entry point on the existing
      `Geometry::Smoothing` namespace (module `Geometry.Smoothing`, interface
      `src/geometry/Geometry.HalfedgeMesh.Smoothing.cppm`), filling a gap: the
      engine currently has bilateral filtering only for point clouds, not for
      triangle meshes.
- [ ] Stay entirely within the geometry layer, deterministic and fail-closed,
      with no renderer/runtime/UI coupling.

## Non-goals

- No changes to the existing Laplacian/Taubin/Implicit smoothing operators
  (`UniformLaplacian`, `CotanLaplacian`, `Taubin`, `ImplicitLaplacian`); they
  already exist and are out of scope.
- No GPU/compute backend — CPU reference only.
- No UI/editor surface. The editor denoise window is owned by UI-024; this task
  must not add any `src/runtime/*` (Editor/Visualization/SpatialDebug) code.
- Do not wire the denoiser into remeshing
  (`Geometry.HalfedgeMesh.Remeshing` / `AdaptiveRemeshing`) or any pipeline.
- Do not introduce a probabilistic-quadric solve path; `Geometry.Quadric`
  stays as-is and is not consumed by this task (see Context).

## Context

- `Geometry::Smoothing` (interface
  `src/geometry/Geometry.HalfedgeMesh.Smoothing.cppm`, implementation
  `src/geometry/Geometry.HalfedgeMesh.Smoothing.cpp`) currently offers only
  Uniform Laplacian, Cotan Laplacian, Taubin, and Implicit (backward-Euler)
  Laplacian. All operate directly on vertex positions and none preserve sharp
  features under noise.
- Face normals and per-vertex normals are computed by
  `Geometry::HalfedgeMesh::VertexNormals` (module
  `Geometry.HalfedgeMesh.Vertices.Normals`), which already establishes the
  area-weighted / angle-weighted averaging modes and the fail-closed
  diagnostics pattern (`RecomputeStatus`, degenerate/non-finite face counts)
  that this denoiser should mirror.
- Probabilistic-quadric building blocks exist in `Geometry.Quadric`
  (`src/geometry/Geometry.Quadric.cppm`) but are not wired into any denoiser
  and are deliberately left untouched here; the Sun et al. vertex update is a
  least-squares normal-projection integration, not a quadric solve.
- The denoiser is distinct from the point-cloud bilateral filter; this task
  introduces the mesh analogue (Fleishman et al. 2003 bilateral normal
  filtering + Sun et al. 2007 vertex update; cf. the `bcg_mesh_denoising`
  reference).
- Geometry layer rules apply: `src/geometry/*` may import `core` only and must
  not import assets/runtime/graphics/rhi/ecs/app. Algorithms must be
  deterministic, fail-closed on degenerate/empty/non-finite input with explicit
  diagnostics (no asserts, no NaNs), per GEOM-005 and GEOM-007.

## Slice plan

- [ ] Slice A — Stage 1 (face-normal bilateral filtering): add the filtered
      per-face normal field computation with spatial + range weights and its
      params/result structs, plus tests for clean-mesh near-identity and
      determinism.
- [ ] Slice B — Stage 2 (vertex update) + orchestrator: add the
      normal-projection vertex integration, the two-stage `DenoiseBilateral`
      entry point, noise-reduction and feature-preservation tests, and
      fail-closed coverage. Slice B closes the task at `CPUContracted`.

## Required changes

- [ ] In `src/geometry/Geometry.HalfedgeMesh.Smoothing.cppm`, extend namespace
      `Geometry::Smoothing` with the denoiser API (declarations + small structs
      only; non-trivial bodies go in the `.cpp`):
  - [ ] `struct BilateralDenoiseParams` with: `std::size_t NormalIterations{}`
        (Stage 1 face-normal smoothing iterations), `std::size_t
        VertexIterations{}` (Stage 2 position-update iterations), `double
        SigmaSpatial{}` (centroid-distance Gaussian; `<= 0` means
        auto-select from mean centroid distance of 1-ring face neighbors),
        `double SigmaRange{}` (normal-difference Gaussian; `<= 0` means
        auto-select), `bool PreserveBoundary{true}`, and a small finite-input
        epsilon field consistent with `VertexNormals::Params`.
  - [ ] `enum class DenoiseStatus : std::uint8_t { Success, EmptyMesh,
        NonManifoldInput, DegenerateGeometry, NonFiniteInput, InvalidParams }`
        for fail-closed reporting.
  - [ ] `struct BilateralDenoiseResult` carrying `DenoiseStatus Status`,
        `std::size_t NormalIterationsPerformed`, `std::size_t
        VertexIterationsPerformed`, `std::size_t VertexCount`, and degenerate /
        non-finite / skipped-face counts mirroring `VertexNormals::Result`.
  - [ ] `[[nodiscard]] BilateralDenoiseResult DenoiseBilateral(
        HalfedgeMesh::Mesh& mesh, const BilateralDenoiseParams& params);` — the
        two-stage orchestrator that runs Stage 1 then Stage 2 in place.
  - [ ] Doc comments stating the references (Fleishman et al. 2003; Sun et al.
        2007 / Ohtake normal-projection update) and the fail-closed contract.
- [ ] In `src/geometry/Geometry.HalfedgeMesh.Smoothing.cpp`, implement:
  - [ ] Stage 1 — face-normal bilateral filtering: for each non-deleted face,
        compute the centroid and area-aware face normal, then iteratively
        replace each face normal with the normalized weighted sum over
        adjacent faces (edge-adjacent 1-ring) using weight `w =
        exp(-||c_i - c_j||^2 / (2 SigmaSpatial^2)) *
        exp(-||n_i - n_j||^2 / (2 SigmaRange^2)) * area_j`. Filtering reads from
        the previous iteration's normals into a scratch buffer (double-buffered)
        so iterations are order-independent and deterministic.
  - [ ] Stage 2 — vertex position update (Sun et al. 2007): for `VertexIterations`,
        move each vertex `x_i ← x_i + (1/|F(i)|) Σ_{f ∈ F(i)} n_f (n_f · (c_f - x_i))`,
        where `n_f` are the filtered face normals and `c_f` the incident face
        centroids; pin boundary vertices when `PreserveBoundary` is set.
  - [ ] Reuse the existing face-normal/centroid computation conventions from
        `Geometry.HalfedgeMesh.Vertices.Normals` (import that module rather than
        re-deriving averaging policy) to keep degenerate handling consistent.
  - [ ] Fail-closed guards: empty mesh → `EmptyMesh`; any non-finite input
        position → `NonFiniteInput`; zero-area / degenerate faces counted and
        excluded from weighting (and if no usable faces remain →
        `DegenerateGeometry`); non-manifold edges (an edge with >2 incident
        faces) → `NonManifoldInput`; invalid params (negative iterations not
        possible with unsigned, but non-finite sigma) → `InvalidParams`. On any
        non-`Success` status the mesh is left unmodified.
- [ ] Register the denoiser source in the geometry module library build entry
      that already lists `Geometry.HalfedgeMesh.Smoothing.cpp` / `.cppm`
      (via `intrinsic_add_module_library` + the `FILE_SET CXX_MODULES`
      `target_sources` for the geometry target); no new module is created — the
      symbols extend the existing `Geometry.Smoothing` module.

## Tests

- [ ] In `tests/unit/geometry/Test_Smoothing.cpp`, add cases under the existing
      `unit;geometry` label (do not introduce a new CTest label):
  - [ ] Near-identity on a clean mesh: denoising an already-smooth/flat mesh
        moves every vertex by less than a tight tolerance (max displacement
        below a small epsilon relative to mean edge length).
  - [ ] Noise reduction: inject deterministic (fixed-seed) Gaussian noise into
        vertex positions of a known surface, run `DenoiseBilateral`, and assert
        the RMS distance to the clean surface is strictly reduced versus the
        noisy input.
  - [ ] Feature preservation vs. uniform Laplacian: on a mesh with a sharp
        feature edge (e.g. two planes meeting at a crease) plus injected noise,
        assert the bilateral denoiser preserves the dihedral angle / crease
        sharpness better (smaller error along the feature) than
        `UniformLaplacian` run to comparable smoothness.
  - [ ] Determinism: two runs with identical mesh and params produce
        bit-identical vertex positions and identical `BilateralDenoiseResult`
        counters.
  - [ ] Fail-closed: empty mesh → `EmptyMesh`; a mesh with a non-manifold edge
        → `NonManifoldInput`; a mesh containing a zero-area face →
        `DegenerateGeometry` (or excluded-and-counted per contract); non-finite
        injected position → `NonFiniteInput`; non-finite sigma → `InvalidParams`.
        In every failing case assert the mesh vertex positions are unchanged.

## Docs

- [ ] Update the module doc comment block in
      `src/geometry/Geometry.HalfedgeMesh.Smoothing.cppm` to describe the new
      denoiser, its two stages, parameters, and references.
- [ ] Regenerate `docs/api/generated/module_inventory.md` via
      `tools/repo/generate_module_inventory.py` so the new exported symbols on
      `Geometry.Smoothing` are reflected.
- [ ] If `tests/unit/geometry/README.md` enumerates covered behaviors, add the
      denoiser cases there (no new label introduced).

## Acceptance criteria

- [ ] `Geometry::Smoothing::DenoiseBilateral` exists, is exported from module
      `Geometry.Smoothing`, and runs the two stages in place, returning a
      `BilateralDenoiseResult` with a `DenoiseStatus`.
- [ ] On a clean mesh, max per-vertex displacement after denoising is below the
      documented near-identity tolerance.
- [ ] On a fixed-seed noisy mesh, RMS error to the clean surface is strictly
      smaller after denoising than before.
- [ ] On the crease-plus-noise mesh, the bilateral denoiser's feature error is
      strictly smaller than `UniformLaplacian`'s at comparable overall
      smoothness.
- [ ] Repeated runs are bit-identical (positions and result counters).
- [ ] Every degenerate/empty/non-manifold/non-finite/invalid-params case returns
      the specified non-`Success` status, emits no NaN/Inf, triggers no assert,
      and leaves the mesh unmodified.
- [ ] `src/geometry/*` still imports only `core`-level dependencies; no
      assets/runtime/graphics/rhi/ecs/app imports are added.
- [ ] All listed verification commands pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Smoothing|Denoise' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves with the semantic addition of the denoiser.
- Introducing unrelated feature work (no changes to existing smoothing
  operators, remeshing, simplification, or boolean code).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into the
  geometry layer; no `src/runtime/*` (Editor/Visualization/SpatialDebug) code.
- Adding a GPU/compute backend or any UI surface (UI-024 owns the editor window).
- Wiring `Geometry.Quadric` into the denoiser or altering it.
- Introducing a new CTest label without updating `tests/README.md` and
  `tests/CMakeLists.txt` in the same change.
- Claiming performance improvements without a baseline comparison.

## Maturity

- Stop-state pin: this task closes at **CPUContracted**. The denoiser is a
  deterministic, fail-closed CPU reference with full correctness tests
  (near-identity, noise reduction, feature preservation, determinism,
  degenerate handling) and synchronized docs. Optimized/parallel backends, GPU
  paths, benchmark harnesses, and editor/UI integration are explicitly out of
  scope and belong to later tasks.

- Closure: no `Operational` follow-up is owed for this task.
