# Methods Backlog

Current unstarted work. Use the [convergence priorities](../README.md) and
[resolved task state](../../SESSION-BRIEF.md) before selection. Each task owns
its dependencies, conditional gates, scope and verification.

## Tasks

- [HARDEN-084 — Localized CPU/GPU parity signatures](HARDEN-084-localized-cpu-gpu-parity-signatures.md)
- [METHOD-003 — Closest Point Method PDE solver reference backend](METHOD-003-closest-point-method-pde-reference-backend.md)
- [METHOD-003A — Spatial-query CPU-reference integration intake](METHOD-003A-spatial-query-reference-integration-intake.md)
- [METHOD-004 — Walk on Stars PDE solver reference backend](METHOD-004-walk-on-spheres-reference-backend.md)
- [METHOD-005 — Robust mesh boolean reference backend](METHOD-005-robust-mesh-boolean-reference-backend.md)
- [METHOD-006 — Surface cross-field design CPU reference backend](METHOD-006-cross-field-design-reference-backend.md)
- [METHOD-007 — Constrained Delaunay tetrahedralization reference backend](METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md)
- [METHOD-007A — CDT engine-integration intake and ownership](METHOD-007A-cdt-engine-integration-intake.md)
- [METHOD-014 — Progressive Poisson GPU operational parity](METHOD-014-progressive-poisson-gpu-operational-parity.md)
- [METHOD-021 — ARAP (local/global) parameterization reference backend](METHOD-021-arap-parameterization-reference-backend.md)
- [METHOD-022 — SLIM locally-injective parameterization reference backend](METHOD-022-slim-injective-parameterization-reference-backend.md)
- [METHOD-024 — Spectral Conformal Parameterization (SCP) reference backend](METHOD-024-spectral-conformal-parameterization-reference-backend.md)
- [METHOD-025 — Progressive SLIM optimized CPU backend and comparison benchmark](METHOD-025-parameterization-family-optimized-cpu-backend.md)
- [METHOD-026 — Parameterization family GPU (Vulkan compute) backend and parity](METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md)
- [METHOD-027 — Adaptive Delaunay/QEF implicit meshing reference](METHOD-027-adaptive-delaunay-qef-implicit-meshing.md)
- [METHOD-028 — Confidence-driven spatial guiding for Walk on Stars](METHOD-028-confidence-driven-walk-on-stars-guiding.md)
- [METHOD-029 — Discontinuity-aware material derivatives](METHOD-029-discontinuity-aware-material-derivatives-and-jacobian-portability.md)
- [METHOD-030 — Neural render proxy path-replay reference](METHOD-030-neural-render-proxy-path-replay-reference.md)
- [METHOD-031 — Cross-artifact Jacobian portability prediction](METHOD-031-jacobian-portability-predictive-study.md)
- [METHOD-032 — Octree parity normal orientation reference backend](METHOD-032-octree-parity-normal-orientation.md)
- [METHOD-033 — Screened Poisson surface reconstruction reference backend](METHOD-033-screened-poisson-reconstruction-reference.md)
- [METHOD-033A — Screened Poisson engine-integration intake and ownership](METHOD-033A-screened-poisson-engine-integration-intake.md)
- [METHOD-034 — iPSR normal orientation baseline (reference backend)](METHOD-034-ipsr-orientation-baseline.md)
- [METHOD-035 — Parametric Gauss (winding-number) orientation baseline (reference backend)](METHOD-035-pgr-winding-number-orientation-baseline.md)
- [METHOD-036 — Normal-orientation method comparison evidence (publication protocol)](METHOD-036-orientation-comparison-evidence.md)

- [METHOD-048 — Full HKTex mesh textures and measured default adoption](METHOD-048-hktex-mesh-textures.md)
- [METHOD-052 — Anderson acceleration for Coherent Point Drift EM](METHOD-052-coherent-point-drift-anderson-acceleration.md)
- [METHOD-054 — Geodesic Bayesian Coherent Point Drift (GBCPD)](METHOD-054-geodesic-bayesian-coherent-point-drift.md)
- [METHOD-055 — Vulkan compute port of the hole-sieve farthest-point sampler](METHOD-055-vulkan-hole-sieve-farthest-point-sampling.md)
- [METHOD-057 — Vulkan truncated and Nystroem E-steps for Coherent Point Drift (gated)](METHOD-057-coherent-point-drift-vulkan-truncated-and-nystrom.md)
- [METHOD-058 — Permutohedral-lattice E-step for Coherent Point Drift (FilterReg), CPU](METHOD-058-coherent-point-drift-lattice-e-step.md)
- [METHOD-059 — Vulkan permutohedral-lattice E-step for Coherent Point Drift (gated)](METHOD-059-coherent-point-drift-vulkan-lattice-e-step.md)
- [METHOD-060 — Vulkan coupled eta-sieve](METHOD-060-vulkan-coupled-eta-sieve.md)
- [METHOD-061 — Vulkan flat beta-greedy / implicit MIS sampling](METHOD-061-vulkan-flat-beta-greedy.md)
- [METHOD-062 — Vulkan lazy greedy sampling over the point LBVH](METHOD-062-vulkan-lazy-greedy-lbvh.md)

## RobustPCA applications (REVIEW-007 E7, GE15)

Operator-requested proposals for `Geometry::Linalg::RobustPCA` (2026-10-06).
Each task owns its formulation, kernel extensions (thin SVD, observation mask)
and any missing-input prerequisite; none states a result.

- [METHOD-066 — Point-cloud patch denoising and outlier scoring](METHOD-066-robust-pca-point-cloud-patch-denoising.md)
- [METHOD-067 — Fixed-topology mesh-sequence cleanup and compression](METHOD-067-robust-pca-mesh-sequence-cleanup-compression.md)
- [METHOD-068 — Rotation synchronization for multi-view alignment](METHOD-068-robust-pca-rotation-synchronization.md)
- [METHOD-069 — Robust photometric stereo (needs image-stack input)](METHOD-069-robust-pca-photometric-stereo.md)
- [METHOD-070 — Material-capture highlight and defect removal (needs image-stack input)](METHOD-070-robust-pca-material-capture-highlight-removal.md)
- [METHOD-071 — Background/foreground separation (needs image-sequence input)](METHOD-071-robust-pca-background-foreground-separation.md)
- [METHOD-072 — Animation and motion-capture trajectory cleanup](METHOD-072-robust-pca-motion-trajectory-cleanup.md)
- [METHOD-073 — Structure-from-motion measurement-matrix factorization (needs correspondences)](METHOD-073-robust-pca-structure-from-motion-factorization.md)
- [METHOD-074 — Shape-collection map synchronization](METHOD-074-robust-pca-shape-collection-map-synchronization.md)
- [METHOD-075 — BRDF and measurement-table cleanup (needs BRDF input and consumer)](METHOD-075-robust-pca-brdf-measurement-table-cleanup.md)

Theme I remains paused behind REVIEW-004 except explicit operator direction
and named product dependencies. A retired prerequisite does not prove a
method, backend, or adoption gate passed.

Completed and superseded work is recorded in the
[retirement log](../../done/RETIREMENT-LOG.md) and Git history.
