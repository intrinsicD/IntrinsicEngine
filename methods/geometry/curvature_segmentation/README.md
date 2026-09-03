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
backend and changed no production default. METHOD-039 consumed those immutable
controls but rejected its seed-sensitive local patch formulation; METHOD-040
owns the next task-local global-partition attempt.

METHOD-039 Slices A--C freeze that follow-up's equations, numeric screening
parameters, fail-closed supplied-evidence shape, and generated oracle and
detector controls in [`feature_aligned_intake.md`](feature_aligned_intake.md).
The executable catalog and the rejected local-solver stability gate are
[`Test.CurvaturePatchContract.cpp`](../../../tests/unit/geometry/Test.CurvaturePatchContract.cpp).
The geometry module now implements the standalone computed feature-evidence
stage and an unadopted local grow/merge/refine patch reference. The latter is not
an accepted `cpu_reference_v2`: one-dual-step seed perturbation exceeds the
frozen variation-of-information gate. Publication properties, a selector,
config, runtime, and UI adoption therefore remain absent.

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

### METHOD-039 feature-evidence stage

The narrow companion module
`Geometry.HalfedgeMesh.CurvatureSegmentation.Features` keeps the retired v1
interface and its sealed evidence byte-stable. `DetectFeatureEvidence` consumes
a full owning triangle mesh and slot-aligned ordered signed principal
curvatures; `ComputeFeatureEvidence` obtains those curvatures through the
existing estimator. Both return separate hard and soft edge arrays. Hard facts
reuse the shared strict dihedral classifier. Soft
evidence uses the three physical radii around `r_0/D=0.02`, compact one-sided
surface neighborhoods, signed-curvature-type transition plus ridge/valley
responses, two-of-three persistence, deterministic thinning, hysteresis, and
short-fragment filtering.

The owning result exposes the raw and retained confidence, every per-scale
`T/R/V/Q` value, persistence bits, dominant signal, suppression/hysteresis
decision and predecessor, combined vertex incidence, bounded-search work
counters, explicit failure status, and observational stage timings. The method
uses reusable stamped bounded-search workspaces and slot arrays; it constructs
no dense face-pair matrix. This stage does not mutate topology, publish
properties, select a v2 segmentation mode, or imply that every detected soft
line must become a final patch boundary. ARA claim C44 bounds the current CPU
contract evidence.

### METHOD-039 local patch reference and negative verdict

The companion module
`Geometry.HalfedgeMesh.CurvatureSegmentation.Patches` consumes borrowed
hard/soft feature evidence and reuses the existing deterministic Gaussian
mixture. It selects deterministic farthest-point seeds behind hard barriers,
grows provisional regions with stable multi-source Dijkstra, greedily merges a
sparse region-adjacency graph under the frozen area-weighted regional and
intrinsic boundary energy, and performs bounded connectivity-preserving
one-ring refinement. Its slot-aligned result separates provisional fronts,
final regions, hard/soft/closure boundary roles, accepted energy changes,
regional statistics, and failure/work/timing diagnostics. It neither mutates
the mesh nor changes the sealed v1 interface.

The reference exactly matches all seventeen supplied-oracle fixtures and the
bounded computed smooth-transition/ridge/valley, hard-fold, and homogeneous
controls. Seed-density, alternate-diagonal, scale, noise, and orientation
checks also pass. However, moving every automatic seed by one legal dual step
leaves a different local optimum and exceeds the preregistered area-weighted
VI limit of `0.01`. The focused regression preserves that refutation, and ARA
claim C45 bounds it. Per the frozen stop rule, no thresholds were retuned and
no production selector or control surface was added. `METHOD-040` owns a
separate task-local multicut attempt; it must distinguish a genuinely global
objective from a wider-move heuristic before implementation.

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
- Opt-in METHOD-039 completion profiles:
  `curvature_segmentation_feature_patch_{feature_smoke,quality_smoke,seed_refutation}.yaml`
  and `curvature_segmentation_feature_patch_health_100k.yaml`. Replay the
  three bounded generated controls with
  `INTRINSIC_CURVATURE_PROFILE_COHORT=feature_patch_smoke
  build/ci/bin/IntrinsicCurvaturePatchProfile <output-directory>` and
  run the separate heavy health lane with
  `INTRINSIC_CURVATURE_PROFILE_COHORT=feature_patch_health
  build/ci/bin/IntrinsicCurvaturePatchProfile <output-directory>`.
  A passed `seed_refutation` row means the frozen negative oracle was
  reproduced, not that seed stability passed. The 100k homogeneous-plane row
  checks complete assignment, deterministic repeated payloads, sparse result
  storage, finite stage timings, and bounded working-set health; its generous
  runtime limit is not a performance baseline or speed claim. All four rows
  identify the local candidate as unadopted.

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

There is no accepted v2, optimized, or GPU backend. METHOD-039's unadopted local
patch candidate is executable but failed its frozen seed-location adoption
gate, so current output remains the deliberately non-destructive v1 path and
makes no atlas-quality claim. METHOD-040 must first produce an accepted global-
partition CPU result before any separately scoped engine adoption or later
`GEOM-076` UV-atlas chart-hint evaluation may proceed.
