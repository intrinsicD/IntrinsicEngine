# Methods Backlog

Paper/method packages following
[`docs/agent/method-workflow.md`](../../../docs/agent/method-workflow.md):
paper intake → CPU reference → correctness tests → benchmark harness →
optimized CPU → GPU only after reference parity.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Program tasks

- No open program tasks. Retired `METHODS-001` pins METHOD-002 as the
  methods-pipeline pathfinder.

## Tasks

- [METHOD-003 — Closest Point Method PDE solver reference backend](METHOD-003-closest-point-method-pde-reference-backend.md)
  (interior-boundary CPM selected; unblocked by retired `geometry/GEOM-023`
  and expected to use `Geometry.Sparse::SparseBiCGSTAB` for its non-symmetric
  solve).
- [METHOD-004 — Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md)
  (mixed-boundary Walk on Stars selected; no solver gate — promotable now).
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
  (Cherchi et al. formulation selected; `GEOM-007` gate satisfied 2026-05-27
  for filtered-sign/diagnostic vocabulary; this method owns its
  formula-specific exact/indirect escalation).
- [METHOD-006 — Surface cross-field design CPU reference backend](METHOD-006-cross-field-design-reference-backend.md)
  (globally optimal N-RoSy formulation selected; gated on the
  `geometry/GEOM-024` eigensolver seam).
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  (Diazzi et al. formulation selected; `GEOM-007` gate satisfied 2026-05-27
  for filtered orientation/diagnostics; this method owns LNC indirect
  `orient3d`/`inSphere`).
- [METHOD-014 — Progressive Poisson GPU operational parity](METHOD-014-progressive-poisson-gpu-operational-parity.md)
  (Vulkan compute operational/parity follow-up to retired `METHOD-013`; no
  speedup claim without benchmark baseline comparison).
- [METHOD-015 — Coherent Point Drift registration family reference backend](METHOD-015-coherent-point-drift-family-reference-backend.md)
  (rigid default with affine/nonrigid in-package; gated on
  `geometry/GEOM-058` Gaussian-mixture/EM seam).
- [METHOD-016 — Locally Optimal Projection (LOP/WLOP) consolidation reference backend](METHOD-016-locally-optimal-projection-reference-backend.md)
  (WLOP default; establishes the shared typed
  `Geometry.PointCloud.Consolidation` strategy surface; the first backend
  selector belongs to `METHOD-019`; gated on `geometry/GEOM-062` kernels).
- [METHOD-017 — Continuous LOP (CLOP) reference backend](METHOD-017-continuous-lop-clop-reference-backend.md)
  (adds the `Clop` strategy; gated on `METHOD-016`, `geometry/GEOM-058`
  Gaussian-mixture seam, and `geometry/GEOM-062` kernels).
- [METHOD-018 — Edge-Aware Resampling (EAR) and anisotropic feature-preserving LOP reference backend](METHOD-018-edge-aware-resampling-anisotropic-lop-reference-backend.md)
  (adds the `Ear`/anisotropic strategies; gated on `METHOD-016` and
  `geometry/GEOM-062`; consumes `Geometry.PointCloud.Normals`).
- [METHOD-019 — LOP-family optimized CPU backend and comparison benchmark](METHOD-019-lop-family-optimized-cpu-backend.md)
  (adds the first concrete backend selector plus `cpu_optimized` with paired
  parity/baseline evidence; gated on `METHOD-016`/`017`/`018` and the delivered
  `RUNTIME-175`/`UI-035` CPU control surfaces; deliberately excludes
  opportunistic `GEOM-060`/`061` adoption).
- [METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity](METHOD-020-lop-family-gpu-vulkan-compute-backend.md)
  (adds `gpu_vulkan_compute` with `gpu;vulkan` parity; gated on `METHOD-019`).
- [METHOD-021 — ARAP (local/global) parameterization reference backend](METHOD-021-arap-parameterization-reference-backend.md)
  (adds its concrete ARAP params alternative to the retired `GEOM-063` typed
  `Geometry.Parameterization` strategy surface; gated on `geometry/GEOM-064`
  optimization kernels; roadmap Pack 3).
- [METHOD-022 — SLIM locally-injective parameterization reference backend](METHOD-022-slim-injective-parameterization-reference-backend.md)
  (adds its concrete flip-free SLIM params alternative; gated on `GEOM-064`
  and `METHOD-021`; roadmap Pack 4).
- [METHOD-024 — Spectral Conformal Parameterization (SCP) reference backend](METHOD-024-spectral-conformal-parameterization-reference-backend.md)
  (adds its concrete pin-free conformal SCP params alternative; gated on the
  `geometry/GEOM-024` generalized eigensolver seam; new SOTA pack).
- [METHOD-025 — Progressive SLIM optimized CPU backend and comparison benchmark](METHOD-025-parameterization-family-optimized-cpu-backend.md)
  (evaluates a SLIM-only `cpu_optimized` Progressive Parameterizations path
  against `METHOD-022`; ARAP deliberately remains reference-only).
- [METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity](METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md)
  (adds `gpu_vulkan_compute` for the iterative ARAP/SLIM strategies with
  `gpu;vulkan` parity; gated on the SLIM-only `METHOD-025` evidence pass and
  builds on retired
  `RUNTIME-176`'s delivered facade/config/result model, extending it with the
  GPU job-queue seam and extending the panel delivered by retired `UI-036`;
  the linear one-shot strategies
  (LSCM/SCP/BFF) record no GPU follow-up in their own tasks).
- [METHOD-027 — Adaptive Delaunay/QEF implicit meshing](METHOD-027-adaptive-delaunay-qef-implicit-meshing.md)
  (post-stability, opt-in method; paper intake and a frozen method contract
  precede a 2D adversarial killing test and any 3D CPU reference; gated by
  `REVIEW-003`, `GEOM-013`, and `METHOD-007`).
- [METHOD-028 — Confidence-driven Walk on Stars guiding](METHOD-028-confidence-driven-walk-on-stars-guiding.md)
  (post-stability CPU reference and equal-memory variance study; gated by
  `REVIEW-003` and the base `METHOD-004` WoSt reference).
- [METHOD-029 — Discontinuity-aware material derivatives](METHOD-029-discontinuity-aware-material-derivatives-and-jacobian-portability.md)
  (post-stability custom-derivative/optimization evidence on a fixed
  method-local expression corpus; gated by `REVIEW-003` and the Slang pilot,
  with no MaterialX or production material-graph surface).
- [METHOD-030 — Neural Render Proxy path-replay reference](METHOD-030-neural-render-proxy-path-replay-reference.md)
  (post-stability intake, deterministic path replay, correctness tests, and a
  validated reference benchmark first; a small CPU proxy may start only after
  that checkpoint, with no renderer integration).
- [METHOD-031 — Cross-artifact Jacobian portability prediction](METHOD-031-jacobian-portability-predictive-study.md)
  (conditional post-stability study over the exact retained `METHOD-029`
  analytic-C++/Slang-SPIR-V corpus; held-out predictive evidence only, with no
  compiler, material, or renderer framework).
- [METHOD-032 — Octree parity normal orientation reference backend](METHOD-032-octree-parity-normal-orientation.md)
  (in-house formulation; prior-art/formulation intake and a frozen held-out
  killing test precede any public CPU-reference surface).
- [METHOD-033 — Screened Poisson surface reconstruction reference backend](METHOD-033-screened-poisson-reconstruction-reference.md)
  (uniform-grid CPU reference and watertight reconstruction oracle; supplies
  the inner reconstruction solver required by `METHOD-034`).
- [METHOD-034 — iPSR normal orientation baseline](METHOD-034-ipsr-orientation-baseline.md)
  (package-local deterministic CPU comparison baseline; gated on the
  screened-Poisson reference in `METHOD-033`).
- [METHOD-035 — Parametric Gauss normal orientation baseline](METHOD-035-pgr-winding-number-orientation-baseline.md)
  (package-local dense matrix-free CPU comparison baseline with an explicit
  smoke-scale resource guard).
- [METHOD-036 — Normal-orientation comparison evidence](METHOD-036-orientation-comparison-evidence.md)
  (shared-fixture publication protocol comparing octree parity, MST, iPSR,
  and PGR; implementation changes remain in their owning method tasks).
- [HARDEN-084 — Localized CPU/GPU parity signatures](HARDEN-084-localized-cpu-gpu-parity-signatures.md)
  (post-stability two-consumer evidence task, gated by the Progressive Poisson
  and parameterization GPU paths; no generic parity framework unless both
  local prototypes meet the frozen localization and overhead gates).

## Convergence

- `METHOD-023` is retired; its authoritative task record is under
  `tasks/done/METHOD-023-boundary-first-flattening-reference-backend.md`.
  Its paper audit bounded the reference to disk BFF with automatic, approximate
  target-length, and exterior-angle modes; cone BFF requires a future
  seam-aware cut/result contract and is not approximated on the current
  one-UV-per-vertex surface.

- METHOD-001 contributes to **Theme C — Physics readiness** and is retired at
  `CPUContracted`. Runtime/ECS integration and any performance backend remain
  out of scope for the method package and are owned by physics/runtime bridge
  follow-ups.
- METHOD-009 through METHOD-011 are physics-roadmap follow-ups from
  `ARCH-002`. Promote them
  CPU-reference-first in numeric order unless a task records a stronger local
  dependency; do not open optimized CPU or GPU backends until the selected
  reference backend is implemented, tested, benchmark-manifested, and documented.
- METHOD-002 through METHOD-007 are seeded by the geometry paper survey
  [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md)
  and target gaps from
  [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
  Each open task now names one selected public method contract and treats
  alternatives explicitly as internal comparisons, prior art, or separately
  scoped follow-ups. Changing that selection re-opens its intake, dependency,
  and evidence gates.
- METHOD-015 and METHOD-016 are seeded by the 2026-07-07 framework24
  (`bcg_framework`) port-gap comparison (see the geometry backlog README's
  port-gap section). METHOD-015's E-step numerics are owned by
  `geometry/GEOM-058` (encoded in `depends_on`), and its optimized nonrigid
  fast path is deferred to the `geometry/GEOM-060` permutohedral seam.
- **LOP consolidation family (METHOD-016..020).** WLOP/LOP (`METHOD-016`),
  CLOP (`METHOD-017`), and EAR/anisotropic (`METHOD-018`) share one
  `Geometry.PointCloud.Consolidation` module and typed `Strategy` surface, so
  every implemented variant is choosable through one surface. The first
  backend selector lands only with the real `cpu_optimized` implementation in
  `METHOD-019`; the GPU adapter follows in `METHOD-020`. The shared weight
  math is factored into the `geometry/GEOM-062` `Geometry.PointCloud.Kernels`
  seam; optimized CPU/GPU work follows reference parity; and the engine
  integration — config lane, runtime facade,
  and Sandbox editor panel — is owned by `runtime/RUNTIME-175` and `ui/UI-035`,
  mirroring the retired `RUNTIME-134` progressive-Poisson playground. Promote
  CPU-reference-first in the dependency order encoded in each task's
  `depends_on`; do not open optimized/GPU/engine slices before the reference
  variant they extend is implemented, tested, and benchmark-manifested.
- **Parameterization family (METHOD-021..026).** The state-of-the-art mesh
  parameterization variants — ARAP (`METHOD-021`), SLIM (`METHOD-022`), BFF
  (`METHOD-023`), and SCP (`METHOD-024`) — each add a concrete params
  alternative to the typed CPU strategy variant retired by `geometry/GEOM-063`,
  alongside the existing LSCM and Tutte/Harmonic (`GEOM-019`) alternatives.
  No backend token is reserved: optimized/GPU policy enters with the later task
  that owns a real second implementation. The
  iterative variants share the `geometry/GEOM-064` optimization-kernel seam
  (local rotation fit, symmetric-Dirichlet energy/proxy, injective line search)
  and the `GEOM-018` diagnostics. The optimized CPU task (`METHOD-025`) is
  SLIM-only; ARAP records no optimized follow-up. The GPU task (`METHOD-026`)
  follows reference parity for both iterative strategies (the linear one-shot
  strategies record no optimized/GPU follow-up); and the
  engine integration — config lane, runtime facade, the UV view model, and the
  Sandbox editor panel with a resizable UV split view — was delivered by
  retired `RUNTIME-176` and `UI-036`. Later strategies/backends extend those
  delivered surfaces rather than assigning future work to either retired
  task. The optional `GRAPHICS-122` GPU-shaded upgrade was delivered and
  retired at `Operational` on 2026-07-15, with the derived-view decision
  recorded in
  [ADR-0025](../../../docs/adr/0025-parameterization-uv-view-and-split-view.md).
  This mirrors the LOP consolidation family and the retired `RUNTIME-134`
  progressive-Poisson playground. Realizes Packs 3–4 of the
  [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md)
  plus the new SCP/BFF SOTA packs. Promote CPU-reference-first in each task's
  `depends_on` order.
- **Normal-orientation publication track (METHOD-032..036).** `METHOD-032`
  owns the in-house octree-parity killing gate and conditional CPU reference;
  `METHOD-033` supplies screened-Poisson reconstruction for the `METHOD-034`
  iPSR baseline; `METHOD-035` supplies the PGR winding-number baseline; and
  `METHOD-036` owns only the shared-input comparison evidence and publication
  report. The optional Sandbox diagnostic view is `runtime/RUNTIME-189` and
  cannot change method outputs or confer method maturity.
- Forbidden: importing runtime, graphics, platform, app, or live ECS ownership
  into a method package; claiming performance wins without a baseline.

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [METHOD-002 — Signed Heat Method reference backend](../../archive/METHOD-002-signed-heat-method-reference-backend.md)
  (done, 2026-06-28, `CPUContracted`): pathfinder method package for
  `geometry.signed_heat`, with surface Variant A CPU reference, correctness
  tests, smoke benchmark, method docs, and `Geometry.Sparse::SparseLDLT` as the
  default heat/Poisson solver.
- [METHODS-001 — Pin signed heat as methods-pipeline pathfinder](../../archive/METHODS-001-signed-heat-pathfinder.md)
  (done, 2026-06-28, `Retired`): records METHOD-002 as the first method to
  drive the full paper-intake → CPU-reference → tests → benchmark → docs
  pipeline and names retired GEOM-020 as the LDLT gate that makes promotion
  deterministic.
- [METHOD-012 — Progressive Poisson-disk sampling: paper intake + CPU reference backend](../../archive/METHOD-012-progressive-poisson-disk-cpu-reference.md)
  (done, 2026-06-28, `CPUContracted`): deterministic CPU reference, manifest,
  docs, correctness tests, and smoke benchmark for progressive Poisson-disk
  subsampling. GPU parity remains owned by
  [`METHOD-014`](METHOD-014-progressive-poisson-gpu-operational-parity.md).
- [METHOD-013 — Progressive Poisson-disk sampling: GPU backend contract slices](../../archive/METHOD-013-progressive-poisson-disk-gpu-backend.md)
  (done, 2026-07-02, `CPUContracted`): Vulkan planning, shader assets,
  recordable dispatch seams, upload/readback-copy ownership, parsed readback
  payloads, and CPU-reference parity diagnostics. Public GPU result return,
  `gpu;vulkan` parity tests, and benchmark metric extension remain owned by
  `METHOD-014`.
- [METHOD-001 — Rigid-body dynamics reference backend](../../archive/METHOD-001-rigid-body-dynamics-reference-backend.md)
  (done 2026-06-05 at `CPUContracted`; ownership gate accepted by
  [`ARCH-001`](../../archive/ARCH-001-physics-layer-ownership-and-ecs-integration.md)
  / [ADR-0019](../../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md);
  ECS authoring side handled by retired
  [`HARDEN-064`](../../archive/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)).
- [METHOD-008 — Resolve `_example_vector_heat` method manifest placeholders](../../archive/METHOD-008-example-vector-heat-manifest-placeholders.md)
  (done 2026-06-06, Outcome A): relocated the structure example from
  `methods/geometry/_example_vector_heat/` to `methods/_examples/vector_heat/`
  and resolved its `TODO`/`year: 0` paper placeholders; no CPU reference backend
  (real Vector Heat Method intake remains a future METHOD-* task).
- [METHOD-009 — Particle and mass-spring reference backend](../../archive/METHOD-009-particle-spring-reference-backend.md)
  (done, 2026-06-10, `CPUContracted`): deterministic particle/mass-spring
  `cpu_reference` backend with stability/energy diagnostics and smoke
  benchmark; physics roadmap follow-up from
  [`ARCH-002`](../../archive/ARCH-002-physics-phenomena-roadmap.md).
- [METHOD-010 — XPBD cloth and shell reference backend](../../archive/METHOD-010-xpbd-cloth-shell-reference-backend.md)
  (done, 2026-06-10, `CPUContracted`): deterministic XPBD cloth/shell
  `cpu_reference` backend with constraint residual/convergence diagnostics
  and smoke benchmark
  (physics roadmap follow-up from [`ARCH-002`](../../archive/ARCH-002-physics-phenomena-roadmap.md)).
- [METHOD-011 — SPH fluid reference backend](../../archive/METHOD-011-sph-fluid-reference-backend.md)
  (done, 2026-06-10, `CPUContracted`): deterministic WCSPH `cpu_reference`
  backend with density/compression and neighbor diagnostics and smoke
  benchmark
  (physics roadmap follow-up from [`ARCH-002`](../../archive/ARCH-002-physics-phenomena-roadmap.md)).
- Ordering: [`geometry/GEOM-008`](../../archive/GEOM-008-linear-algebra-solver-infrastructure.md)
  retired 2026-05-27 at `CPUContracted` shipping the CSR builder + CG /
  shifted-CG iterative solver. The direct sparse SPD factorization
  (LDLT/LLT) path that METHOD-002 used is available from retired
  [`geometry/GEOM-020`](../../archive/GEOM-020-sparse-direct-factorization-seam.md);
  METHOD-002 is retired at `CPUContracted`.
  METHOD-003's selected non-symmetric closest-point-extension solver is
  available from retired
  [`geometry/GEOM-023`](../../archive/GEOM-023-sparse-nonsymmetric-iterative-solver-seam.md);
  METHOD-003 is no longer blocked on the solver seam.
  METHOD-004 needs no solver gate and may proceed against retired
  `GEOM-008` directly. METHOD-005 and METHOD-007 waited on
  [`geometry/GEOM-007`](../../archive/GEOM-007-robust-predicates-intersection-classification.md),
  which retired 2026-05-27, so both are promotable. METHOD-006 step 4's
  sparse symmetric generalized eigensolver is owned by
  [`geometry/GEOM-024`](../geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md).
  All of these gates are encoded in the method tasks' `depends_on`
  front-matter and surface in `tasks/SESSION-BRIEF.md`.
