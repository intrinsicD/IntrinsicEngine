# Signed-Curvature Mesh Segmentation

Method ID: `geometry.curvature_segmentation`. Status: **reference**.

This package records the CPU-reference contract for non-destructive mesh
segmentation from signed principal curvatures. It is informed by published
curvature-tensor mesh segmentation, Gaussian-mixture EM, BIC, and spatial
labeling work, but the exact combined objective is repository-specific. See
[`paper.md`](paper.md) for the citations and frozen equations.

METHOD-038 retired at `CPUContracted` as an evidence-only intake. Its
primary-source equations, scale convention, projected metrics, candidate
killing order, profiling lanes, and validated analytic controls are in
[`feature_aligned_intake.md`](feature_aligned_intake.md). It selected no v2
backend and changed no production default. METHOD-039 owns the practical
feature-network patch method that may consume these immutable controls.

METHOD-039 Slice A now freezes that follow-up's equations, numeric screening
parameters, fail-closed supplied-evidence shape, and seventeen generated oracle
fixtures in [`feature_aligned_intake.md`](feature_aligned_intake.md). The
executable catalog is
[`Test.CurvaturePatchContract.cpp`](../../../tests/unit/geometry/Test.CurvaturePatchContract.cpp).
This is a pre-implementation contract: the computed feature detector,
grow/merge solver, `cpu_reference_v2` selector, publication properties, config,
runtime, and UI are not implemented yet.

## Implemented path

`Geometry.HalfedgeMesh.CurvatureSegmentation` computes or accepts signed
per-vertex `(k1,k2)`, robustly normalizes face averages, fits the existing
deterministic Gaussian mixture, and minimizes a feature-weighted Potts energy
on the face-dual graph. It supports:

- **Fixed count:** fit exactly the requested feasible number of mixture
  components.
- **Automatic:** fit a bounded count range, prefer candidates meeting a
  normalized curvature-fit tolerance, and choose by a weighted two-dimensional
  BIC criterion.

The method publishes statistical component labels separately from connected
region labels. This matters when disconnected surface pieces share the same
curvature regime: they can keep one mixture component while receiving distinct
region IDs.

## Runtime and UI

The schema-versioned `sandbox.curvature_segmentation` config section is the
single control lane for config files, editor, agent/CLI, and programmatic calls.
The Sandbox Curvature window exposes Fixed/Automatic selection, model-fit,
spatial, cleanup, and deterministic EM controls. Running it writes these
same-cardinality properties without changing topology:

| Domain | Property | Meaning |
| --- | --- | --- |
| Face | `f:curvature_component` | selected GMM component label |
| Face | `f:curvature_region` | contiguous dual-connected region ID |
| Face | `f:curvature_region_color` | deterministic opaque region color |
| Edge | `e:curvature_region_boundary` | nonzero exactly across different region IDs |
| Edge | `e:curvature_region_boundary_color` | opaque red on boundaries, transparent elsewhere |

“Show result” enables both the face-color surface and boundary-only edge
visualization. The operation is undoable, preserves unrelated properties, and
rejects stale source geometry before writeback.

## Diagnostics

The result reports every automatic candidate, selected/fitted/active component
counts, GMM convergence and regularization, signed-curvature centers/scales and
fit residual, ICM energy/iterations/moves, small-region merges, connected-region
and boundary counts, explicit validation/failure status, per-candidate GMM fit
duration, and wall-clock timings for face aggregation, GMM fitting, unary and
dual-graph construction, spatial optimization, connectivity/publication,
curvature estimation, and total work. Timings are observational and never
affect the deterministic solve. The UI still reports the METHOD-037 result
contract and states that spatial optimization is local.

## Verification and scope

- Correctness: `tests/unit/geometry/Test.CurvatureSegmentation.cpp`
- METHOD-039 frozen oracle/preflight contract:
  `tests/unit/geometry/Test.CurvaturePatchContract.cpp`
- Runtime publication/history: `tests/contract/runtime/Test.CurvatureSegmentationOperations.cpp`
- Config source parity: `tests/integration/runtime/Test.SandboxConfigSections.cpp`
- UI/visualization contract: `tests/integration/runtime/Test.SandboxCurvatureSegmentationPanel.cpp`
- Smoke benchmark: `benchmarks/geometry/manifests/curvature_segmentation_reference_smoke.yaml`
- Opt-in supplied-curvature baseline profiles:
  `curvature_segmentation_reference_profile_{10k,100k,1m}_{fixed,automatic}.yaml`.
  The 10k cohort is the bounded screening default; 100k/1M require the
  explicit heavy cohort switch and experiment custody. Each profile publishes
  `population_count` and gates it to exactly two for the preregistered
  two-regime fixture.
- Opt-in 10k fixture profiles:
  `curvature_segmentation_reference_profile_10k_{cold,reuse}_{fixed,automatic}.yaml`
  and
  `curvature_segmentation_reference_remeshing_10k_{fixed,automatic}.yaml`.
  The descriptor pair compares cold `ComputeAndSegment` with segmentation from
  one reusable curvature field on alternate triangulations of an analytic unit
  sphere. The remeshing pair reports label error and a continuous planar
  boundary-distance upper bound against the exact `x=0.5` reference line.
  These are bounded Slice A fixtures, not the full analytic corpus or a
  refinement-convergence result.
- Opt-in fold contract controls:
  `curvature_segmentation_screening_fold_controls.yaml`. The screening cohort
  checks paired diagonals of isometric `30/45/60`-degree folds against the
  shared strict `angle > 45 degrees` feature classifier, verifies the
  flat/fold edge-length correspondence, and records that the constant supplied-
  curvature v1 negative control remains one region with no boundary. It does
  not execute or select candidate A-D; the disjoint cheapest-screen parameters
  are frozen in `feature_aligned_intake.md`. The accepted non-claim custody
  control reported exact `0/0/24` feature counts on both diagonals, zero mask
  error, `0.000000038` maximum normalized edge-length delta, and exact v1 flat-
  control payload parity (ARA C42).
- Opt-in cylinder/smooth-transition contract controls:
  `curvature_segmentation_screening_surface_controls.yaml`. The separate
  `surface_controls` cohort checks two rigidly phase-shifted triangulations of
  the declared open cylinder and two diagonal triangulations of the smooth
  graph `z=0.5(1+tanh(x/0.08))`. It validates orientation, analytic geometry
  and supplied-curvature contracts, zero hard-feature classifications, the
  one-region cylinder v1 negative control, and the fixed-two v1 comparison to
  the exact smooth `x=0`, `z=0.5` reference curve. It is fixture/oracle work
  only and neither executes nor selects candidate A-D.

The historical non-claim-eligible
[`METHOD-038` scratch replay](../../../tasks/evidence/METHOD-038/superseded/20260810-fixture-cohort-runner-change/experiment/protocol.yaml)
selected four Automatic components on the paired 10k fixture, failing the
exact-two gate while passing the `0.02` label-error bound; the matched Fixed
control selected two. The independent
[`audit`](../../../tasks/evidence/METHOD-038/superseded/20260810-fixture-cohort-runner-change/experiment/runs/scratch-002/audit.json)
rejected claim authorization as intended. This is bounded negative evidence
(ARA C40), not a reason to retune or change the production default.

The historical non-claim-eligible
[`scratch-003` protocol](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-controls/experiment/protocol.yaml)
extends that screen across the new fixture lanes. Fixed selected the expected
two planar-transition components and one sphere component in both cold and
reusable lanes; its maximum label error was `0.005`, and the planar continuous-
boundary upper bound was `0.007071061` against the frozen `0.02` tolerance.
Automatic selected four components in all three lanes and reached `0.656`
maximum label error on the sphere pair. The independent
[`audit`](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-controls/experiment/runs/scratch-003/audit.json)
accepted the bundle derivation while leaving `claim_authorized: false` (ARA
C41). This is a bounded negative fixture result, not a curved-surface geodesic,
refinement-convergence, performance, or candidate-v2 result.

The final checkpoint-3 non-claim-eligible
[`scratch-006` fold protocol](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-final-task-bound/experiment/protocol.yaml),
[`sealed result`](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-final-task-bound/experiment/inputs/fold_controls_benchmark_result.json),
[`bundle`](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-final-task-bound/experiment/runs/scratch-006/bundle.yaml),
and independent
[`audit`](../../../tasks/evidence/METHOD-038/superseded/20260811-fold-screening-final-task-bound/experiment/runs/scratch-006/audit.json)
remain fixture/oracle integrity evidence only. The earlier `scratch-004`
schema rejection and accepted pre-status `scratch-005` replay are preserved
separately; neither historical run is reinterpreted against the final task
hash.

The accepted pre-status checkpoint-4 non-claim-eligible
[`scratch-007` surface-control protocol](../../../tasks/evidence/METHOD-038/superseded/20260811-surface-screening-task-status-advance/experiment/protocol.yaml),
[`sealed result`](../../../tasks/evidence/METHOD-038/superseded/20260811-surface-screening-task-status-advance/experiment/inputs/surface_controls_benchmark_result.json),
[`bundle`](../../../tasks/evidence/METHOD-038/superseded/20260811-surface-screening-task-status-advance/experiment/runs/scratch-007/bundle.yaml),
and independent
[`audit`](../../../tasks/evidence/METHOD-038/superseded/20260811-surface-screening-task-status-advance/experiment/runs/scratch-007/audit.json)
passed all seventeen frozen gates. Both cylinder phases recorded zero hard
features, exact v1 payload parity, at most `0.000000008` normalized radial
error, and at most `0.000000015` paired-edge delta. Both smooth diagonal phases
recorded zero hard features, zero face-label and boundary-mask error, the exact
24-edge reference mask with two endpoints and no junction, and a normalized
sampled symmetric-distance upper bound of `0.000049835` (ARA C43). This remains
fixture/oracle integrity evidence only; custody retains
`claim_authorized: false`.

The final retirement-task-bound
[`scratch-011` protocol](../../../tasks/evidence/METHOD-038/experiment/protocol.yaml),
[`sealed result`](../../../tasks/evidence/METHOD-038/experiment/inputs/surface_controls_benchmark_result.json),
[`bundle`](../../../tasks/evidence/METHOD-038/experiment/runs/scratch-011/bundle.yaml),
and independent
[`audit`](../../../tasks/evidence/METHOD-038/experiment/runs/scratch-011/audit.json)
repeat the same scientific row after the task split and bind the completed
evidence-only task bytes. All seventeen gates pass and
`claim_authorized: false` remains unchanged; scratch-011 does not broaden C43.
Historical scratch-008 and scratch-010 remain immutable under `superseded/`,
and scratch-009 preserves the missing-`jq` replay failure rather than hiding it.

There is no implemented v2, optimized, or GPU backend. METHOD-039 has selected
and frozen a practical v2 formulation for implementation, but Slice A does not
make it executable or change the current output. The implemented v1 output is
deliberately non-destructive and makes no atlas-quality claim. METHOD-039 must
still produce an accepted feature/patch result before `GEOM-076` may evaluate
any later UV-atlas chart hints.
