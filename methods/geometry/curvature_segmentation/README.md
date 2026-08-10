# Signed-Curvature Mesh Segmentation

Method ID: `geometry.curvature_segmentation`. Status: **reference**.

This package records the CPU-reference contract for non-destructive mesh
segmentation from signed principal curvatures. It is informed by published
curvature-tensor mesh segmentation, Gaussian-mixture EM, BIC, and spatial
labeling work, but the exact combined objective is repository-specific. See
[`paper.md`](paper.md) for the citations and frozen equations.

METHOD-038 is active in an evidence-only Slice A. Its primary-source equation
intake, scale convention, paired-remeshing metrics, and candidate killing order
are in [`feature_aligned_intake.md`](feature_aligned_intake.md). No v2 backend
or production default is selected yet.

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

The frozen non-claim-eligible
[`METHOD-038` scratch replay](../../../tasks/evidence/METHOD-038/experiment/protocol.yaml)
selected four Automatic components on the paired 10k fixture, failing the
exact-two gate while passing the `0.02` label-error bound; the matched Fixed
control selected two. The independent
[`audit`](../../../tasks/evidence/METHOD-038/experiment/runs/scratch-002/audit.json)
rejected claim authorization as intended. This is bounded negative evidence
(ARA C40), not a reason to retune or change the production default.

There is no optimized or GPU backend. The output is deliberately
non-destructive and makes no atlas-quality claim. `GEOM-076` owns the later,
evidence-gated decision about converting accepted boundaries into cuts and UV
atlas chart hints.
