---
id: METHOD-046
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Operator-directed interactive CPU experiment with retained controls and actual Claude review; no publication custody."
owner: codex
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T02:48:49+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
contract_review: "Oriented embedded triangles and source-bound face regions/charts are semantic inputs. Original geometry and region labels remain immutable; output is detached chart labels and corner UVs."
---
# METHOD-046 — Move additional UV borders toward feature evidence

## Goal
- Implement and evaluate a bounded boundary relocation after METHOD-045, planned and reviewed with Claude, while retaining original sculpt borders and the existing UV quality limits.

## Context
- The operator explicitly requested collaborative planning, implementation and renewed review on 2026-09-08, directing this research outside the standing product-convergence focus. C73 identifies earlier sculpt hard-border loss during merging and frog placement problems before merging.
- Claude proposed binary band min-cuts instead of single-face descent. Adopt the edge-weighted Potts formulation from Boykov, Veksler and Zabih (2001), [original paper](https://www.cs.cornell.edu/~rdz/Papers/BVZ-PAMI01.pdf). This is a constrained two-label subproblem, not the paper's complete multi-label algorithm or its approximation guarantee.
- [Sharp and Crane (2018)](https://www.cs.cmu.edu/~kmcrane/Projects/VariationalCuts/) provides the relevant continuous distortion-aware cutting alternative; this experiment remains on mesh edges and reuses explicit LSCM validation rather than implementing that formulation.

## Frozen experiment
- Hypotheses, ranked: collective moves can escape jagged single-face minima; curve support helps beyond shortening; topology/distortion constraints may reject the useful collective moves. Preserve rejected outcomes.
- Minimize sum of seam edge length times `(1 - beta * support)`, beta 0.8, support in [0,1]. Count chart borders AND internal UV cuts for final acceptance. Require strict full-energy decrease; the graph cut optimizes only the border surrogate.
- Freeze a six-dual-hop band around each initial same-region chart-pair seam. Freeze all faces outside it and existing hard-feature seam endpoints. Also freeze global chart interiors deeper than six hops from every original seam; charts without such an interior are frozen as a whole. Skip pairs without a fixed core on each side. Three deterministic rounds maximum. Original region membership, chart count and chart cores remain fixed.
- This tightens Claude's current-seam band to an initial-seam band so multiple rounds cannot drift arbitrarily. No chart centers need moving. No area penalty, turning term or new framework.
- Controls: protected reference; beta-zero shortening; native soft guidance; max(native soft, principal curve support); fixed-seed shuffled combined guidance. Use the same large detector scale (index 2) on both meshes, avoiding shape-specific scale choices. Curves contribute confidence times absolute tangent cosine times surface-local distance falloff over their recorded radius. Detector scores are not probabilities.
- Exact frog/sculpt source and METHOD-045 labels; analytic folded sheet with known crease, fixed-core/region/hard constraints, binary-cut brute-force oracle, disconnected-sheet evidence isolation, and internal-cut accounting. Audit and native-pack every cohort arm independently. No parameter sweep or default promotion based on the guidance's own score.

## Acceptance criteria
- [x] Deterministic CPU reference moves boundaries with explicit topology/UV rejection; controls pass.
- [x] Matched arms, intermediate accepted moves, scores, baseline-border retention, distortion and packing measurements are inspectable.
- [x] Actual Claude review of final implementation and aggregate results, with findings resolved or explicitly bounded.
- [x] Focused verification, source-bound report, and ARA outcome record.

## Engine integration
| Field | Decision / owner |
| --- | --- |
| Least-structured input | Embedded oriented triangle surface with face regions/chart labels and source-bound feature evidence. |
| Compatible entity sources | Any mesh satisfying this typed topology contract; no point/graph provenance restriction beyond required faces. |
| RuntimeModule | Offline experiment; METHOD-044 owns runtime adoption after quality acceptance. |
| Config/agent | Serializable offline CLI parameters; METHOD-044 owns shared production apply path. |
| UI | Interactive diagnostic viewer; METHOD-044 owns editor integration. |
| Publication | Source geometry/regions unchanged; detached face chart labels and corner UVs. |
| End-to-end tests | Source-bound inputs through refinement and native packing; METHOD-044 owns ECS publication. |

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicUvChartPackDiagnostic
OPENBLAS_NUM_THREADS=1 python3 tests/regression/tooling/Test.AtlasBoundaryRefine.py
OPENBLAS_NUM_THREADS=1 INTRINSIC_TEST_NATIVE_ATLAS=1 python3 tests/regression/tooling/Test.BaselineAtlas.py
ctest --test-dir build/ci --output-on-failure -R 'Uv|Parameterization' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

## Implementation corrections during controls
- The first frog run exposed consumption of a pair-local core by moves involving another pair. Global immutable cores now persist across the full run; a three-chart regression checks this explicitly. The failed attempt remains in the evidence history.
- Internal cut accounting compares both edge endpoints and is independently reconciled against corner-UV discontinuities. A cylinder control checks the complete cut chain.

## Result and review
- The bounded implementation and renewed actual Claude review are complete; [report](../../docs/methods/uv_boundary_refinement_experiment.md), [source-bound record](../../ara/evidence/diagnostics/method046/record.json), C74.
- All original region labels and borders remain fixed. Frog extra seams shorten, with distinct native/curve/length/packing tradeoffs; sculpt changes twelve faces along an extra chart border and retains its distortion quality, with a small square-occupancy decrease. No anatomical/default acceptance is inferred.
- Review findings added per-move area ratios, fixed-face contact counts, failed-chart IDs, wide-range capacity and evidence-field tests. Exact replay preserves all labels, UVs and accepted stages across the ten final cells.
- Verification: twelve new and ten existing Python controls; ten packed cells; ci build; 136 focused CTest entries and 4308 full CPU entries with zero failures, one expected unsanitized leak-check skip; all 185 browser modes. No GPU-method or sanitizer result claimed.
- Completed 2026-09-08 in implementation commit `910a1ed36b32dad5d8a6491519a49cd7f1deca77`, published to `origin/main`. Production adoption remains with METHOD-044; remaining quality investigations stay with METHOD-045 and anatomical-method work with METHOD-043.

## Maturity
- Reached `CPUContracted` for the offline diagnostic reference, with the reported controls and source-bound evidence. This closes the bounded experiment only.
- `Operational` owned by `METHOD-044` after quality acceptance; no production backend or anatomical-quality verdict is implied.

## Completion
- Completed: 2026-09-08.
- PR/commit: `910a1ed36b32dad5d8a6491519a49cd7f1deca77` (verified on `origin/main`).
