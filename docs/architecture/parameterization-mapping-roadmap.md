# Parameterization and Mapping Roadmap

Status: roadmap / planning note for [`GEOM-011`](../../tasks/archive/GEOM-011-parameterization-mapping-roadmap.md).

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
- `Geometry.UvAtlas` provides the backend-neutral UV atlas seam with authored-UV preservation, source xrefs for seam splits, chart/seam-cut records, GEOM-018 quality diagnostics, a method selector for `FastStaged` versus `XAtlas`, and `FastStaged` as the default concrete CPU backend. `FastStaged` grows connected planar multi-face charts, attempts existing LSCM then harmonic/Tutte parameterization per chart where topology allows, records per-chart quality diagnostics, and shelf-packs finite non-overlapping UVs; the repository-pinned `jpcy/xatlas` overlay remains available through explicit `XAtlas` requests and as compatibility fallback when enabled.
- `Extrinsic.Runtime.AssetMeshNormals` consumes `Geometry.UvAtlas` from the runtime layer for imported renderable meshes, preserving valid authored UVs or generating atlas UVs before generated texture bakes. Geometry stays independent of assets, ECS, graphics, runtime, platform, and app layers.
- `Geometry.DEC` and the reusable `Geometry.Sparse` seam provide sparse matrix and conjugate-gradient infrastructure that future parameterization solvers can share.
- `Geometry.Linalg` provides dense decomposition, covariance, least-squares, and GLM/Eigen adapter utilities behind an explicit geometry-owned numerical module.
- `Geometry.HalfedgeMesh.Boundary`, `Geometry.HalfedgeMesh.Analysis`, `Geometry.HalfedgeMesh.Quality`, and mesh/soup conversion contracts provide topology and fixture utilities for disk topology, boundary loops, degenerate faces, and validation.
- `Geometry.HtexPatch` provides patch metadata and simple atlas layout helpers, but it is not a complete UV atlas segmentation or chart-packing system.
- `GEOM-005` through `GEOM-009` provide the style/numeric policy, mesh/soup conversion contracts, robust predicates, reusable numerical infrastructure, and benchmark manifest groundwork future packs should use.

The gaps below come from the [`src/geometry` gap analysis](../reviews/2026-05-12-src-geometry-gap-analysis.md), especially the parameterization/mapping, diagnostics, reproducibility, and benchmark sections.

## Family surface and shared optimization seam

The direct solver entry points (`ComputeLSCM`, `ComputeHarmonic`) remain
available with their per-method params structs. Retired GEOM-063 now also
consolidates the implemented CPU solvers behind one typed dispatch surface.
Each later method extends that variant with its concrete params type when it
lands; runtime config uses explicit stable tokens and converts them to those
typed payloads. Backend selection stays at the runtime/method boundary until a
second implementation exists.

- Family surface: retired [`GEOM-063`](../../tasks/done/GEOM-063-unified-cpu-parameterization-strategy-dispatch.md) — `Geometry.Parameterization::ParameterizeMesh(mesh, strategy)` with `ParameterizationStrategy = std::variant<ParameterizationParams, HarmonicParams>`, normalized status, and the GEOM-018 diagnostics in every successful result. Tutte is selected through `HarmonicParams::Weights = Uniform`. No unimplemented strategy or backend token is reserved; each method/backend owner extends the surface when its implementation lands.
- Shared optimization seam: [`GEOM-064`](../../tasks/backlog/geometry/GEOM-064-parameterization-optimization-kernels.md) — `Geometry.Parameterization.Optimize` with the per-triangle local rotation fit, the symmetric-Dirichlet energy/gradient plus PSD proxy, and the injectivity-preserving line search that ARAP (Pack 3), SLIM (Pack 4), and the optimized backend (Pack 7) share, so no variant re-derives the nonlinear-solve core privately.

The rendering/interaction decision for the interactive UV view is recorded in [ADR-0025](../adr/0025-parameterization-uv-view-and-split-view.md): the UV layout is a derived second view of the mesh entity (shared topology/`StableId`/`v:texcoord`), not a separate ECS entity.

## Pack 1 — Distortion and map-quality diagnostics

Follow-up task: [`GEOM-018`](../../tasks/archive/GEOM-018-parameterization-distortion-map-quality-diagnostics.md).

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

Retired implementation task: [`GEOM-019`](../../tasks/done/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md).

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

Follow-up task: [`METHOD-021`](../../tasks/backlog/methods/METHOD-021-arap-parameterization-reference-backend.md) (adds the `Arap` strategy to the family surface).

Scope:

- Add ARAP parameterization after Pack 1 diagnostics and Pack 2 boundary records exist.
- Define local/global iteration records, rotation-fit diagnostics, and convergence status.
- Paper: Liu, Zhang, Xu, Gotsman & Gortler, "A Local/Global Approach to Mesh Parameterization" (SGP 2008).

Primary home: `src/geometry` — the `Arap` strategy on the `Geometry.Parameterization` family surface (Pack "Family surface"), with the paper claim capture under `methods/geometry/arap_parameterization`.

Dependencies:

- Pack 1 distortion diagnostics; Pack 2 boundary constraints and initial (Tutte) embedding via the surface.
- The `GEOM-063` dispatch surface and the `GEOM-064` optimization kernels seam — the "future generic optimization framework" this pack originally contemplated, now factored out because ARAP, SLIM, and the optimized backend share it.
- `Geometry.Linalg` for per-triangle local rotations/polar decomposition (behind `GEOM-064`).

Correctness and benchmarks:

- Planar disk fixture where ARAP preserves a known low-distortion embedding.
- Deformed boundary fixture with decreasing energy across iterations.
- Degenerate triangle and singular local-step fixtures with explicit failure diagnostics.

## Pack 4 — SLIM and advanced distortion energies

Follow-up task: [`METHOD-022`](../../tasks/backlog/methods/METHOD-022-slim-injective-parameterization-reference-backend.md) (adds the flip-free `Slim` strategy).

Scope:

- Add SLIM-style locally-injective mapping and advanced distortion energies after the ARAP/local-global seam and diagnostics are in place.
- Treat SLIM paper-specific claims as method-workflow work with parity against the ARAP reference on developable input.
- Paper: Rabinovich, Poranne, Panozzo & Sorkine-Hornung, "Scalable Locally Injective Mappings" (TOG 2017).

Primary home: the `Slim` strategy on the `Geometry.Parameterization` family surface, with paper claim capture under `methods/geometry/slim_parameterization`. The reusable energy/proxy/line-search records live in the `GEOM-064` seam, shared with ARAP.

Dependencies:

- Pack 1 diagnostics; the `GEOM-063` surface; the `GEOM-064` optimization kernels (symmetric-Dirichlet energy/proxy + injective line search); an injective (Tutte/ARAP) start.
- Robust invalid/flipped element policy (SLIM guarantees zero flips from an injective start and reports `min_signed_area`).

Correctness and benchmarks:

- Injectivity/regression fixtures with known difficult boundaries.
- Energy monotonicity and line-search diagnostics where applicable.
- Backend parity reports before any optimized backend is added.

## Pack 4b — Spectral conformal parameterization (SCP)

Follow-up task: [`METHOD-024`](../../tasks/backlog/methods/METHOD-024-spectral-conformal-parameterization-reference-backend.md) (adds the pin-free conformal `Scp` strategy).

Scope:

- Add a pin-free conformal map recovered as the smallest non-trivial generalized eigenvector of the conformal-energy matrix against a boundary-area matrix — lower distortion than two-point-pinned LSCM.
- Paper: Mullen, Tong, Alliez & Desbrun, "Spectral Conformal Parameterization" (SGP 2008).

Primary home: the `Scp` strategy on the family surface, with paper claim capture under `methods/geometry/spectral_conformal`.

Dependencies:

- The `GEOM-063` surface; the [`GEOM-024`](../../tasks/backlog/geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md) generalized symmetric eigensolver seam (shared with `METHOD-006`); the DEC cotangent/area operators; boundary-loop helpers.

## Pack 4c — Boundary First Flattening (BFF) and interactive control

Follow-up task: [`METHOD-023`](../../tasks/backlog/methods/METHOD-023-boundary-first-flattening-reference-backend.md) (adds the controllable-conformal `Bff` strategy).

Scope:

- Add an interactive conformal flattening that lets the caller prescribe boundary data — target boundary lengths or target exterior angles (curvature) — with optional interior cone singularities, via the Dirichlet-to-Neumann boundary reduction. This is the state-of-the-art *controllable* conformal method and the basis for the interactive UV editing in the engine-integration pack.
- Paper: Sawhney & Crane, "Boundary First Flattening" (TOG 2018).

Primary home: the `Bff` strategy on the family surface, with paper claim capture under `methods/geometry/boundary_first_flattening`.

Dependencies:

- The `GEOM-063` surface; the DEC cotangent Laplacian; `Geometry.Sparse` LDLT for the interior solves; boundary-loop helpers. No eigensolver (that is Pack 4b).

## Pack 5 — Atlas segmentation, seam generation, and chart packing

Implementation tasks:

- [`GEOM-025`](../../tasks/archive/GEOM-025-uv-atlas-backend-xatlas.md) established the backend-neutral UV atlas contract and initial xatlas default.
- [`GEOM-057`](../../tasks/archive/GEOM-057-fast-uv-atlas-charting-and-packing.md) promotes the fast staged replacement path to the default while keeping xatlas as the visible compatibility fallback.

Scope:

- Add geometry-owned chart records, seam cuts, atlas segmentation, and CPU chart packing suitable for later renderer/material consumers without depending on those layers. Current promoted state is the `Geometry.UvAtlas` backend contract with `FastStaged` as the default concrete CPU backend, deterministic connected planar chart proposals, per-chart LSCM/harmonic attempts with projection fallback for unsupported topology, per-chart quality records, deterministic shelf packing, chart records, seam records, and explicit xatlas fallback diagnostics for failing fast backends when compatibility fallback is enabled.
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
- Fixtures proving the built-in fast staged backend emits finite non-overlapping UVs, per-chart quality diagnostics, chart records, seam cuts, and property xrefs, plus fallback fixtures proving `RequestedMethod`, `ActualMethod`, and `UsedFallback` distinguish xatlas compatibility from the fast staged method actually running. The smoke benchmark `geometry.uv_atlas.fast_staged_vs_xatlas.smoke` records fast-staged versus xatlas runtime and quality deltas on the deterministic cube-surface fixture without making an adoption claim. The promotion benchmark `geometry.uv_atlas.fast_staged_promotion.smoke` runs the multi-fixture PR-fast suite and records `promotion_pass`/`adoption_claim` for default adoption.

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

## Pack 7 — Engine integration and interactive UV view

Implementation tasks: [`RUNTIME-176`](../../tasks/backlog/runtime/RUNTIME-176-parameterization-runtime-config-integration.md), [`UI-036`](../../tasks/backlog/ui/UI-036-sandbox-parameterization-editor-and-uv-split-view.md), optional [`GRAPHICS-122`](../../tasks/backlog/rendering/GRAPHICS-122-uv-view-offscreen-render-target.md); decision record [ADR-0025](../adr/0025-parameterization-uv-view-and-split-view.md).

This pack is outside `src/geometry` — it wires the geometry family surface into the engine so the parameterization is choosable and controllable from a config file, an agent/CLI, and the UI as co-equal surfaces, and adds the interactive UV view. It follows the LOP-family (`RUNTIME-175`/`UI-035`) and progressive-Poisson (`RUNTIME-134`) integration precedents.

Scope:

- Runtime facade + config lane (`RUNTIME-176`): an `EngineConfig.sandbox.parameterization` section applied through `EngineConfigControl`, an editor command that writes UVs back as `v:texcoord` via `GeometrySources`, a `Runtime.ParameterizationBackend` RHI fallback adapter, and a pointer-free UV view model.
- Sandbox editor panel + resizable UV split view (`UI-036`): a two-pane controls/UV-layout split (manual splitter, no docking dependency), the 2D UV layout drawn with `ImDrawList` from the view model, a distortion-heatmap overlay, and interactive pin/BFF-boundary/cone control routed through the config lane.
- Optional GPU-shaded UV target (`GRAPHICS-122`): an offscreen UV render presented via `ImGui::Image` for texel-density/texture/heatmap shading and dense meshes, with a CPU-layout fallback.

Rendering-model decision (ADR-0025): the UV layout is a **derived second view of the mesh entity** (shared topology/`StableId`/`v:texcoord`), not a separate ECS entity, matching how Blender/Houdini/Maya/RizomUV present a UV editor as a second view of the same mesh in UV space. A separate UV entity and a true second viewport are considered and deferred/rejected there.

Layer boundary: geometry stays free of renderer/runtime/assets; `RUNTIME-176` owns composition; `UI-036` is `app -> runtime` only; `GRAPHICS-122` keeps the UV pass in graphics wired by runtime.

## Cross-pack correctness policy

Parameterization and mapping tasks must make topology, boundary, and degeneracy behavior explicit:

- Solvers report unsupported topology instead of projecting arbitrary meshes into UV space.
- Boundary-condition records are explicit and deterministic.
- Degenerate 3D triangles, degenerate UV triangles, flipped elements, singular systems, non-convergence, and invalid caller-provided pins are all reported through diagnostics.
- Public APIs keep GLM and geometry-owned records as the storage vocabulary; Eigen remains behind `Geometry.Linalg` / implementation units.
- Benchmark smoke fixtures are small, CPU-only, deterministic in numerical metrics, and free of renderer/material/asset dependencies.

## Initial priority

The diagnostics ([`GEOM-018`](../../tasks/archive/GEOM-018-parameterization-distortion-map-quality-diagnostics.md), retired) and harmonic/Tutte ([`GEOM-019`](../../tasks/done/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md), retired) foundations exist, so the SOTA-variant program orders as:

1. Retired [`GEOM-063`](../../tasks/done/GEOM-063-unified-cpu-parameterization-strategy-dispatch.md) — the typed CPU family dispatch surface, consolidating existing Harmonic/Tutte and LSCM so later implemented variants can extend one API. Behavior-preserving; no new algorithm or speculative backend token.
2. [`GEOM-064`](../../tasks/backlog/geometry/GEOM-064-parameterization-optimization-kernels.md) — the shared optimization-kernel seam ARAP and SLIM consume.
3. The SOTA reference variants on the surface: ARAP (`METHOD-021`, Pack 3), SLIM (`METHOD-022`, Pack 4), SCP (`METHOD-024`, Pack 4b), BFF (`METHOD-023`, Pack 4c) — CPU-reference-first, in each task's `depends_on` order.
4. The optimized CPU (`METHOD-025`) and GPU (`METHOD-026`) backends for the iterative strategies after reference parity; the linear one-shot strategies (LSCM/SCP/BFF) record no optimized/GPU follow-up in their tasks. `METHOD-026` also wires the runtime GPU job-queue leg, so it is additionally gated on `RUNTIME-176`.
5. Engine integration and the interactive UV view (Pack 7: `RUNTIME-176`, `UI-036`, optional `GRAPHICS-122`).

Later packs should not begin until their prerequisites are retired to `tasks/done/` or recorded as explicit out-of-scope assumptions in the candidate task file.
