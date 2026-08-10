---
id: METHOD-037
theme: I
depends_on: [GEOM-058]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-10T14:15:44Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
contract_review: "The task adds a reusable mesh-topology method surface, publishes same-cardinality face/edge properties consumed by visualization, and binds it through runtime config and Sandbox UI. geometry.parameterization-optimization does not apply because topology cutting and UV optimization are explicitly deferred."
maturity_target: Operational
---
# METHOD-037 — Signed-principal-curvature mesh segmentation

## Status

- Completed and retired on 2026-08-10 at `Operational` maturity for the CPU
  runtime/config/UI/editor path.
- Implementation commit: `be22bb4560f0a46227400d84971c847348b08940`.

## Goal
- Decompose an oriented surface mesh into spatially coherent, connected regions of near-constant signed principal curvature using the existing deterministic Gaussian-mixture fitter, publish inspectable face-region and edge-boundary properties, and expose the complete CPU-reference method through shared config/agent and Sandbox UI controls.

## Non-goals
- No mesh cutting, vertex duplication, seam materialization, chart parameterization, or UV-atlas regeneration in this task; those open under `GEOM-076` only after segmentation-quality evidence is acceptable.
- No new Gaussian-mixture implementation, generic clustering framework, service, registry, backend abstraction, optimized CPU path, or GPU path.
- No claim that the first local spatial optimizer is globally optimal, scale-independent on every curvature estimator, or suitable as an atlas seam policy without downstream evaluation.
- No hard dependency on the unfinished `GEOM-071` sharp-feature classifier. This method derives soft dual-edge weights from its own signed-curvature discontinuity and adjacent-face normal angle; adopting canonical hard feature masks is a later compatibility change if `GEOM-071` lands.

## Context
- Owner/layer: the deterministic CPU kernel and diagnostics live in `src/geometry` (`geometry -> core`); runtime owns ECS binding, validated hot config, property publication/dirty propagation, and editor commands; app owns only the Sandbox controls.
- The closest published family combines curvature-tensor descriptors with spatially coherent mesh segmentation (Lavoué, Dupont, and Baskurt, 2005) and constant-curvature proxy fitting/region growth. Intake under `methods/geometry/curvature_segmentation/` freezes the exact repository formulation and distinguishes it from those sources rather than claiming a paper-faithful reproduction.
- `Geometry.GaussianMixture::FitEM` already supplies deterministic seeded EM, k-means++ initialization, posterior responsibilities, covariance flooring, and convergence diagnostics. Signed `(k1, k2)` occupy the first two coordinates of its 3D sample record; the third coordinate is fixed and covariance-regularized. Spatial position is deliberately excluded from the statistical feature vector and enters only through mesh-dual regularization.
- User-confirmed selection policy: UI/config support both a fixed component count and automatic bounded model selection. Fixed mode fits exactly the requested mixture count; automatic mode evaluates a bounded count range using curvature-fit tolerance plus an explicit complexity penalty/information criterion.
- User-confirmed first slice is non-destructive: cluster/region labels and boundary flags/colors are published for inspection; cuts and UV-atlas consumption wait for quality evidence.
- Right-sizing: add one focused geometry module with plain params/result records and free functions, then extend the existing geometry-processing/config/visualization paths. Reuse the current GMM, property catalog, visualization lanes, job/command seams, and color publication conventions.

## Control surfaces
- Config: schema-versioned `sandbox.curvature_segmentation` persists selection mode, fixed/min/max component counts, automatic fit tolerance and complexity weight, EM controls, spatial regularization, feature sensitivity, cleanup threshold, and seed.
- UI: the existing Mesh / Processing surface presents the same validated draft, supports Fixed count and Automatic selection, runs the configured method, displays diagnostics, and can show face-region colors together with boundary-edge colors.
- Agent/CLI: the same typed config record, codec, preview/validate/apply registration, and configured request drive programmatic execution; there is no UI-only algorithm state.

## Backends
- Backend axis: CPU reference only. An optimized CPU or GPU backend opens only after the reference, correctness tests, smoke benchmark, and visual quality evaluation justify it.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | An oriented halfedge surface mesh with live faces, vertex positions, and face adjacency; faces/topology are semantic inputs. Signed per-vertex principal curvatures are computed through the existing curvature implementation and aggregated onto live faces. |
| Compatible entity sources | Mesh provenance only, because surface faces and face adjacency are required. Eligibility uses the canonical mesh topology/property preflight, not a UI-local provenance shortcut. |
| RuntimeModule | Extend the existing editor geometry-processing operation with one configured CPU request; do not add a service, queue, registry, or backend interface. |
| Config/agent | Add one schema-versioned config section and reuse `EngineConfigControl` preview/validate/apply/source-parity behavior. |
| UI | Add controls and result diagnostics to Mesh / Processing; execution routes through the same configured request. Cluster and boundary visualization routes through existing surface/edge visualization commands. |
| Publication | Same-cardinality publication only: `f:curvature_component`, connected `f:curvature_region`, deterministic `f:curvature_region_color`, `e:curvature_region_boundary`, and `e:curvature_region_boundary_color`; preserve topology and unrelated properties. |
| End-to-end tests | Geometry fixtures, config source parity, runtime publication/dirty propagation, Sandbox presentation/submission, and simultaneous surface/edge visualization. |

## Required changes
- [x] Complete paper intake and freeze the objective: robustly normalized signed face `(k1, k2)` data terms from deterministic GMM responsibilities plus a feature-weighted Potts term on the face-dual graph; document orientation/sign assumptions and local-optimizer limitations.
- [x] Add `Geometry.HalfedgeMesh.CurvatureSegmentation` as an interface/implementation-unit pair with validated params, explicit status/diagnostics, deterministic GMM fitting, fixed and automatic model selection, bounded deterministic spatial optimization, connected-component relabeling, small-region cleanup, and boundary extraction.
- [x] Make the automatic selector evaluate a bounded component range, record every candidate's likelihood/criterion/fit residual, prefer candidates satisfying the configured curvature tolerance, and fail closed rather than silently substituting invalid parameters or non-finite curvature.
- [x] Publish component labels separately from connected region labels so repeated curvature regimes in disconnected patches remain statistically identifiable while downstream cuts receive connected pieces.
- [x] Materialize the canonical face/edge output properties and deterministic colors through the runtime operation, with content revisions and renderer dirty markers observing the mutation.
- [x] Add and register the schema-versioned runtime config lane; serialize, validate, round-trip, hot-apply, and form configured requests from all supported control sources.
- [x] Extend Mesh / Processing with Fixed/Automatic controls, validated Apply/Reload/Run behavior, diagnostics, and cluster-plus-boundary visualization through existing face/edge lanes.
- [x] Add a declared deterministic smoke benchmark that records runtime and quality/diagnostic metrics without making a performance or atlas-quality claim.

## Tests
- [x] Geometry unit fixtures: plane, sphere, cylinder, saddle/hyperbolic patch, joined constant-curvature patches, a sharp fold, disconnected equal-curvature components, non-triangle faces, boundaries, degenerate/non-finite input, invalid params, determinism, and global orientation reversal.
- [x] Prove fixed mode fits the requested feasible component count; automatic mode chooses the expected bounded count on synthetic separated curvature populations and reports tolerance fallback when none qualifies.
- [x] Prove increasing spatial weight reduces label disagreement on smooth dual edges; feature weighting preserves a known fold boundary; connected region IDs are contiguous and each region is dual-connected.
- [x] Assert exact face/edge property cardinality, semantic boundary flags, deterministic colors, preservation of topology/unrelated properties, revision changes, and stale-source/undo behavior required by the existing editor operation.
- [x] Cover config schema/defaults/invalid ranges and Editor/AgentCli/Programmatic source parity.
- [x] Cover Sandbox availability, both selection modes, configured request submission, result reporting, and simultaneous face-region/edge-boundary visualization commands.

## Docs
- [x] Add `methods/geometry/curvature_segmentation/{method.yaml,paper.md,README.md}` with stable citations, exact objective, diagnostics, complexity, parameter guidance, and limitations.
- [x] Update geometry, runtime/config, Sandbox, visualization, and parameterization-roadmap current-state docs; record `GEOM-076` as the evidence-gated seam/cut/atlas consumer.
- [x] Regenerate `docs/api/generated/module_inventory.md`, `tasks/SESSION-BRIEF.md`, and any method/benchmark inventories required by validators.

## Acceptance criteria
- [x] On deterministic analytic fixtures, the result separates distinct signed curvature regimes, remains spatially coherent on smooth faces, aligns the known fold boundary under feature weighting, and emits connected region labels with exact boundary flags.
- [x] Both Fixed and Automatic modes are selectable through config, agent/programmatic sources, and UI using one validated apply/execute path.
- [x] The editor visibly renders deterministic per-face region colors and boundary-only per-edge colors without changing mesh topology.
- [x] Diagnostics expose GMM convergence, candidate selection, curvature fit, spatial energy/moves, component/region/boundary counts, invalid inputs, and local-optimum limitations.
- [x] CPU correctness, config/runtime/UI contracts, smoke benchmark validation, strict structure/docs/layering checks, and claim-grade fixed-surface review/custody pass before retirement.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureSegmentation|SandboxCurvatureSegmentation|CurvatureSegmentationConfig' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/generate_session_brief.py
python3 tools/agents/workflow_evidence.py validate --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Reimplementing GMM/EM, introducing a clustering service/framework, or embedding world-space coordinates into the signed-curvature statistical feature vector.
- Publishing only a visual color without semantic component/region/boundary properties.
- Treating an internal region boundary as an actual UV seam or replacing topology in this task.
- Adding an optimized/GPU backend or atlas-quality claim before CPU-reference evidence.

## Maturity
- Target: `Operational` through the real CPU runtime/config/UI/visualization path.
- This task closes the non-destructive segmentation method only. `GEOM-076` owns any later topology cuts and UV-atlas chart-hint adoption after evidence review.
- Reached: `Operational` through the CPU geometry kernel, runtime publication,
  shared config lane, Sandbox controls, and simultaneous face/edge
  visualization. This does not claim an operational promoted-Vulkan backend.

## Verification evidence

- Implementation commit: `be22bb4560f0a46227400d84971c847348b08940`.
- The focused curvature-segmentation tests and the complete default
  CPU-supported CTest gate passed in `build/ci` with Clang 23; command receipts
  are under `tasks/evidence/METHOD-037/commands/`.
- A real Sandbox run imported `tests/data/sculpt.obj` (3,669 vertices and 7,342
  faces), computed principal curvature, and ran fixed-count segmentation. The
  result reported six selected components, five active components, and 752
  boundary edges while visibly rendering deterministic per-face region colors
  together with boundary-only edge colors. The captured editor view is
  `tasks/evidence/METHOD-037/previews/sandbox-sculpt-curvature-segmentation.png`.
- The visual run exercised the CPU runtime/config/UI/editor path. Promoted
  Vulkan was requested but remained non-operational after barrier validation,
  so this evidence deliberately makes no Vulkan-operational claim. The
  LeakSanitizer shutdown hang remains owned by `BUG-118`.
- The local benchmark result is a sealed, non-claim-eligible reference smoke;
  it supports only the frozen-fold fixture's correctness/diagnostic gate and
  makes no throughput, atlas-quality, optimized-backend, or GPU claim.
- Strict method/benchmark/task, layering, test-layout, documentation-link,
  generated-inventory, workflow-evidence, and experiment-custody validators
  passed. The final fixed surface and portable bundle received independent
  claim-grade acceptance under `tasks/evidence/METHOD-037/`.
