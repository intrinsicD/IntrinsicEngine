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
- CPU backends resolve on the job worker (`Compute`). Vulkan resolves in `Capture` because its
  dispatch starts on the frame thread (a `shortcut:` comment marks it): radius-independent
  `AdmitPointScalarGpu` first, then the analysis, then admission of the resolved radius (review
  fix round 1). Readiness does not resolve an automatic radius. Admission and submission use the
  same resolved params.
- Vulkan overflow now fails with, for example (real GPU run, phase 12 of the smoke):
  "Radius candidate overflow: support radius 2 is too large (1029 candidates, capacity 1024).
  Use about 1.99, 0 for automatic, or a CPU backend." Other GPU scalar failures keep the old text.

**Defaults chosen by the agent, pending operator review (not operator decisions).**
1. The LOP-family policy row (16th neighbour, minimum rank 4, P75, x1.25) instead of the helper
   default x1.0. It matches automatic isotropic WLOP only when neither neighbour budget binds.
2. `gpu_radius_capacity` is the neighbour budget for every backend, so the radius does not
   depend on the backend.
3. Fewer than two live samples, a zero-extent set or an over-budget minimum rank fails in
   automatic mode with `InvalidProcessingParameters` on every backend ("Automatic support radius
   failed (status); set a positive support radius.").
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

**Review fix round 1 (after independent Claude and Codex reviews of d49d0dd85).**
- Blocking finding (Codex review): automatic Vulkan ran the KD-tree analysis on the frame
  thread before `AdmitPointScalarGpu`, so requests Vulkan refuses anyway (over 2^20 points,
  neighbour workspace over 2^24 entries, no float64 device) still paid for it. Fixed in
  `Capture`; manual radii keep their order.
- Adopted optional findings: the CPU automatic-radius failure now returns
  `InvalidProcessingParameters` like Vulkan (was `GeometryProcessingFailed`); the overflow-hint
  comment calls R*C/N unverified; `support-radius-policy.md` states the manual-density exception
  and that `gpu_radius_capacity` budgets only the automatic radius; `density-weights.md` says
  the sampled budget (at most 2048 rows) is not a guarantee on Vulkan; GPU smoke phase 12 also
  asserts "Use about 1.99," (2*1024/1029).
- Failing first (predictions in `tasks/evidence/RUNTIME-291/fix1-predictions.txt`, all held):
  with the new assertions and the old `src/`, `.../AutomaticSupportRadiusResolvesBeforeVulkanSubmission/2`
  reported "Automatic support radius failed (degenerate_neighborhood); ..." for a float64-less
  device instead of the admission text, and the scale test saw `GeometryProcessingFailed` 36
  times. Mutation: the unscaled suggestion ("Use about 2,") fails the GPU smoke.
- After the fix: `ctest --test-dir build/ci -j 4 -R '<task selector>|PointScalarTransaction|EditorPointReadiness|EditorQueuedPointJobs|SandboxEditorSession|QueuedEditorJob|RuntimeReuseDriftGuard|AgentOperations|EditorKeypointAgent|PointCloudConsolidationModule|EngineConfigSectionSchemas|SandboxConfigSections|SandboxAgentServer|CompilationLocality|RuntimeRenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine'`:
  520/520 pass (task selector alone: 143 tests). `ci-vulkan` `PointLBVHGpuSmoke.DensityWeight*`
  3/3, `KernelDensity*` and `RUNTIME298PointScalarResidency` 2/2 pass. `build/ci` was rebuilt
  after the mutation restore, so its mtimes are consistent (CompilationLocality passes).
- Not re-run: the one-off Vlasic engine check (its success path passes both admissions the
  same way; covered by the transaction test's CPU/Vulkan radius equality).

**Remaining.**
- The live Sandbox check (Operational) on the Vlasic mesh through the panel is not done; it
  needs the operator at the display. The engine run above used the same runtime command on a
  point-cloud entity, not the Sandbox panel on the mesh entity. Persisted Sandbox configs keep
  `support_radius` 1.0, so enter 0 for that check.
- Agent results carry only the message: for `vulkan_lbvh` the message comes from the GPU
  snapshot, so agents do not see the resolved radius there (the panel and result field do).
- The overflow message always offers "0 for automatic", also when the request already was
  automatic (sampled budget, denser unsampled row). Dropping it there needs the automatic flag
  in the generic scalar transaction; left as is.
- `MakeEditorDensityWeightTransactionForTest` injects values and never resolves an automatic
  radius; its Sandbox caller now shows radius 0. Harmless, not changed.

## Live check — 2026-10-24

What ran: `ExtrinsicSandbox` from `build/ci-vulkan` (head 0b51444a9, ASan/UBSan Debug) on
DISPLAY=:1, RTX 4090, driver 580.159.04. `device_status` reported Vulkan operational with 0
validation errors at the start and the end of every session. The working directory was
`tasks/evidence/RUNTIME-291/live/`. It held the client scripts, predictions and build logs but no
`config/engine.json`; `config_get` returned the section defaults, so the reference config was used.
The Sandbox ran with `--agent-socket` and `--agent-root` for the Vlasic data. A throwaway socket
client (`live/agent_client.py`) drove it through the agent tools (`import_file`, `config_apply`,
`run_operation density_weight`, `property_*`, `view_capture`). These use the same editor command
layer as the panel. For Vulkan the panel opens a manual-accept transaction
(`StartEditorDensityWeightTransaction`), while `run_operation` accepts automatically. Both paths
share `Capture`, `Resolve` and `AdmitPointScalarGpu`. The ImGui panel was not clicked.

No raw file saved the literal argv, environment or per-session `ASAN_OPTIONS`/`LSAN_OPTIONS`.
What is recorded:
- the setup prose in `predictions.txt`;
- the socket paths in each `sandbox.log` (`live/agent.sock`, `live/s2/agent.sock`,
  `live/s3/agent.sock`);
- the leak settings named in the prediction addenda: session 1 `suppressions=lsan.supp`, session 2
  that plus `fast_unwind_on_malloc=0`, session 3 `detect_leaks=0`.

A rerun should save the command lines.

Meshes: `I_crane/meshes/mesh_0060.obj` and `D_march/meshes/mesh_0060.obj`. D_march is the path in
the lane brief. Both have 10002 vertices, were imported as mesh entities and got their weights on
`MeshVertex`. The diagonals 2.1409 (I_crane) and 2.2693 (D_march) and all neighbour counts come from
the SciPy mirror (`repro_vlasic_lop125.log`, `live/mirror_dmarch_lop125.log`), not from engine
output. That I_crane is the operator's mesh is an inference from matching numbers. The operator gave
a diagonal of 2.14 and 49 neighbours at radius 0.03. I_crane matches both; D_march has 35
neighbours.

Predictions were written first in `live/predictions.txt`. The file was created at 04:30:03, and the
Sandbox started at 04:30:40. The file is git-ignored. It was last modified at 04:37:06, after the
runs, when the P6b/P6c addenda and a timestamp correction were appended. Nothing pins the text from
before this commit. From this commit on, two sha256 hashes pin it:
- lines 1-33 (the pre-run section, P0-P6):
  `e15d249ea14eda254ee3d1327b619d542471a3cf4ce8d1887fd255ae89c83c55`;
- the whole file: `3bd41d85f08c68ab93ba65a22dc64513e7af8f5c0b38eee152ff9c313356f0fb`.

Session 1 ran the full sequence. Sessions 2 and 3 repeated it and gave identical density values.

| Prediction (from `predictions.txt`) | Observed | Outcome |
| --- | --- | --- |
| P0: default `support_radius` 0, backend cpu_kdtree, capacity 256 | 0, cpu_kdtree, 256 | held |
| P1: each import creates one mesh entity with 10002 vertices | 10002 and 10002 | held |
| P2: `vulkan_lbvh` with radius 0 succeeds with no overflow; 10002 finite weights; 1 <= min and max <= mirror max + 1 (100 / 86); I_crane min and max within 1% of 1.0206 and 9.677 | Applied. I_crane 1.0206 to 9.6770, D_march 1.0149 to 6.0159, 10002 finite each | held |
| P2: the Vulkan reply does not state the radius | "Device scalar accepted; input upload: 120024 bytes; ..." on both meshes | held (known gap) |
| P3: `cpu_kdtree` with radius 0 ends "support radius 0.04634." (I_crane) and "0.04749." (D_march); Vulkan vs CPU max abs error <= 1e-4 | Both messages as predicted; 10002/10002 rows identical, max error 0, on both meshes | held |
| P4: `vulkan_lbvh` with radius 1.0 fails with "Radius candidate overflow: support radius 1 is too large (10001 candidates, capacity 256). Use about 0.0256, 0 for automatic, or a CPU backend."; generation and statistics unchanged | That exact text, status GeometryProcessingFailed, on both meshes. Generation (session 1: I_crane 4785, D_march 6399), statistics and CPU equality unchanged | held |
| P5: `view_capture` writes a PNG inside `live/` | `live/icrane_density_weight_vulkan_auto.png`, 1600x924 | held |
| P6 (before session 1): after WM_DELETE_WINDOW the process exits with 0 | session 1: `sandbox.exit` = 1 | **not held** |
| P6b (before session 2): with `fast_unwind_on_malloc=0`, every leak matches `lsan.supp` and the exit status is 0 | session 2: `s2/sandbox.exit` = 1; 3 x 80 B not suppressed | **not held** |
| P6c (before session 3): with `detect_leaks=0`, exit 0 and the same close line | session 3: `s3/sandbox.exit` = 0; close line present | held |

All density-weight predictions (P0-P5) held. The shutdown predictions under LeakSanitizer (P6, P6b)
did not. Only P6c held, and it holds because leak detection was switched off.

Shutdown and LeakSanitizer, from the raw `live/sandbox.log`, `live/s2/sandbox.log` and
`live/s3/sandbox.log`:
- In all three sessions, the WM_DELETE_WINDOW sent to the Sandbox's own window took the clean path
  ("Window close requested; stopping Engine::Run loop. source=platform-event").
- Session 1 (default unwinding) reported 116338 B in 25 allocations. Every stack has one frame after
  the allocator, so `lsan.supp` matched nothing:
  - 18 x calloc (113760 B) and 3 x malloc (240 B) from an unknown module;
  - 408 B from `_XimOpenIM` (libX11);
  - 184 B direct and 1746 B indirect from libdbus.
- Session 2 (`fast_unwind_on_malloc=0`) reported the same 25 allocations. `lsan.supp` matched 22 of
  them:
  - 18 (113760 B) to `VulkanCommandContext::PushConstants`;
  - 3 (1930 B) to `dbus_connection_send_with_reply_and_block`;
  - 1 (408 B) to `_XimRegisterIMInstantiateCallback`.
- The three direct leaks of 80 B that remain look alike. In each, `malloc` is called from an unknown
  module, and the calling PC ends in `0d1` (`0x7a8d61a0c0d1` in session 2, `0x78be09c0c0d1` in
  session 1). Five or six more unknown-module frames follow. Then the stack enters
  `VulkanDevice::CreatePipeline` (`Backends.Vulkan.Device.cpp:5455`, `:5618`) through
  `PipelineManager::Create`. The pipelines come from `CullingSystem::Initialize`
  (`Graphics.CullingSystem.cpp:379`) and `Renderer::InitializeOperationalPassResources`
  (`Graphics.Renderer.cpp:6335`, `:6891`). The stacks go on through `RebuildOperationalResources`,
  `Engine::RunFrame` and `main`. These are startup pipelines, not the density path.
- Session 3 (`detect_leaks=0`) printed no report and exited with 0.
- Comparison with [BUG-180](../bugs/BUG-180-framed-icp-leak-enabled-process-retention.md): its
  2026-10-24 diagnosis is commit 9396405a5 on `claude/night-2026-10-24-bug-sweep`, which is not on
  this branch. It concerns the framed ICP gtest. It attributes 3 x 80 B to `malloc` called from
  `libnvidia-glcore.so.580.159.04+0xa0c0d1`, during pipeline creation from a filled NVIDIA shader
  disk cache. Its call sites are the same three lines: `Graphics.CullingSystem.cpp:379`,
  `Graphics.Renderer.cpp:6335` and `:6891`. Those files are unchanged between that diagnosis's
  `599f02ff1` and 0b51444a9.
- What matches: driver version, block size and count, the three call sites, and the low 12 bits of
  the calling PC (`0d1`). This does not establish identity. In this run the module was not mapped
  (no `LD_DEBUG=files`, no pinned glcore), the shader-cache state was not recorded, and no api_dump
  of pipeline destruction exists.
- One difference: BUG-180's Sandbox control (shutdown-contract environment, 5 frames) stayed clean
  with a filled cache, but this longer Sandbox session reports the three blocks. So this run is one
  more observation of a BUG-180-like signature, now in the Sandbox. It is not a classification. No
  suppression was added.

Other observations:
- Radius visibility on Vulkan: the agent reply omits the resolved radius (see Remaining). Code
  reading shows that `FoldGpu` does not overwrite `Result.SupportRadius`. The result field and the
  panel line therefore keep the value that `Capture`/`Resolve` set; this run did not observe that.
  The earlier engine check read 0.0463368 directly from the Vulkan result. In this run, the Vulkan
  radius shows only indirectly, through the rows that are identical to CPU.
- Persisted configs: this run used the reference config. A Sandbox with a config saved before this
  task keeps `support_radius` 1.0 (schema stays version 1, decision 5; see Remaining). There the
  original overflow comes back until 0 is entered.
- Viewport capture (`live/icrane_density_weight_vulkan_auto.png`): the legend strip is Viridis
  (dark purple, teal, yellow, range 1.23 to 4.73). The mesh shows magenta, pale cyan and white, with
  a little yellow at silhouettes. The hues do not match, so this is more than the mesh being
  brighter. Not investigated, and outside this task.
- Each overflow run logs `[JobService] Job 'Device point scalar' completion publisher rejected
  result type ... bool; completion dropped.` at error level. The GPU smoke log from before the fix
  (`prefix-gpu-densityweight-ctest.log`, old message) has the same line. So it predates this task,
  and no task tracks it.
- All three sessions log `VulkanRequestedButNotOperational status=RequestedButIncompleteGate
  reason=BarrierValidationFailed` at cold start. The preceding log line describes that state: the
  backend stays fail-closed until the first clean default-recipe validation. `device_status`
  reported operational afterwards.

Open for the operator (no task file created here): ownership of the JobService error line, of the
Sandbox 3 x 80 B observation (BUG-180 or a new task), and of the viewport hue mismatch.

Still missing for Operational (the reviewers decide whether this run counts):
- The panel path itself: `StartEditorDensityWeightTransaction` with manual Accept/Discard and the
  panel line "Support radius R; query radius Q" on Vulkan. The agent's `run_operation` auto-accepts
  through `ApplyEditorDensityWeightCommand`. Only the `SandboxProcessingPanels` contract tests
  cover the panel.
- No committed `gpu;vulkan` test runs the Vlasic mesh. The GPU smoke `PointLBVHGpuSmoke.DensityWeight*`
  covers the overflow message on synthetic data.
