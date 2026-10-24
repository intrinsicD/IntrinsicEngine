---
id: RUNTIME-291
theme: G
depends_on: []
maturity_target: Operational
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the operator's panel check (2026-09-29); implementation owes contract tests and a live sandbox check.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.support-radius-policy]
---
# RUNTIME-291 — Scale-aware default radii for point operations

## Goal
- Operator check (2026-09-29, Vlasic mesh_0060, 10002 vertices, bounding-box diagonal 2.14):
  Compact Density Weights starts with `support_radius` 1.0 in absolute world units, about half
  the model. The Vulkan radius query then overflows its 256-candidate capacity, so the output
  is left unchanged. A radius of 0.03 worked (at most 49 candidates per row). Other absolute
  defaults (e.g. Outlier Analysis `radius` 1.0, Smooth Property `spatial_sigma`/`range_sigma`
  1.0, the Hoppe construction's scales) can be wrong in the same way on other models.
- Find a sensible default derived from the entity: from its bounding box / bounding volume
  (diagonal, or volume or area per point), or from mean nearest-neighbor spacing, which the
  Point Spacing operation already computes (it reports min / mean / max nearest spacing).
  Point Sampling's `elimination_radius` already follows the pattern "0 derives it from the
  bounding box"; FPFH derives its feature radius as five times the mean nearest spacing.
- Prefer one shared helper at the owner (runtime point-input capture or the point-cloud
  geometry layer) over per-panel formulas, and follow the existing "0 = automatic" convention.
  The resolved radius should be shown in the result, and a query capacity overflow should say
  "radius too large" with the suggested value.

## Acceptance criteria
- [ ] Survey every radius, sigma and bandwidth field of the point panels (Density Weights,
      Outlier Analysis radius mode, Normal Estimation radius mode, Smooth Property, Bilateral,
      Point Construction) and record which are absolute and what each scales with.
- [ ] Compact Density Weights (at least) defaults to an entity-derived radius: `support_radius`
      0 = automatic, reported in the result. On the Vlasic mesh it runs without overflow.
- [ ] The automatic rule is the same for mesh vertices, graph nodes and point clouds
      (`geometry.element-domain-sources`) and does not change under uniform scaling of the
      model (a contract test scales one fixture by 1000 and gets the same weights).
- [ ] An overflow message suggests a smaller radius instead of silently keeping the old output.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'DensityWeight|KernelDensity|PointSpacing|OutlierAnalysis|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
```

## Resolution — 2026-10-24

Overnight lane (delegated, operator absent). Checkboxes stay for the independent reviewers.

**Root cause.** `DensityWeightConfig::SupportRadius` defaulted to an absolute 1.0 and
validation rejected 0, so there was no automatic mode. On Vulkan the shader records the true
per-row candidate count, but `PointScalarTransaction::CompleteRun` only answered
"Scalar numerical failure or radius candidate overflow; increase capacity or select CPU."
Reproduction: `tasks/evidence/RUNTIME-291/repro_vlasic.py` is a SciPy mirror of the engine's
neighbour counting (not engine output) on `mesh_0060.obj` (10002 vertices, diagonal 2.1409):

| Radius | Max neighbours | p95 | Rows > 256 |
| --- | ---: | ---: | ---: |
| 1.0 (old default) | 10001 | 10001 | 10002 |
| 0.03 (operator; reproduces the reported 49) | 49 | 24 | 0 |
| LOP-family rule (16th neighbour, P75, x1.25): 0.04634 | 99 | 63 | 0 |
| Same rule on the mesh x1000: 46.3368 | 99 | 63 | 0 |

**Survey (criterion 1).**

| Panel | Field and default | Scales with |
| --- | --- | --- |
| Density Weights | `support_radius` 1.0 | Absolute world units; changed here to 0 = automatic |
| Outlier Analysis | `radius` 1.0 (only `method: radius`; default method is statistical) | Absolute |
| Outlier Analysis | `k_neighbors` 16, `minimum_neighbors` 4, `stddev_multiplier` 1, `score_threshold` 2 | Dimensionless |
| Normal Estimation | radius mode `radius` 0, must be > 0 when `use_radius` is set | Absolute, no default |
| Normal Estimation | `k` 15; epsilons | Numerical guards |
| Smooth Property | `spatial_sigma` 1.0 (position units, > 0) | Absolute |
| Smooth Property | `range_sigma` 1.0 | Absolute in property-value units, not geometry |
| Harmonic Field / Laplacian Eigenbasis | `spatial_sigma` 1.0 | Absolute |
| Bilateral point filter | `spatial_sigma` 0 = 2 x average spacing; `normal_sigma` 0.25 | Relative; angular |
| Point Construction (Hoppe) | `resolution` 32 cells; padding 0.1 x diagonal; `kernel_sigma_scale` 2 x kNN distance; `min_distance_epsilon` 1e-12 | Relative; the epsilon is an absolute numerical floor |
| Kernel Density | `bandwidth` 0 = automatic | Relative; absolute floor 1e-8 |
| Point Spacing | `scale_factor` 1 | Multiplier |
| ISS keypoints / FPFH | 0 = 6/4/5 x mean spacing | Relative |
| Point Sampling | `elimination_radius` 0 = from bounding box | Relative; degenerate fallback is absolute 1.0 |
| Consolidation | auto = `SupportRadius::Analyze` | Relative |

Only Density Weights changed. The other absolute fields have different semantics (Gaussian
sigma, property-value scale, radius-outlier threshold coupled to `minimum_neighbors`, PCA
radius), so the support-radius helper does not apply trivially; they are left for the operator.

**Change.**
- Owner reused: `Geometry::SupportRadius::Analyze` (canonical, contract
  `geometry.support-radius-policy`), called once from a private `Resolve` in
  `Runtime.PointAnalysisOperations.DensityWeights.cpp` on the captured live positions. The
  capture is the same `std::vector<glm::vec3>` for every domain, so mesh vertices, graph nodes
  and point clouds get one rule. No geometry-layer change.
- `support_radius` 0 is the new default and is accepted by validation; positive values are
  unchanged. `EditorDensityWeightResult::SupportRadius` reports the resolved radius; the CPU
  completion message appends it; the panel shows "Support radius R; query radius Q".
- CPU backends resolve on the job worker (`Compute`). Vulkan resolves in `Capture` before
  admission because its dispatch starts on the frame thread (a `shortcut:` comment marks it).
  Readiness does not resolve an automatic radius. Radius-independent LBVH checks and
  `AdmitPointScalarGpu` stay in `Capture`; admission and submission use the same resolved params.
- Vulkan overflow now fails with, for example (real GPU run, phase 12 of the smoke):
  "Radius candidate overflow: support radius 2 is too large (1029 candidates, capacity 1024).
  Use about 1.99, 0 for automatic, or a CPU backend." Other GPU scalar failures keep the old text.

**Defaults chosen by the agent, pending operator review (not operator decisions).**
1. The LOP-family policy row (16th neighbour, minimum rank 4, P75, x1.25) instead of the helper
   default x1.0. It matches automatic isotropic WLOP only when neither neighbour budget binds.
2. `gpu_radius_capacity` is the neighbour budget for every backend, so the radius does not
   depend on the backend.
3. Fewer than two live samples, a zero-extent set or an over-budget minimum rank fails in
   automatic mode ("Automatic support radius failed (status); set a positive support radius.").
   Behaviour change: with the default, one-sample and all-coincident inputs used to succeed
   (weight 1, or the coincident count) and now fail; readiness says Enabled for an automatic
   radius even when execution will fail.
4. Vulkan resolves on the frame thread, an exception to the policy doc's "job worker" rule.
5. Schema stays version 1 (0 used to be rejected, so no stored config changes meaning).
6. Manual radii get no sampled pre-check; the GPU complete-support check stays the exact guard.

**Verification (this session).** Builds: `ci` (IntrinsicRuntimeContractTests,
IntrinsicSandboxEditorIntegrationTests) and `ci-vulkan` (IntrinsicPointLBVHGpuTests) on an
RTX 4090.
- Failing first, before the fix, with only the result field and tests in place:
  `DensityWeightOperations.AutomaticSupportRadiusIsScaleInvariantOnMeshGraphAndCloud`,
  `DensityWeightConfig.RoundTripAndSharedPreviewApplyRun`,
  `ThreeMethods/PointScalarTransaction.AutomaticSupportRadiusResolvesBeforeVulkanSubmission/2`
  ("Support radius must be positive..."), `SandboxProcessingPanels.ReusedExecutionPanels...`
  (label and "Support radius 2;" missing), `EngineConfigSectionSchemas.StrictOwnerLiteralRanges...`
  (0 rejected), and `PointLBVHGpuSmoke.DensityWeightRejectsStaleCancellationOverflowAndSubmission`
  (old message, no "too large"). All pass after the fix.
- Mutation checks: an absolute 1.0 in place of the automatic rule fails the scale test (radius
  ratio 1 instead of 1000, maximum weight 1); skipping `Resolve` for Vulkan fails the
  transaction test (0 instead of 4.9706 radius, a submission instead of a refusal).
- Task selector (`ctest ... -R 'DensityWeight|KernelDensity|PointSpacing|OutlierAnalysis|SandboxProcessingPanels' -LE 'gpu|vulkan|slow|flaky-quarantine'`): 143/143 pass.
- Suites that run the changed default, message, schema or fixture (PointScalarTransaction,
  EditorPointReadiness, EditorQueuedPointJobs, SandboxEditorSession(Lifecycle),
  QueuedEditorJob*, RuntimeReuseDriftGuard, AgentOperations, EditorKeypointAgent,
  PointCloudConsolidationModule, EngineConfigSectionSchemas, SandboxConfigSections,
  SandboxAgentServer, Processing/EditorCompilationLocality, RuntimeRenderExtraction,
  DensityWeight*): 354/354 pass.
- `ctest --test-dir build/ci-vulkan -R 'PointLBVHGpuSmoke\.DensityWeight'`: 3/3 pass (2/3
  before the fix). Run through ctest because the fixture environment sets `detect_leaks=0`;
  the bare binary reports NVIDIA-driver allocations under `PushConstants` as leaks.
- `check_layering.py --strict` reports only its own negative fixtures under
  `tests/contract/repo/layering_fixtures`; `check_doc_links.py`: no broken links.
- jscpd (`--min-lines 5 --format cpp src/runtime`, no project config, `.cppm` not scanned):
  no clone overlaps the new `Resolve` or overflow-message lines.
- One-off engine run on the operator's mesh (`vlasic/I_crane/meshes/mesh_0060.obj`, 10002
  vertices, diagonal 2.1409) through an uncommitted gtest in `build/ci-vulkan` (RTX 4090); the
  positions went into a point-cloud entity. Predictions were written first
  (`tasks/evidence/RUNTIME-291/vlasic-engine-check-predictions.txt`); all held:
  `vulkan_lbvh` with `support_radius` 0 succeeded with radius 0.0463368, at most 99 candidates
  per row (capacity 256) and weights 1.02 to 9.68. `cpu_kdtree` resolved the same radius
  bit for bit. The old default 1.0 failed with "Radius candidate overflow: support radius 1 is
  too large (10001 candidates, capacity 256). Use about 0.0256, 0 for automatic, or a CPU
  backend." and left the weights unchanged.
- `PointLBVHGpuSmoke.KeypointVulkanComputePlanarZeroScoresAndIsotropicFloor` and
  `KeypointResidentSubnormalScores` fail in `build/ci-vulkan` (keypoint count 5 instead of 8).
  They fail the same way with this change's `src/` reverted, so they predate it and are not
  part of this task.

**Remaining.**
- The live Sandbox check (Operational) on the Vlasic mesh through the panel is not done; it
  needs the operator at the display. The engine run above used the same runtime command on a
  point-cloud entity, not the Sandbox panel on the mesh entity. Persisted Sandbox configs keep
  `support_radius` 1.0, so enter 0 for that check.
- Agent results carry only the message: for `vulkan_lbvh` the message comes from the GPU
  snapshot, so agents do not see the resolved radius there (the panel and result field do).
