# Backlog reconciliation — 2026-09-15

Source reviewed: `c1986c76fdcad6a7385cdf0cdf46e149375e5ef8`.
Scope: all 97 task bodies under `tasks/backlog/`, their current source/test
owners, dependencies and backlog indexes. The operator requested obsolete-work
removal, then clarified that real gaps must be rewritten against the improved
engine. This is a backlog audit, not a full engine correctness audit.

## Disposition

No complete task was proven obsolete. All 97 retain a real implementation,
integration, evidence or conditional-intake gap. Revised 31 task bodies and
removed completed requirements, stale architecture instructions and duplicated
slice-time remainder lists. Replaced 11 bloated backlog indexes with current
navigation; retirement history remains in its canonical records and Git.
No task is marked done merely because a prerequisite or helper landed.

Three independent read-only Claude Code CLI partitions covered all task bodies
(30 integration/geometry/UI, 41 infrastructure, 26 methods). The primary writer
reconciled their findings against source and task contracts. In particular:

- Kept satisfied dependency edges: done/archive references are already satisfied
  by the task resolver and preserve provenance. They do not block selection.
- Rejected the suggestion to reopen descriptor matching: `MatchDescriptors`
  consumes `DescriptorSet`, and coarse alignment already consumes spans.
  Bounds/noise still expose Cloud-only entry points in
  `src/geometry/Geometry.PointCloud.Utils.cppm`.
- Kept GEOM-076's existing adoption gate. METHOD-044 explicitly reserves a
  later rescope; adjacent offline experiments do not authorize production
  adoption or erase the task's real future integration obligations.
- Enrolled only edited legacy tasks; byte-identical legacy notes keep their
  prospective exemptions. No research threshold, frozen evidence, or backend
  acceptance gate was relaxed.

## Current implementation anchors

- `src/runtime/Editor/Operations/Runtime.PointSetOperations.cppm` and
  `Runtime.ParameterizationOperations.cppm` own the typed operations; completed
  Sandbox facade/DTO cleanup is not future METHOD-014/026 work.
- `src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm` owns mesh
  field integration; Signed Heat's geometry implementation is not yet bound.
- `src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp` supplies disabled tooltips
  and shared processing support; UI tasks should consume family-owned frames.
- `src/runtime/Modules/Clustering/Runtime.ClusteringModule.cpp` still restricts
  execution domains. Progressive Poisson also retains provenance-based binding
  in `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cpp`.
  RUNTIME-211/212 and UI-043/044 therefore remain necessary.
- `VectorFieldVisualizationRecipe`, `AppendVectorFieldPacket` and vector-field
  extraction already exist. UI-050 owns generic property-list binding rather
  than a new vector renderer. GPU readback proof of that new binding is still owed.
- `src/runtime/AssetWorkflow/Runtime.AssetWorkflowRecipePolicies.cpp` owns the
  remaining import visualization policy targeted by GRAPHICS-105.
- `cmake/Dependencies.cmake` already arranges stb image-write implementation;
  GRAPHICS-109 owes the final capture/publication path, not another image library.
- `tools/benchmark/run_and_seal.py` already propagates stage failure, but still
  classifies output roots by suffix. BUG-149 remains open for the latter.
- `tools/benchmark/validate_benchmark_manifests.py` owns the metric allowlist.
  METHOD-035 now agrees with METHOD-032/034/036 on structured diagnostics and
  separated comparison information budgets.

## All backlog tasks

“Keep” means no substantive contradiction found in the task's remaining scope;
it does not claim reproduction of a bug, method acceptance, or a passing gate.
Task links retain detailed acceptance criteria, source references and evidence.

| Task | Disposition | Remaining work / correction |
| --- | --- | --- |
| [REVIEW-004](../../tasks/backlog/architecture/REVIEW-004-framework24-product-convergence-audit.md) | Keep | Framework24 product-convergence audit. |
| [ASSETIO-009](../../tasks/backlog/assets/ASSETIO-009-loss-aware-materialx-subset-import.md) | Keep | Loss-aware MaterialX subset import. |
| [ASSETIO-010](../../tasks/backlog/assets/ASSETIO-010-async-model-companion-preflight.md) | Revised | Add async preview to the existing import recipe; bridge deletion is complete. |
| [ASSETIO-011](../../tasks/backlog/assets/ASSETIO-011-semantic-sandbox-file-import-workflow-matrix.md) | Revised | Real-control import matrix remains; reuse the landed format capability table. |
| [BENCH-001](../../tasks/backlog/benchmarks/BENCH-001-framework24-golden-workflow-comparison-harness.md) | Keep | Framework24 golden-workflow comparison harness. |
| [BUG-091](../../tasks/backlog/bugs/BUG-091-gtest-pretest-discovery-cold-timeout.md) | Keep | GoogleTest PRE_TEST discovery times out on a cold start. |
| [BUG-097](../../tasks/backlog/bugs/BUG-097-progressive-model-scene-zero-uv-atlas.md) | Keep | Progressive model-scene UV job publishes a zero atlas. |
| [BUG-149](../../tasks/backlog/bugs/BUG-149-benchmark-sealer-dotted-output-directory.md) | Revised | Fix output-root classification; exit-status propagation already exists. |
| [BUG-160](../../tasks/backlog/bugs/BUG-160-fast-staged-atlas-chart-fragmentation.md) | Keep | FastStaged fixed seed planes fragment smooth meshes into tiny charts. |
| [BUG-171](../../tasks/backlog/bugs/BUG-171-required-command-receipt-supersession.md) | Keep | Required development receipts cannot be superseded by a passing rerun. |
| [BUG-172](../../tasks/active/BUG-172-synchronous-cpu-load-completion-race.md) | Keep | Synchronous CPU completion can observe an unfinished or stale load transition. |
| [BUG-176](../../tasks/done/BUG-176-concurrent-ctest-discovery.md) | Keep | Concurrent CTest discovery can duplicate generated registrations. |
| [BUG-178](../../tasks/backlog/bugs/BUG-178-clang23-incremental-module-ice.md) | Keep | Clang 23 crashes during an incremental module rebuild. |
| [BUG-180](../../tasks/backlog/bugs/BUG-180-framed-icp-leak-enabled-process-retention.md) | Keep | Leak-enabled framed ICP process reports 240 retained bytes. |
| [BUG-182](../../tasks/backlog/bugs/BUG-182-framed-knn-unavailable-device-timeout.md) | Keep | Framed kNN smoke times out when promoted Vulkan is not compiled. |
| [BUG-188](../../tasks/backlog/bugs/BUG-188-sandbox-sanitizer-test-discovery.md) | Keep | Sandbox blocks LeakSanitizer during CTest discovery. |
| [BUG-193](../../tasks/backlog/bugs/BUG-193-gpu-pacing-and-watchdog-margin.md) | Keep | Investigate GPU pacing variability and watchdog margin. |
| [BUG-195](../../tasks/backlog/bugs/BUG-195-verification-disk-headroom.md) | Keep | Verification can exhaust host disk space. |
| [GEOM-013](../../tasks/backlog/geometry/GEOM-013-feature-preserving-dual-contouring.md) | Keep | Feature-preserving dual contouring. |
| [GEOM-024](../../tasks/backlog/geometry/GEOM-024-sparse-symmetric-generalized-eigensolver-seam.md) | Revised | Generalized eigensolver remains; the LDLT prerequisite is already delivered. |
| [GEOM-059](../../tasks/backlog/geometry/GEOM-059-kernel-matrices-nystroem-gaussian-process.md) | Keep | Kernel matrices, Nyström approximation, and Gaussian-process interpolation seam. |
| [GEOM-060](../../tasks/backlog/geometry/GEOM-060-permutohedral-lattice-highdim-filtering.md) | Revised | Lattice remains absent; extend the delivered bilateral owner if justified. |
| [GEOM-061](../../tasks/backlog/geometry/GEOM-061-grid-downsampling-reduction-strategies.md) | Revised | Index-returning grid reductions remain; BUG-109 is an existing invariant. |
| [GEOM-065](../../tasks/backlog/geometry/GEOM-065-invariant-aware-scientific-field-mip-pyramids.md) | Keep | Invariant-aware scientific-field mip pyramids. |
| [GEOM-066](../../tasks/backlog/geometry/GEOM-066-meshoptimizer-v1-2-geometry-oracle.md) | Revised | Oracle/adoption evidence remains; recheck the pinned dependency before requiring an override. |
| [GEOM-067](../../tasks/backlog/geometry/GEOM-067-memory-aware-bvh-merged-node-evidence.md) | Keep | Memory-aware BVH and merged-node evidence. |
| [GEOM-068](../../tasks/backlog/geometry/GEOM-068-weighted-dijkstra-edge-cost-contract.md) | Keep | Weighted Dijkstra edge-cost contract. |
| [GEOM-069](../../tasks/backlog/geometry/GEOM-069-astar-graph-shortest-path.md) | Keep | A* graph shortest path. |
| [GEOM-070](../../tasks/backlog/geometry/GEOM-070-sparse-lsqr-lscm-adoption.md) | Keep | Rectangular sparse LSQR and LSCM adoption. |
| [GEOM-072](../../tasks/backlog/geometry/GEOM-072-catmull-clark-crease-masks.md) | Keep | Catmull-Clark crease masks. |
| [GEOM-073](../../tasks/backlog/geometry/GEOM-073-point-analysis-property-span-contracts.md) | Revised | Audit remaining bounds/noise borrowed inputs; analysis, FPFH and matching kernels already exist. |
| [GEOM-074](../../tasks/backlog/geometry/GEOM-074-graph-property-adjacency-contracts.md) | Revised | Borrowed adjacency/property inputs remain; old API compatibility is not required. |
| [GEOM-076](../../tasks/backlog/geometry/GEOM-076-curvature-region-guided-uv-atlas-cuts.md) | Revised | Keep conditional adoption gate; reconcile active atlas experiments before any rescope. |
| [HARDEN-084](../../tasks/backlog/methods/HARDEN-084-localized-cpu-gpu-parity-signatures.md) | Keep | Localized CPU/GPU parity signatures. |
| [METHOD-003](../../tasks/backlog/methods/METHOD-003-closest-point-method-pde-reference-backend.md) | Keep | Closest Point Method PDE solver reference backend. |
| [METHOD-003A](../../tasks/backlog/methods/METHOD-003A-spatial-query-reference-integration-intake.md) | Keep | Spatial-query CPU-reference integration intake. |
| [METHOD-004](../../tasks/backlog/methods/METHOD-004-walk-on-spheres-reference-backend.md) | Revised | Walker remains absent; select RNG reuse by actual contract and callers. |
| [METHOD-005](../../tasks/backlog/methods/METHOD-005-robust-mesh-boolean-reference-backend.md) | Keep | Robust mesh boolean reference backend. |
| [METHOD-006](../../tasks/backlog/methods/METHOD-006-cross-field-design-reference-backend.md) | Keep | Surface cross-field design CPU reference backend. |
| [METHOD-007](../../tasks/backlog/methods/METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md) | Keep | Constrained Delaunay tetrahedralization reference backend. |
| [METHOD-007A](../../tasks/backlog/methods/METHOD-007A-cdt-engine-integration-intake.md) | Keep | CDT engine-integration intake and ownership. |
| [METHOD-014](../../tasks/backlog/methods/METHOD-014-progressive-poisson-gpu-operational-parity.md) | Revised | Actual Vulkan result/parity remains; typed operation and facade deletion are delivered. |
| [METHOD-015](../../tasks/backlog/methods/METHOD-015-coherent-point-drift-family-reference-backend.md) | Keep | Coherent Point Drift registration family reference backend. |
| [METHOD-021](../../tasks/backlog/methods/METHOD-021-arap-parameterization-reference-backend.md) | Keep | ARAP (local/global) parameterization reference backend. |
| [METHOD-022](../../tasks/backlog/methods/METHOD-022-slim-injective-parameterization-reference-backend.md) | Keep | SLIM locally-injective parameterization reference backend. |
| [METHOD-024](../../tasks/backlog/methods/METHOD-024-spectral-conformal-parameterization-reference-backend.md) | Keep | Spectral Conformal Parameterization (SCP) reference backend. |
| [METHOD-025](../../tasks/backlog/methods/METHOD-025-parameterization-family-optimized-cpu-backend.md) | Revised | SLIM optimization remains gated; introduce only an adopted SLIM backend axis. |
| [METHOD-026](../../tasks/backlog/methods/METHOD-026-parameterization-family-gpu-vulkan-compute-backend.md) | Revised | GPU implementation remains; extend the existing typed family operation and prepared frame. |
| [METHOD-027](../../tasks/backlog/methods/METHOD-027-adaptive-delaunay-qef-implicit-meshing.md) | Keep | Adaptive Delaunay/QEF implicit meshing reference. |
| [METHOD-028](../../tasks/backlog/methods/METHOD-028-confidence-driven-walk-on-stars-guiding.md) | Keep | Confidence-driven spatial guiding for Walk on Stars. |
| [METHOD-029](../../tasks/backlog/methods/METHOD-029-discontinuity-aware-material-derivatives-and-jacobian-portability.md) | Keep | Discontinuity-aware material derivatives. |
| [METHOD-030](../../tasks/backlog/methods/METHOD-030-neural-render-proxy-path-replay-reference.md) | Keep | Neural render proxy path-replay reference. |
| [METHOD-031](../../tasks/backlog/methods/METHOD-031-jacobian-portability-predictive-study.md) | Keep | Cross-artifact Jacobian portability prediction. |
| [METHOD-032](../../tasks/backlog/methods/METHOD-032-octree-parity-normal-orientation.md) | Keep | Octree parity normal orientation reference backend. |
| [METHOD-033](../../tasks/backlog/methods/METHOD-033-screened-poisson-reconstruction-reference.md) | Keep | Screened Poisson surface reconstruction reference backend. |
| [METHOD-033A](../../tasks/backlog/methods/METHOD-033A-screened-poisson-engine-integration-intake.md) | Keep | Screened Poisson engine-integration intake and ownership. |
| [METHOD-034](../../tasks/backlog/methods/METHOD-034-ipsr-orientation-baseline.md) | Keep | iPSR normal orientation baseline (reference backend). |
| [METHOD-035](../../tasks/backlog/methods/METHOD-035-pgr-winding-number-orientation-baseline.md) | Revised | PGR remains absent; use existing benchmark metrics and method-specific diagnostics. |
| [METHOD-036](../../tasks/backlog/methods/METHOD-036-orientation-comparison-evidence.md) | Keep | Normal-orientation method comparison evidence (publication protocol). |
| [PLATFORM-004](../../tasks/backlog/platform/PLATFORM-004-alternative-platform-backend-onboarding.md) | Keep | Alternative-platform backend onboarding policy (planning seed). |
| [BUILD-005](../../tasks/backlog/process/BUILD-005-hermetic-toolchain-action-identity.md) | Keep | Define hermetic toolchain and action identity. |
| [BUILD-006](../../tasks/backlog/process/BUILD-006-cxx23-module-build-backend-bakeoff.md) | Revised | Build/cache backend comparison remains distinct from completed source-locality measurements. |
| [CI-012](../../tasks/backlog/process/CI-012-versioned-verification-evidence-graph.md) | Keep | Compile a versioned verification evidence graph. |
| [CI-013](../../tasks/backlog/process/CI-013-unified-verifier-profiles-and-receipts.md) | Keep | Add unified verifier profiles and receipts. |
| [CI-014](../../tasks/backlog/process/CI-014-static-build-contract-impact-graph.md) | Keep | Derive the static build and contract impact graph. |
| [CI-015](../../tasks/backlog/process/CI-015-digest-test-inventory-and-sharding.md) | Keep | Add digest-keyed test inventory and deterministic sharding. |
| [CI-016](../../tasks/backlog/process/CI-016-content-addressed-build-test-result-cache.md) | Keep | Add a content-addressed build and test result cache. |
| [CI-017](../../tasks/backlog/process/CI-017-test-quality-and-fault-detection-oracle.md) | Keep | Establish a test-quality and fault-detection oracle. |
| [CI-018](../../tasks/backlog/process/CI-018-hybrid-impact-selection-admission.md) | Keep | Admit hybrid impact-based verification selection. |
| [CI-019](../../tasks/backlog/process/CI-019-thin-ci-merge-queue-topology.md) | Keep | Make CI thin and run full confidence once per merge group. |
| [CI-020](../../tasks/backlog/process/CI-020-verification-cutover-and-legacy-retirement.md) | Keep | Cut over verification and retire legacy policy. |
| [PROC-031](../../tasks/backlog/process/PROC-031-agent-verification-receipts.md) | Revised | Unified receipt binding remains; BUG-171 owns receipt supersession. |
| [GRAPHICS-105](../../tasks/active/GRAPHICS-105-unified-mesh-shading-and-attribute-source-authority.md) | Revised | Material synthesis remains; remove one redundant Unlit write/bit, preserving the slot-0 error material. |
| [GRAPHICS-109](../../tasks/backlog/rendering/GRAPHICS-109-offscreen-frame-capture-png.md) | Revised | PNG publication remains; reuse the manifested stb encoder implementation. |
| [GRAPHICS-123](../../tasks/backlog/rendering/GRAPHICS-123-slang-single-kernel-gradient-pilot.md) | Keep | Slang single-kernel gradient pilot. |
| [GRAPHICS-124](../../tasks/backlog/rendering/GRAPHICS-124-slughorn-world-space-vector-annotation-proof.md) | Keep | Slughorn world-space vector annotation proof. |
| [GRAPHICS-125](../../tasks/backlog/rendering/GRAPHICS-125-memory-priced-cluster-hierarchy-evidence.md) | Keep | Memory-priced cluster hierarchy evidence. |
| [GRAPHICS-126](../../tasks/backlog/rendering/GRAPHICS-126-bandwidth-priced-frame-recipe-trace-model.md) | Keep | Bandwidth-priced frame-recipe trace model. |
| [GRAPHICS-135](../../tasks/backlog/rendering/GRAPHICS-135-renderprep-per-frame-taskgraph-overhead.md) | Keep | Measure current render-prep scheduling and material-sync overhead. |
| [GRAPHICS-136](../../tasks/backlog/rendering/GRAPHICS-136-rename-pipeline-to-graphics-state-rhi-boundary.md) | Keep | Rename `Pipeline*` to `GraphicsState*` at the RHI boundary. |
| [GRAPHICS-137](../../tasks/backlog/rendering/GRAPHICS-137-shader-object-realization-spike.md) | Keep | Shader-object realization spike (ADR-0028 killing experiment). |
| [LEGACY-043](../../tasks/backlog/rendering/LEGACY-043-retire-stale-multiset-shaders.md) | Keep | Retire stale multi-descriptor-set shader sources. |
| [RUNTIME-210](../../tasks/backlog/runtime/RUNTIME-210-signed-heat-runtime-config-integration.md) | Revised | Signed Heat runtime binding remains; extend MeshFieldOperations. |
| [RUNTIME-211](../../tasks/backlog/runtime/RUNTIME-211-kmeans-property-domain-integration.md) | Keep | K-Means property-domain integration. |
| [RUNTIME-212](../../tasks/backlog/runtime/RUNTIME-212-progressive-poisson-property-domain-publication.md) | Keep | Progressive Poisson property-domain publication. |
| [RUNTIME-218](../../tasks/backlog/runtime/RUNTIME-218-default-scene-lighting-and-light-authoring.md) | Revised | Light authoring remains missing; update scene and extraction ownership references. |
| [RUNTIME-222](../../tasks/backlog/runtime/RUNTIME-222-model-space-point-radius-rendering.md) | Revised | Model-space radius rendering remains; existing PointSizeBDA is only dormant transport. |
| [UI-037](../../tasks/active/UI-037-linear-domain-action-readiness-tooltips.md) | Revised | Unify remaining readiness pairs through family frames and the existing tooltip helper. |
| [UI-042](../../tasks/backlog/ui/UI-042-signed-heat-mesh-panel.md) | Revised | Signed Heat panel remains; reuse shared processing-panel support. |
| [UI-043](../../tasks/backlog/ui/UI-043-kmeans-property-domain-panel.md) | Revised | K-Means domain panel remains blocked on RUNTIME-211; reuse shared panel support. |
| [UI-044](../../tasks/backlog/ui/UI-044-progressive-poisson-property-domain-panel.md) | Revised | Poisson domain panel remains blocked on RUNTIME-212; reuse shared panel support. |
| [UI-046](../../tasks/backlog/ui/UI-046-sandbox-geometry-export.md) | Keep | Sandbox cannot export geometry at all. |
| [UI-047](../../tasks/backlog/ui/UI-047-file-chooser-for-import-and-scene-paths.md) | Revised | Path chooser remains missing; source references updated to current controls. |
| [UI-048](../../tasks/backlog/ui/UI-048-first-run-workspace-and-layout-persistence.md) | Revised | First-run layout/persistence remains; references updated to current owners. |
| [UI-049](../../tasks/backlog/ui/UI-049-editor-panel-sizing-and-readability.md) | Revised | Reproduce historical layout symptoms on shared panels before fixing remaining defects. |
| [UI-050](../../tasks/backlog/ui/UI-050-vector-field-property-visualization.md) | Revised | Generic vector-property action remains; reuse the delivered vector recipe/rendering path. |
| [UI-051](../../tasks/backlog/ui/UI-051-domain-agnostic-appearance-properties-selection-windows.md) | Revised | Property-based domain windows remain; update the actual shared-panel gate locations. |

## Review and verification

Changes are documentation/task reconciliation only; engine source and tests are
unchanged. Source inspection establishes remaining implementation gaps, not
fresh runtime, sanitizer, GPU or performance evidence. Historical UI symptoms
are explicitly queued for reproduction where panel ownership has changed.

Claude's final fixed-diff review found one stale flag-writer inventory and one
ambiguous prospective test command. Corrected GRAPHICS-105 to remove the sole
redundant Unlit write/bit while preserving slot-0 error shading; BUG-149 now
explicitly names the regression file to create before running its command.
Also corrected UI/ECS index capitalization. The primary writer checked the
additional five source-reference/premise corrections against the audit evidence.

Passed after reconciliation:

```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
```

No C++ build or runtime test was needed for these documentation-only edits.
