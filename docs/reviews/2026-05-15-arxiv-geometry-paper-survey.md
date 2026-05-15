# Arxiv Geometry-Processing Paper Survey (2026-05-15)

## Scope

This is a research note, not an implementation plan. It surveys recent arxiv / venue papers in geometry processing and ranks candidates for IntrinsicEngine integration against the gaps documented in `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` and the existing capability inventory under `src/geometry/`.

Selection criteria:

- Plugs a P0/P1 gap identified in the geometry gap analysis.
- Has a tractable CPU reference implementation (method-workflow step 2 before any optimized or GPU backend).
- Composes with existing modules (halfedge mesh, point cloud, DEC, heat method, BVH/KD-tree).
- Avoids pulling in heavy ML stacks as a hard dependency for the reference backend.

Papers are grouped by gap and ordered within each group by expected leverage for the engine.

## Tier 1 — High leverage, strong fit, clear CPU reference

### 1. Signed Heat Method — "A Heat Method for Generalized Signed Distance" (Feng & Crane)

- **Why it fits:** Direct extension of the scalar heat method and vector heat method already in `Geometry.HalfedgeMesh.Geodesic` and `Geometry.HalfedgeMesh.VectorHeatMethod`. Solves signed geodesic distance to broken / noisy curve and surface inputs in R^n and on surfaces, reusing the same Laplacian/cotan/mass machinery that the engine already builds for DEC.
- **Gap addressed:** Geodesics and intrinsic geometry pack; SDF utilities; tolerant input for noisy data (P0/P1).
- **Engine notes:** Reference backend would live next to the existing heat-method modules; new public API would mirror `VectorHeatMethod`. No new dependency beyond the sparse solver seam already on the `GEOM-008` roadmap.
- **Reference:** [Project page](https://nzfeng.github.io/research/SignedHeatMethod/SignedDistance.pdf), reference implementation in geometry-central's `signed_heat_method` module.

### 2. Closest Point Method for PDEs on Manifolds with Interior Boundary Conditions (King et al., TOG 2024)

- **Why it fits:** Provides a general embedding-based PDE solver that handles diffusion curves, geodesic distance, harmonic maps, tangent-vector design, and reaction-diffusion textures — all with the same closest-point query that `Geometry.KDTree` / `Geometry.BVH` already support. Complements DEC by enabling PDEs on surfaces given only as closest-point or point-cloud data, which the halfedge-only path cannot do today.
- **Gap addressed:** Geometry-processing PDEs and fields (P1); boundary-condition data structures; symmetric domain views across mesh/point-cloud/implicit-surface inputs (P0).
- **Engine notes:** Implementable as a CPU module over the existing `Geometry.Grid` plus the sparse solver seam from `GEOM-008`. Public surface is small: `(ClosestPoint query, BC list, RHS) -> field on narrow band`.
- **Reference:** [arXiv:2305.04711](https://arxiv.org/abs/2305.04711).

### 3. Walk on Spheres / Walk on Stars family (Sawhney, Miller, Crane, and follow-ups)

- **Why it fits:** Grid-free, discretization-free Monte Carlo solver for Laplace/Poisson/elliptic problems on volumetric and surface domains. Two reasons it is unusually well-aligned with the engine:
  - It needs only `(closest point, signed distance, ray)` queries — every one of those already exists in `Geometry.SDF`, `Geometry.KDTree`, and `Geometry.Raycast`.
  - It is embarrassingly parallel and a natural future GPU backend, satisfying the method-workflow staging (CPU reference now, GPU later).
- **Gap addressed:** PDE pack (P1); stochastic reproducibility state (P2); benchmark manifests (a Monte Carlo solver makes baselining variance / convergence concrete).
- **Recent variance-reduction / extensions worth tracking after the baseline WoS lands:**
  - [Projected Walk on Spheres for Surface PDEs (arXiv:2410.03844)](https://arxiv.org/abs/2410.03844) — generalises WoS to surface PDEs using only closest-point + normal queries; pairs naturally with item 2.
  - [Differential Walk on Spheres (arXiv:2405.12964)](https://arxiv.org/abs/2405.12964) — derivatives w.r.t. problem parameters, useful for inverse problems / shape optimisation.
  - [Path Guiding for Monte Carlo PDE Solvers (arXiv:2410.18944)](https://arxiv.org/abs/2410.18944) — variance reduction.
  - [Off-Centered WoS-Type Solvers with Statistical Weighting (arXiv:2510.25152)](https://arxiv.org/abs/2510.25152) — recent estimator improvement.
- **Engine notes:** Start with the baseline WoS / WoSt reference paper; treat the variance-reduction follow-ups as method variants to be added after parity. Determinism story: explicit seeded RNG, which the gap analysis already calls out as missing.

### 4. Constrained Delaunay Tetrahedrization (Diazzi, Attene, et al., arXiv:2309.09805)

- **Why it fits:** The geometry gap analysis flags volumetric / cell-complex containers and tetrahedralization as a P1 gap. This paper gives a numerically robust CDT achieving 100% success on the 4408 valid Thingi10k models — i.e., a credible reference backend that does not require pulling in TetGen's licence or fTetWild's full pipeline.
- **Gap addressed:** Tetrahedral mesh container + builder (P1); FEM helper structures (P1); robust predicates path (P0, dovetails with `GEOM-007`).
- **Engine notes:** Should land after `GEOM-007` (robust predicates / intersection classification) since that work is a prerequisite. Suggest a `Geometry.TetMesh` module exporting only the container + builder first, with quality metrics and boundary extraction as separate follow-ups.
- **Reference:** [arXiv:2309.09805](https://arxiv.org/abs/2309.09805).

## Tier 2 — Strong fit, larger surface or more dependencies

### 5. Interactive and Robust Mesh Booleans (Cherchi, Livesu, Scateni, Attene — arXiv:2205.14151)

- **Why it fits:** The current `Geometry.HalfedgeMesh.Boolean` is the obvious candidate for a robustness overhaul (the gap analysis flags "robust mesh boolean kernel with arrangement provenance and attribute transfer" explicitly). This paper is now mature, has a public reference implementation, and produces robustness guarantees at interactive rates on ~200K-triangle inputs.
- **Gap addressed:** Robust mesh topology operations (P0–P1); arrangement / BSP data structure (P2); attribute transfer.
- **Engine notes:** Hard prerequisite is `GEOM-007` (robust predicates / exact kernels). Worth scoping as its own method package under `methods/geometry/` rather than an in-place rewrite of the existing boolean module, so the old boolean stays as a comparison baseline during parity.
- **Reference:** [arXiv:2205.14151](https://arxiv.org/abs/2205.14151).

### 6. NeurCross / CrossGen — cross fields for quad meshing

- **Why it fits:** Cross / frame fields are explicitly called out as a P1 missing capability (vector-field design, cross fields, singularity indexing). Both papers learn cross fields jointly with an SDF or in a latent space; CrossGen ([arXiv:2506.07020](https://arxiv.org/abs/2506.07020)) is the more recent and faster of the pair (≤1 s per shape, no per-shape optimisation). NeurCross ([arXiv:2405.13745](https://arxiv.org/abs/2405.13745)) is interesting because its cross-field optimisation is principal-curvature-aligned, which is independently useful even without quad meshing.
- **Gap addressed:** Vector-field / cross-field design (P1); field-guided remeshing (P1).
- **Engine notes:** These are ML-backed. The method-workflow contract requires a CPU reference first. Recommendation: implement a *non-neural* cross-field design baseline (e.g., MIQ / period-jump-based) as the reference, then add the neural variant as an optimised backend behind a capability flag. Both neural papers can serve as comparison references in the benchmark manifest rather than as the default backend.

### 7. Power Diagram Enhanced Adaptive Isosurface Extraction (arXiv:2506.09579) + Neural / Self-Supervised Dual Contouring lineage

- **Why it fits:** The engine already has `Geometry.MarchingCubes` and `Geometry.SDF` but no dual contouring or feature-preserving isosurface extraction; the gap analysis lists "marching tetrahedra / dual contouring" as P1. Power-diagram-enhanced extraction gives feature preservation without requiring a neural network, and pairs naturally with the existing implicit-surface stack.
- **Gap addressed:** Volumetric / implicit surface extraction (P1); dual contouring; sharp feature preservation in isosurfacing.
- **Engine notes:** Implementable on top of existing `Geometry.Grid` + `Geometry.SDF`. The neural follow-ups ([Neural Dual Contouring, arXiv:2202.01999](https://arxiv.org/abs/2202.01999); [Self-Supervised Dual Contouring, arXiv:2405.18131](https://arxiv.org/abs/2405.18131); [TetraSDF, arXiv:2511.16273](https://arxiv.org/abs/2511.16273)) are interesting but should not be the reference backend.
- **Reference:** [arXiv:2506.09579](https://arxiv.org/abs/2506.09579).

### 8. Feature-Aware Quadric Error Metric mesh simplification (FA-QEM, arXiv:2605.14029)

- **Why it fits:** The existing `Geometry.HalfedgeMesh.Simplification` module is the natural drop-in site. FA-QEM extends classical QEM with boundary curvature and normal-consistency terms, preserving sharp features under aggressive decimation — directly relevant to LOD / asset pipelines that the engine already cares about.
- **Gap addressed:** Mesh simplification quality (existing module enhancement); progressive mesh / simplification history records (P1).
- **Engine notes:** A small, contained improvement to an existing module; good "easy win" candidate. Public API stays compatible; new terms are extra weights on the existing quadric.
- **Reference:** [arXiv:2605.14029](https://arxiv.org/abs/2605.14029).

## Tier 3 — Worth tracking, defer

These were considered and intentionally deprioritised:

- **Neural Poisson Surface Reconstruction (arXiv:2308.01766) and related learned reconstructors.** The engine already has Hoppe-style and Poisson-adjacent reconstruction in `Geometry.SurfaceReconstruction`. A *screened Poisson* CPU reference (Kazhdan & Hoppe 2013) is a better next step than a learned model and is listed as a gap candidate in `GEOM-010`.
- **Mesh-generation foundation models (ARMesh, mesh transformers).** Out of scope: these are generative ML systems, not reusable geometry-processing kernels, and they violate the dependency posture for `src/geometry`.
- **Mesh Denoising Transformer / DoubleDiffusion.** ML-heavy; the engine should first add a classical bilateral / anisotropic-normal-filtering denoiser baseline alongside the existing `Geometry.HalfedgeMesh.Smoothing` before considering learned variants.
- **Restricted Delaunay surface reconstruction.** Strong method but overlaps significantly with the planned screened-Poisson + ball-pivoting roadmap in `GEOM-010`; revisit when the point-cloud pack lands.

## Suggested next-step tasks (proposals — not created here)

Each of these would live under `tasks/backlog/geometry/` or `tasks/backlog/methods/` per the task-format contract.

1. `METHOD-GEO-Signed-Heat` — Signed Heat Method reference backend on the halfedge mesh, reusing the existing heat-method Laplacian and sparse-solver seam from `GEOM-008`.
2. `METHOD-GEO-Closest-Point-PDE` — Closest-point-method PDE solver on `Geometry.Grid`, sharing the `ClosestPoint` query interface used by SDF/KD-tree/BVH.
3. `METHOD-GEO-Walk-on-Spheres` — Baseline WoS / WoSt CPU reference solver for Laplace/Poisson on SDF and mesh inputs, with deterministic seeded RNG; opens the door to a GPU backend.
4. `METHOD-GEO-Robust-Boolean` — Robust mesh boolean kernel based on Cherchi et al., gated on `GEOM-007`, packaged under `methods/geometry/` for parity comparison.
5. `GEOM-013` — Cross-field / frame-field design reference module (MIQ-style baseline), with NeurCross / CrossGen as optimised-backend comparison points.
6. `GEOM-014` — Dual contouring with sharp-feature preservation (power-diagram or QEF-based), targeting the existing `Geometry.MarchingCubes` / `Geometry.SDF` neighbourhood.
7. `GEOM-015` — FA-QEM extension to `Geometry.HalfedgeMesh.Simplification` (in-place, behind an opt-in flag).
8. `GEOM-016` — Constrained Delaunay tetrahedralisation reference + `Geometry.TetMesh` container; depends on `GEOM-007`.

Recommended ordering: Tier 1 items 1-3 unblock the PDE / geodesic surface; Tier 2 item 5 (robust booleans) and Tier 1 item 4 (CDT) both depend on `GEOM-007` and should follow it.

## Sources

- [Signed Heat Method — Feng & Crane](https://nzfeng.github.io/research/SignedHeatMethod/SignedDistance.pdf)
- [A Closest Point Method for PDEs on Manifolds with Interior Boundary Conditions (arXiv:2305.04711)](https://arxiv.org/abs/2305.04711)
- [Projected Walk on Spheres (arXiv:2410.03844)](https://arxiv.org/abs/2410.03844)
- [Differential Walk on Spheres (arXiv:2405.12964)](https://arxiv.org/abs/2405.12964)
- [Path Guiding for Monte Carlo PDE Solvers (arXiv:2410.18944)](https://arxiv.org/abs/2410.18944)
- [Off-Centered WoS-Type Solvers (arXiv:2510.25152)](https://arxiv.org/abs/2510.25152)
- [Constrained Delaunay Tetrahedrization (arXiv:2309.09805)](https://arxiv.org/abs/2309.09805)
- [Interactive and Robust Mesh Booleans (arXiv:2205.14151)](https://arxiv.org/abs/2205.14151)
- [NeurCross (arXiv:2405.13745)](https://arxiv.org/abs/2405.13745)
- [CrossGen (arXiv:2506.07020)](https://arxiv.org/abs/2506.07020)
- [Power Diagram Enhanced Adaptive Isosurface Extraction (arXiv:2506.09579)](https://arxiv.org/abs/2506.09579)
- [Neural Dual Contouring (arXiv:2202.01999)](https://arxiv.org/abs/2202.01999)
- [TetraSDF (arXiv:2511.16273)](https://arxiv.org/abs/2511.16273)
- [Fast and Robust Mesh Simplification / FA-QEM (arXiv:2605.14029)](https://arxiv.org/abs/2605.14029)
- [Neural Poisson Surface Reconstruction (arXiv:2308.01766)](https://arxiv.org/abs/2308.01766)
- [TEASER (arXiv:2001.07715)](https://arxiv.org/abs/2001.07715)

## Validation notes

- Documentation-only review; no C++ behavior changed.
- Cross-checked against `src/geometry/CMakeLists.txt` module inventory and `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` to align recommendations with documented gaps and roadmap phases.
- Run `python3 tools/docs/check_doc_links.py --root .` before promoting any of the proposed tasks into `tasks/backlog/`.
