---
id: METHOD-045
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Operator-directed interactive diagnostic work; retain replay inputs, tests, actual Claude review and bounded observations without publication custody."
owner: codex
claimed_at: "2026-09-07T20:41:28+02:00"
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
contract_review: "Embedded oriented triangle connectivity is required. Baseline face regions remain immutable and distinct from UV chart labels and per-corner cuts; no runtime or property publication is introduced."
---
# METHOD-045 — Preserve baseline regions while constructing UV charts

## Goal
- Address the offline atlas quality obstacles with Claude while preserving the original sculpt segmentation where possible, before considering engine integration.

## Context
- On 2026-09-07 the operator redirected the requested runtime integration into collaborative quality work and explicitly prioritized baseline quality, particularly sculpt. This is directed research outside the standing product-convergence focus. The working interpretation is the original METHOD-039/040 five-region sculpt segmentation; native xatlas UV distortion is measured separately.
- METHOD-042/043/044 historical experiments remain frozen. Their earlier round limits are not silently extended. This new scope has a bounded topology/cut experiment followed by one evidence-justified correction if needed; further algorithm choices remain explicit.
- Right-sizing: reuse the offline LSCM, independent UV audit and native packer. Plain functions and detached arrays suffice; no backend, service, runtime module, dependencies or production defaults are added.

## Ranked hypotheses and probe ledger
- H1: Arbitrary initial atoms discard useful baseline boundaries. Probe unchanged native baseline components under the existing LSCM gate before replacing initialization.
- H2: Non-disk baseline components require UV cuts, not new semantic regions. Connect boundary loops by shortest edge paths, duplicate only seam corners, and validate the cut topology before solving.
- H3: Valid charts with better boundaries can still pack poorly. Use the unchanged native UV-only packer and audit actual post-pack maps and square-texture occupancy independently.
- Probe 1: `OPENBLAS_NUM_THREADS=1 python3 /tmp/intrinsic-quality-probe.py` on the fresh native `sculpt local` export: all five components fail topology. Therefore direct baseline-region LSCM without cuts is rejected; do not tune distortion to hide it. The portable replay will replace this scratch command.
- Claude's initial public-report review conflated the five-region baseline with the newer five-chart atlas. Follow-up accepted the correction and independently recommended the topology probe before construction. Public-text exchanges are retained in the experiment evidence; no private input geometry is sent.

## Frozen experiment
- Keep native local-region labels as a separate immutable array. Audit full source correspondence; report label count, connected components, retained/lost baseline edges, extra chart edges and internal UV-cut edges separately.
- For connected orientable manifold genus-zero regions with boundaries, join boundary loops by deterministic shortest primal-edge paths; duplicate source corners along those paths, then require a manifold disk. A closed or unsupported topology follows the explicit existing within-region bisection path. No cut deletes a face or silently repairs source topology.
- Apply unchanged area-normalized LSCM with maximum bidirectional stretch 1.35 and anisotropy 2.0. Split only a failing component; never merge across a baseline region. Compare original 64-seed feature/growth-only and native xatlas on identical exported geometry.
- Controls before cohort: planar disk/annulus, cut/uncut cylinders, disconnected labels, pinched topology, invalid labels, scale/rigid transforms, repeated runs, and packing a chart with duplicated source vertices. Cohort: sculpt and frog; independent sharp/smooth controls fandisk and bunny10k, plus same-surface subdivision.
- Acceptance is complete valid coverage and zero lost baseline edges with unchanged baseline labels. Improvement requires separately measured chart/seam/distortion/packing evidence; fewer charts or returning the baseline alone is not quality superiority. If no exact five-chart sculpt map survives the frozen quality limit, disclose the required extra cuts/charts. No shape-name fallback or blanket xatlas-win claim.

## Acceptance criteria
- [x] Tested topology-cut reference preserving source and baseline region identity, including explicit unsupported and distortion outcomes.
- [x] Correct native packing of internal cuts, exact source-corner mapping and independent post-pack validation.
- [x] Matched baseline/prototype/candidate/native comparisons with inspectable maps and negative cells retained.
- [x] Actual Claude design/result review with corrections checked against implementation and measurements.
- [x] Focused and canonical CPU verification, factual report and source-bound evidence.

## Engine integration
| Field | Decision / owner |
| --- | --- |
| Least-structured input | Oriented embedded manifold triangle surface and source-bound face labels. |
| Compatible entity sources | Any surface satisfying that contract; not arbitrary graphs or point sets. |
| RuntimeModule | Deferred by operator; METHOD-044 owns adoption after quality review. |
| Config/agent | Offline CLI and serialized parameters now; METHOD-044 owns shared production controls. |
| UI | Offline comparison/export only; METHOD-044 owns editor discovery if adopted. |
| Publication | Immutable source geometry and semantic regions, detached corner UVs and chart IDs. |
| End-to-end tests | Native baseline export to offline atlas and packer now; METHOD-044 owns ECS publication. |

## Spatial acceleration consideration

If geometric overlap audits become a measured bottleneck, consider a CPU primitive
broad phase while retaining exact UV triangle/segment predicates. Current point
LBVH cannot establish chart injectivity or preserve semantic regions by nearest-
centroid clustering. Keep the existing bounded experiment, baseline labels,
independent audit and deferred runtime decision.

See the [shared spatial-index consumer inventory](../../docs/architecture/spatial-index-consumers.md).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicUvAtlasMeshDiagnostic IntrinsicUvChartPackDiagnostic IntrinsicCurvatureBoundaryMesh
OPENBLAS_NUM_THREADS=1 python3 tests/regression/tooling/Test.BaselineAtlas.py
OPENBLAS_NUM_THREADS=1 INTRINSIC_TEST_NATIVE_ATLAS=1 python3 tests/regression/tooling/Test.AtlasPatchMerge.py
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_test_layout.py --root . --strict
```

## Continuation after the first cohort
- The direct uncut-region hypothesis was rejected by topology on all five sculpt regions. Internal cuts preserve the original region IDs and distinguish UV seams from segmentation; this is not a thickness-algorithm fix.
- The cut reference passes the cylinder control. The flat-annulus one-chart prediction was false: zero-energy LSCM closes the slit and fails the simple-boundary check. The retained regression checks explicit UV subdivision instead, without relaxing the boundary predicate.
- The first split-only frog result overfragments. The bounded correction reuses seam-length ranking and the same cut/LSCM validity gate to merge only subdivisions of a single baseline region. Original inter-region borders remain fixed.
- Packing-only probe: expose the existing xatlas `bruteForce` option in the diagnostic JSON/CLI. Freeze all UVs, chart labels, resolution 1024 and padding 2; compare exhaustive placement with existing placement on sculpt/frog. Report post-pack numerical drift, actual dimensions and square-texture occupancy. This adds no runtime/default option.
- The native local bunny10k baseline rejects `invalid_topology` before producing labels. Retain this in the cohort rather than repairing or fabricating labels; add dolphin as an additional smooth control, identified as a post-failure cohort extension.


## Current result
- The bounded implementation and report are in
  [baseline_preserving_atlas_experiment.md](../../docs/methods/baseline_preserving_atlas_experiment.md),
  with [record](../../ara/evidence/diagnostics/method045/record.json) and C71/C72.
  Region preservation is supported on the recorded CPU controls. Uniform UV
  superiority and packing improvement are not established; jagged extra UV cuts,
  arbitrary remeshing, native cost, and METHOD-043 parts quality remain open.
- After explicit operator sharing approval, Claude completed the unpublished
  source and mesh-free aggregate review. Its reporting findings were resolved
  against native packing source and retained arrays: whole-chart mirroring,
  per-chart normalization and frog chart inflation are explicit in the report.
  The review is not an independent rerun or anatomical visual acceptance.
- Canonical ci (Clang 23), IntrinsicTests and the diagnostic adapters built.
  Full CPU CTest: 4308 entries, zero failures, one environment skip. Nine new and
  eleven existing Python atlas tests passed including real native packing;
  final focused CTest and structural/schema checks are retained with the evidence.
- This note remains active for the remaining quality follow-ups. No runtime/UI wiring,
  production default, native performance claim, commit or push is implied.

## Operator-requested stage audit, 2026-09-08
- Inspect initial clusters, split/merge histories, curvature-curve alignment and
  decision scalars before changing the algorithm. Compare native baseline, earlier
  feature atlas and protected atlas on exact source geometry.
- Ranked probes: initial placement may already miss useful boundaries; atlas merging
  may erase useful borders; unweighted distortion splits may create poorly placed
  borders. Retain stage outputs and rejected decisions; no smoothing or new
  decision term is adopted from a visual correlation alone.
- Export existing public native baseline diagnostics and observe the Python
  reference without changing its decisions. Native initial/final states and
  aggregate merge history are distinct from a fully ordered native refinement trace.

- Completed stage audit: [report](../../docs/methods/atlas_stage_inspection.md),
  source-bound scalar/curve viewer and C73. Final atlas partitions are unchanged.
  Native baseline exposes initial/final states; Python trace records each accepted
  split/merge and rejected union. Curvature alignment and boundary relocation remain
  unimplemented decisions, not hidden acceptance claims.
- This audit configured/built ci, passed 30 focused curvature tests and 10 Python
  controls, and rendered all 265 viewer modes. No full CPU or sanitizer rerun is
  claimed for this diagnostic-only turn.

- The subsequent operator-directed implementation and review of additional-boundary relocation is recorded in [METHOD-046](../done/METHOD-046-feature-guided-uv-boundary-refinement.md); the frozen METHOD-045 evidence remains unchanged.

## Publication and remaining scope
- The completed protected-atlas, stage-audit and refinement artifacts were published to `origin/main` in `910a1ed36b32dad5d8a6491519a49cd7f1deca77` on 2026-09-08. Portable compressed viewers and frog/sculpt OBJ examples travel with the repository.
- This note remains active for the uncompleted quality investigations: chart-area balance, remeshing robustness, internal-cut placement and feature-aware merge policy. METHOD-046's bounded reference experiment is complete; METHOD-044 still owns production adoption.
