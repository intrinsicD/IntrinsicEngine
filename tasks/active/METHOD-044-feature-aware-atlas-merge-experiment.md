---
id: METHOD-044
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Operator-directed interactive CPU experiment; retain source, controls, local measurements and actual Claude reviews, without publication custody."
owner: codex
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-07T13:57:17+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
contract_review: "Embedded oriented triangle surfaces are semantic inputs. Detached face labels and corner UVs preserve source topology. New numerical code is an offline diagnostic, not a new production parameterization backend."
---
# METHOD-044 — Feature-aware patches and distortion-aware atlas merging

## Goal
- Test a fast classical route to coherent, low-distortion atlas charts: connected feature-aware patches followed by UV-validated adjacent merges. Compare feature-blind patches and native xatlas on identical geometry.

## Context
- The operator explicitly directed this research outside standing product convergence on 2026-09-07 and asked for implementation, iteration and actual Claude collaboration. The earlier METHOD-043 four-round thickness budget is complete; this is a separately authorized atlas experiment. Anatomical labels and deep learning are out of scope.
- Literature basis: [LSCM atlas construction](https://people.engr.tamu.edu/schaefer/teaching/689_Fall2006/p362-levy.pdf), [feature-aware oversegmentation](https://link.springer.com/content/pdf/10.1007/s41095-016-0071-3.pdf), [hierarchical chart merging](https://cs.harvard.edu/~sjg/papers/tmpm.pdf), and [D-Charts](https://www.cs.ubc.ca/~vlady/dcharts/dcharts.htm). This is an adaptation, not a reproduction or novelty claim.

## Frozen first experiment
- Area-normalized geometry; approximately 64 connected initial patches from farthest geodesic seeds. Dual-edge cost is geometric distance plus a soft integrated normal-turn penalty. Use the same seeds for feature-on/off comparisons. Invalid initial patches split recursively.
- A union must be a manifold disk with a continuous finite map, positive triangle orientation, simple UV boundary, maximum area-normalized singular-value stretch at most 1.5, and maximum anisotropy at most 2.0. These are provisional explicit engineering bounds, not user-specified requirements. Features are soft preferences, not mandatory cuts.
- Merge adjacent patches using seam removal and compactness; cache unchanged rejected pairs. Track solver calls, accepted/rejected merges and all phase timings. Same packing for prototype variants; native xatlas is an external end-to-end baseline, not an isolated clustering ablation.
- Synthetic plane/fold/sphere/cylinder/annulus controls precede frog/sculpt and held-out sharp/smooth meshes. Compare uniform subdivision (same piecewise-linear geometry) separately from independently sampled surfaces. Preserve negative cells and show actual UVs.
- Stop or pivot when measured quality, boundary rigidity or repeated solve cost explains lack of progress. No production default or UI changes are authorized by a positive offline result alone.

## Acceptance criteria
- [x] Tested CPU reference with explicit topology, degeneracy, orientation and overlap failure diagnostics.
- [x] Reproducible feature-on/off and native-baseline comparisons, with fixed cohort and parameters per iteration.
- [x] Inspectable mesh/chart and UV artifacts, actual Claude design/result reviews, and honest result/pivot decision.
- [x] Focused tests and repository verification; retain local dirty-source limitations.

## Current stop-state
- The four-round **offline experiment is complete**; [report](../../docs/methods/feature_aware_atlas_experiment.md), [49-cell campaign](../../ara/evidence/diagnostics/method044/record.json), importable examples and ARA C69/C70 retain its bounded results and negative cells.
- Claude's final focused numerical/evidence review found no blocking error in the shown code and recommended keeping the two-stage direction. It did not establish native performance or universal quality superiority. Its normalization question was checked against `geometry`: the face areas sum to one by construction.
- Verification: 11 standalone diagnostic tests including native adapters; 4308 full CPU CTest entries with zero failures and six skips; 90 final focused atlas/parameterization entries with zero failures. Strict task/ARA/manifest/result/layout and documentation-link checks pass. No GPU/sanitizer/UI claim.
- No production method token/default/UI change or automatic native adoption. This interactive note remains active as shared memory for deferred integration decisions and is not retired. The operator authorized committing and pushing the complete outstanding research batch on 2026-09-07; historical measurements remain dirty-source/non-claim-eligible. Existing operator-owned edits were preserved.
- Separate workshop finding: strict root hygiene rejects the untracked local `.agents/` directory. [BUG-177](../backlog/bugs/BUG-177-root-hygiene-local-agent-metadata.md) records it; no directory or root policy was changed to hide the failure.

## Iteration 2 protocol
- Round 1 is retained under its own source hash. The scalar disk predicate dominates the instrumented profile; replace only its traversal with equivalent edge/corner connected-component operations and verify decisions and final outputs against the scalar version. Do not loosen the stretch gate.
- Freeze 64 seeds and weight 2 on held-out bunny10k/fandisk, and uniform fourfold frog/sculpt subdivision. Analytic fold, cut/open cylinder, sphere and annulus controls exercise topology and developability. Predict similar chart counts/seams under subdivision; record any instability rather than hiding it.
- Add 1.25 and 1.35 stretch operating points on frog/sculpt to distinguish fewer-charts/more-distortion from a genuine quality improvement. Native xatlas stays at its unmodified options; do not call these precisely matched-distortion runs.
- Audit direct positive-area triangle intersections, including between packed charts. Non-disk native charts are classified separately rather than automatically counted as invalid. Runtime remains local diagnostic evidence (Python reference vs ci Debug adapter), not a native speed comparison.

## Engine integration
| Field | Decision / owner |
| --- | --- |
| Least-structured input | Positions and oriented manifold triangle connectivity; optional geometric feature weights. |
| Compatible entity sources | Any surface source satisfying those properties/topology, not a provenance-specific mesh type. |
| RuntimeModule | Deferred; METHOD-044 owns candidate selection, then GEOM-076 must be explicitly re-scoped before adoption. |
| Config/agent | Offline serialized experiment parameters now; METHOD-044 owns a follow-up for shared validated production controls if selected. |
| UI | No changes; same future follow-up as runtime/config. |
| Publication | Detached source-face labels and face-corner UVs; source mesh and unrelated properties unchanged. |
| End-to-end tests | Offline full face coverage and continuity now; production source-to-publication tests deferred to the adoption follow-up. |

## Iteration 3 protocol
- Keep labels and LSCM chart maps frozen. Test the existing xatlas `AddUvMesh`/`PackCharts` public API as a pack-only stage, using the existing vcpkg dependency and an opt-in diagnostic adapter, not a new production API.
- Prediction: packing improves occupied atlas area without changing chart membership or meaningfully changing distortion. Verify source-corner correspondence, exact chart count, signed areas, all-chart triangle overlap, and post-pack stretch; report any stretch-bound breach rather than treating packing as automatically safe.
- Inspect original and fourfold-subdivided frog/sculpt plus held-out bunny/fandisk. Report continuous occupied UV area separately from xatlas's rasterized utilization and measure between-chart texel-density variation. No packer speed or cross-language speedup claim.

## Iteration 4 protocol
- Complete the feature ablation without changing quality limits, 64 seed locations, UV solver or packing: add growth-only (growth weight 2, merge ranking weight 0) and merge-only (growth 0, ranking 2) on the original four-mesh cohort. Previous feature-on/off results alone conflate these two effects.
- Prediction: most feature benefit on sharp meshes comes from initialization, while the weak merge-order term has a smaller effect. This round tests attribution, not a new tuned algorithm. Retain any contradictory result; no feature-only production claim from two confounded arms.

## Verification
```bash
python3 tests/regression/tooling/Test.AtlasPatchMerge.py
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicUvAtlasMeshDiagnostic
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
