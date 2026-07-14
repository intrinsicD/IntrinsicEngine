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
  (variant A default; unblocked by retired `geometry/GEOM-023` and expected
  to use `Geometry.Sparse::SparseBiCGSTAB` for its non-symmetric solve).
- [METHOD-004 — Walk on Spheres / Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md)
  (variant A default; no solver gate — promotable now).
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
  (variant A default; `GEOM-007` gate satisfied 2026-05-27 — promotable now).
- [METHOD-006 — Cross-field / frame-field design reference backend](METHOD-006-cross-field-design-reference-backend.md)
  (variant B default; gated on `geometry/GEOM-024` eigensolver seam).
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
  (variant A default; `GEOM-007` gate satisfied 2026-05-27 — promotable now).
- [METHOD-014 — Progressive Poisson GPU operational parity](METHOD-014-progressive-poisson-gpu-operational-parity.md)
  (Vulkan compute operational/parity follow-up to retired `METHOD-013`; no
  speedup claim without benchmark baseline comparison).
- [METHOD-015 — Coherent Point Drift registration family reference backend](METHOD-015-coherent-point-drift-family-reference-backend.md)
  (variant A rigid default with affine/nonrigid in-package; gated on
  `geometry/GEOM-058` Gaussian-mixture/EM seam).
- [METHOD-016 — Locally Optimal Projection (LOP/WLOP) consolidation reference backend](METHOD-016-locally-optimal-projection-reference-backend.md)
  (variant A WLOP default; establishes the shared `Geometry.PointCloud.Consolidation`
  strategy×backend surface; gated on `geometry/GEOM-062` kernel seam).
- [METHOD-017 — Continuous LOP (CLOP) reference backend](METHOD-017-continuous-lop-clop-reference-backend.md)
  (adds the `Clop` strategy; gated on `METHOD-016`, `geometry/GEOM-058`
  Gaussian-mixture seam, and `geometry/GEOM-062` kernels).
- [METHOD-018 — Edge-Aware Resampling (EAR) and anisotropic feature-preserving LOP reference backend](METHOD-018-edge-aware-resampling-anisotropic-lop-reference-backend.md)
  (adds the `Ear`/anisotropic strategies; gated on `METHOD-016` and
  `geometry/GEOM-062`; consumes `Geometry.PointCloud.Normals`).
- [METHOD-019 — LOP-family optimized CPU backend and comparison benchmark](METHOD-019-lop-family-optimized-cpu-backend.md)
  (adds `cpu_optimized` with parity; gated on `METHOD-016`/`017`/`018`; reuses
  the `geometry/GEOM-060` permutohedral and `geometry/GEOM-061` grid seams).
- [METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity](METHOD-020-lop-family-gpu-vulkan-compute-backend.md)
  (adds `gpu_vulkan_compute` with `gpu;vulkan` parity; gated on `METHOD-019`).
- [METHOD-021 — ARAP (local/global) parameterization reference backend](METHOD-021-arap-parameterization-reference-backend.md)
  (adds the `Arap` strategy to the shared `Geometry.Parameterization` surface;
  gated on `geometry/GEOM-063` dispatch surface and `geometry/GEOM-064`
  optimization kernels; roadmap Pack 3).
- [METHOD-022 — SLIM locally-injective parameterization reference backend](METHOD-022-slim-injective-parameterization-reference-backend.md)
  (adds the flip-free `Slim` strategy; gated on `GEOM-063`, `GEOM-064`, and
  `METHOD-021`; roadmap Pack 4).
- [METHOD-023 — Boundary First Flattening (BFF) reference backend](METHOD-023-boundary-first-flattening-reference-backend.md)
  (adds the controllable-conformal `Bff` strategy with boundary length/angle
  targets and cones; gated on `GEOM-063`; new SOTA pack).
- [METHOD-024 — Spectral Conformal Parameterization (SCP) reference backend](METHOD-024-spectral-conformal-parameterization-reference-backend.md)
  (adds the pin-free conformal `Scp` strategy; gated on `GEOM-063` and the
  `geometry/GEOM-024` generalized eigensolver seam; new SOTA pack).
- [METHOD-025 — Parameterization family optimized CPU backend and comparison benchmark](METHOD-025-parameterization-family-optimized-cpu-backend.md)
  (adds `cpu_optimized` progressive acceleration with parity; gated on
  `METHOD-021`/`022`).
- [METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity](METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md)
  (adds `gpu_vulkan_compute` for the iterative ARAP/SLIM strategies with
  `gpu;vulkan` parity; gated on `METHOD-025` and `runtime/RUNTIME-176`, whose
  facade/job-queue seam it wires into; the linear one-shot strategies
  (LSCM/SCP/BFF) record no GPU follow-up in their own tasks).

## Convergence

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
  Each task lists explicit algorithm variants with a marked public-facing
  default backend; changing a default re-opens the corresponding solver-seam
  gating (see the per-task notes above).
- METHOD-015 and METHOD-016 are seeded by the 2026-07-07 framework24
  (`bcg_framework`) port-gap comparison (see the geometry backlog README's
  port-gap section). METHOD-015's E-step numerics are owned by
  `geometry/GEOM-058` (encoded in `depends_on`), and its optimized nonrigid
  fast path is deferred to the `geometry/GEOM-060` permutohedral seam.
- **LOP consolidation family (METHOD-016..020).** WLOP/LOP (`METHOD-016`),
  CLOP (`METHOD-017`), and EAR/anisotropic (`METHOD-018`) share one
  `Geometry.PointCloud.Consolidation` module and its `Strategy` × `Backend`
  dispatch axis (`docs/architecture/algorithm-variant-dispatch.md`), so every
  state-of-the-art variant is choosable through one surface. The shared weight
  math is factored into the `geometry/GEOM-062` `Geometry.PointCloud.Kernels`
  seam; the optimized CPU (`METHOD-019`) and GPU (`METHOD-020`) backends follow
  reference parity; and the engine integration — config lane, runtime facade,
  and Sandbox editor panel — is owned by `runtime/RUNTIME-175` and `ui/UI-035`,
  mirroring the retired `RUNTIME-134` progressive-Poisson playground. Promote
  CPU-reference-first in the dependency order encoded in each task's
  `depends_on`; do not open optimized/GPU/engine slices before the reference
  variant they extend is implemented, tested, and benchmark-manifested.
- **Parameterization family (METHOD-021..026).** The state-of-the-art mesh
  parameterization variants — ARAP (`METHOD-021`), SLIM (`METHOD-022`), BFF
  (`METHOD-023`), and SCP (`METHOD-024`) — are added as `Strategy` values on the
  one shared `Geometry.Parameterization` `Strategy` × `Backend` surface
  (`geometry/GEOM-063`, `docs/architecture/algorithm-variant-dispatch.md`),
  alongside the existing Tutte/Harmonic (`GEOM-019`) and LSCM strategies, so every
  variant is choosable through one API and one config/UI/agent surface. The
  iterative variants share the `geometry/GEOM-064` optimization-kernel seam
  (local rotation fit, symmetric-Dirichlet energy/proxy, injective line search)
  and the `GEOM-018` diagnostics; the optimized CPU (`METHOD-025`) and GPU
  (`METHOD-026`) backends follow reference parity for the iterative strategies
  (the linear one-shot strategies record no optimized/GPU follow-up); and the
  engine integration —
  config lane, runtime facade, the UV view model, and the Sandbox editor panel
  with a resizable UV split view — is owned by `runtime/RUNTIME-176`,
  `ui/UI-036`, and the optional `rendering/GRAPHICS-122`, with the derived-view
  rendering decision recorded in
  [ADR-0025](../../../docs/adr/0025-parameterization-uv-view-and-split-view.md).
  This mirrors the LOP consolidation family and the retired `RUNTIME-134`
  progressive-Poisson playground. Realizes Packs 3–4 of the
  [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md)
  plus the new SCP/BFF SOTA packs. Promote CPU-reference-first in each task's
  `depends_on` order.
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
  METHOD-003's variant-A non-symmetric operator is available from retired
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
