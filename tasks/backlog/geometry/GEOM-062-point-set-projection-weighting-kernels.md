---
id: GEOM-062
theme: I
depends_on: []
maturity_target: CPUContracted
---
# GEOM-062 — Point-set projection and weighting kernels seam

## Goal
- Add a reusable geometry-owned kernel seam — radial weight functions, the LOP repulsion function, and local density-weight estimation — so the Locally Optimal Projection family (LOP/WLOP/CLOP/EAR) shares one tested weighting core instead of each variant re-deriving the same Gaussian/theta math privately.

## Non-goals
- No projection/consolidation control flow here — the fixed-point projection loop stays in `Geometry.PointCloud.Consolidation` (`METHOD-016`); this task ships only the stateless kernel primitives it consumes.
- No opportunistic refactor of the bilateral-filter/KDE Gaussians that are currently inlined privately in `Geometry.PointCloud.Utils.cpp`; migrating those onto this seam is a named future consumer, not part of this change (`docs/architecture/geometry-api-style.md` forbids bundling that refactor).
- No GPU or optimized backend; deterministic CPU numerics only.
- No Eigen types on the module surface.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Today there is **no** exported weight/kernel/repulsion/density seam. The only Gaussian weights are private inline helpers duplicated inside `Geometry.PointCloud.Utils.cpp` (bilateral filter `exp(dist²·invSpatial2)`, KDE `normFactor·exp(dist²·invH2)`), confirmed by a `src/geometry` port-gap survey (2026-07-13).
- Port reference: the density-weighting and repulsion terms from framework24 `lib_bcg_framework/include/bcg_locally_optimal_projection.h` (untested in bcg); this task lifts only the stateless weight math with explicit tests, not the projection driver.
- Present consumers that justify the seam (P1 second-caller rule): WLOP/LOP (`METHOD-016`), CLOP (`METHOD-017`), and EAR/anisotropic (`METHOD-018`) all weight neighbor contributions with the same fast-decaying radial kernel `θ(r) = exp(-r²/(h/4)²)` and a repulsion balancing term. `Geometry.PointCloud.Utils` bilateral/KDE are named opportunistic future consumers.
- Reuses `Geometry.KDTree` radius/KNN queries for neighborhood assembly and `Geometry.PointCloud.Utils::ComputeStatistics` (`AverageSpacing`, `BoundingBoxDiagonal`) as the scale reference for a default support radius `h`. Weighted-covariance/eigendecomposition needs of anisotropic kernels are already served by `Geometry::PCA::SymmetricEigen3` — no new linear-algebra module.

## Required changes
- [ ] Add module `Geometry.PointCloud.Kernels` (`.cppm` interface + `.cpp` implementation unit) in namespace `Geometry::PointCloud::Kernels`, exposing only `std`/`glm`/scalar types.
- [ ] Radial weight functions with a selectable `KernelType { Gaussian, ThetaLop, WendlandC2 }`: a support-radius-parameterized weight `Weight(distanceSquared, h, kernel)` where `ThetaLop` is the LOP/WLOP fast-decaying `exp(-r²/(h/4)²)`; document each closed form and its scale convention.
- [ ] LOP repulsion balancing function `Repulsion(r, h)` and its derivative used by the repulsion term, with the degenerate `r → 0` limit handled (finite, documented) rather than dividing by zero.
- [ ] Local density-weight estimation `ComputeDensityWeights(points, h, kernel, neighborhood)` returning the per-point WLOP weight `1 + Σ_{i'≠i} θ(‖p_i − p_i'‖)` (and the reciprocal projected-set form), deterministic in output order, using a supplied or internally built `Geometry.KDTree`.
- [ ] Compute weights in `double` internally; expose `float`/scalar results. Fail-closed on non-positive `h`, non-finite inputs, and empty neighborhoods (explicit status/`std::optional`, never NaN/Inf escape).
- [ ] Register `Geometry.PointCloud.Kernels.cppm` / `.cpp` in the single `IntrinsicGeometry` module-library target lists in `src/geometry/CMakeLists.txt` (alphabetical placement; no new target or link dependency — `glm`/`Eigen3` are already linked).

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudKernels.cpp` with `unit;geometry` labels.
- [ ] Analytic weight values: each `KernelType` matches its closed form at known radii (0, `h/4`, `h`, `>h`), is monotonically non-increasing in `r`, and reaches ~0 beyond the support radius.
- [ ] Repulsion: `Repulsion`/derivative match hand-computed values at sample radii and stay finite at `r → 0`.
- [ ] Density weights: on a crafted non-uniform fixture (dense cluster + sparse tail) dense points receive strictly larger weights than sparse points; a uniform grid yields near-constant weights.
- [ ] Determinism: identical `(points, h, kernel)` produce bitwise-identical weights across two runs and across thread counts.
- [ ] Fail-closed: non-positive `h`, non-finite input, and empty neighborhoods return explicit failure states with no NaN/Inf.

## Docs
- [ ] Interface documentation per `docs/architecture/geometry-api-style.md`, including each kernel's closed form, the `h` scale convention, and the failure-state contract.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.
- [ ] Update the port-gap cluster notes in `tasks/backlog/geometry/README.md` and record `METHOD-016`/`017`/`018` as the consumers.

## Acceptance criteria
- [ ] Public surface exposes only `std`/`glm`/scalar types (no Eigen).
- [ ] All listed tests pass in the default CPU gate.
- [ ] `METHOD-016` can express its attraction/repulsion/density weighting against this surface without private weight math.
- [ ] Layering check passes (`geometry -> core` only).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudKernels|Kernels' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No projection/consolidation logic in this task.
- No refactor of existing bilateral/KDE inline weights (named follow-up only).
- No `std::rand` or global RNG state.
- No public Eigen types on the module interface.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed — this is a pure CPU numerics seam. GPU weight evaluation, if ever needed, opens with the family GPU backend (`METHOD-020`).
