---
id: METHOD-042
theme: I
depends_on: [METHOD-041]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive implementation of the operator-approved frog/sculpt experiment; code, tests, raw observations and independent Claude review are the evidence."
owner: codex
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-07T11:00:00+02:00"
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
contract_review: "Triangle-surface labels and geometry are semantic inputs. Initial work is an offline experiment over source-bound native exports; any reusable CPU method must preserve the source mesh and declare its own formulation. No ECS property or layering boundary changes are planned."
---
# METHOD-042 — Region-aware parts experiment

## Goal
- Work out and implement the approved sequence with Claude: test explicit frog seams against the current objective, distinguish geometry/sampling/search failures on controlled shapes, and build the smallest evidence-justified region-aware CPU comparison with inspectable results.

## Context
- On 2026-09-07 the operator accepted the joint recommendation and explicitly requested implementation together with Claude. This is directed method work outside the standing Framework24 selection focus. The preceding local comparison is under `build/frog-sculpt-review-20260907/`; source-bound exports can be reproduced with existing native runners.
- Sculpt's two replayed partitions equal the hard-barrier components; frog needs smooth boundary decisions. Provisional authored seams are hypotheses, not operator ground truth. Existing METHOD-039/040 negative adoption results remain unchanged.
- The task-workflow alignment questions are resolved by the preceding comparison and the operator's approval. This interactive lane does not enroll overnight custody or require another scope confirmation.

## Acceptance criteria
- [x] A tested evaluator binds native exports and evaluates full connected candidate partitions under the exact fixed METHOD-040 objective, checking hard constraints and distinguishing cleanup.
- [x] Explicit provisional frog seams have saved labels, geometric approximation diagnostics, feasibility and exact energy comparisons; no desired-label claim is inferred from their construction.
- [x] Controlled smooth-lobe/neck and decorative-ridge cases distinguish regional utility from feature strength and disclose sensitivity to sampling and triangulation.
- [x] Implement and inspect one bounded region-aware CPU candidate justified by the oracle result, with a distinct implementation identity, deterministic controls and retained failures. This is an offline diagnostic reference with a regional proposal gate, not an adopted engine backend or validated interior-aware model.
- [x] Claude reviews the design and resulting evidence; resolve mathematical/source findings and preserve the exchange.
- [x] Tests, source/input/output identity, comparison artifacts, limitations and the next implementation decision are recorded without claiming accepted general quality or changing production defaults.

## Engine integration
| Field | Decision |
| --- | --- |
| Least-structured input | Embedded oriented triangle surface, face-dual adjacency and source-bound evidence/labels. |
| Compatible entity sources | Mesh sources satisfying the triangle-surface contract; arbitrary graphs or point sets do not contain the required embedded surface. |
| RuntimeModule | Existing production paths unchanged; METHOD-043 owns any later positive candidate's native integration, subject to a new evidence decision. |
| Config/agent | Offline serialized experiment specification and CLI are the current control surface; METHOD-043 owns later shared validated config integration. |
| UI | Source-bound standalone comparison for this experiment; UI-053 independently owns the existing extremum overlay. METHOD-043 owns any later candidate selection UI. |
| Publication | Detached face labels and diagnostic curves; original topology, positions and unrelated fields remain unchanged. |
| End-to-end tests | Experiment-specification-to-export/viewer checks now; METHOD-043 owns any later engine publication tests. |

## Slice plan
- A: freeze seam hypotheses and evaluator; compare with exact native fixed-profile energy. A candidate worse under that energy is evidence about the specific proposed partition, not proof that every useful seam has higher energy.
- B: use controlled geometry and an independently named regional comparator to test the failure identified by A. Freeze controls before cohort inspection; avoid frog-specific parameter selection.
- C: source/evidence review, verification, matching-view artifacts and a bounded verdict. Promote a candidate only if its own declared tests support that disposition.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCurvatureBoundaryMesh IntrinsicCurvatureExtremaMesh IntrinsicGeometryFeaturePartitionTests IntrinsicGeometryCurvatureExtremaTests
ctest --test-dir build/ci --output-on-failure -R 'CurvatureExtrema|CurvatureBoundaryPartition' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Limits
- No new production default, relaxed frozen gate, silent source repair, semantic part-quality guarantee, novelty claim, GPU backend or performance claim.
- Local comparisons remain exploratory; a validated artifact proves its recorded inputs and measurements, not that its boundaries are useful.

## Session result — 2026-09-07
- The bounded experiment is implemented and reviewed, with [report](../../docs/methods/frog_parts_experiment.md), [raw summary and bindings](../../ara/evidence/diagnostics/method042/record.json), and [verification](../../ara/evidence/diagnostics/method042/verification.json). It remains in the active tree pending normal human review/commit; no retirement or production maturity is claimed.
- C63 records the feasible-but-disfavored provisional seams; C64 refutes sampling stability of the separate diagnostic; C65 distinguishes a missing proposal from an unfavorable explicit seam on the dense synthetic neck. Frog's frozen comparator adds no splits.
- Claude supplied design/source critique; Codex ran every computation/test and corrected unsupported review findings. The complete retained review sequence is linked in the report.
- Canonical Clang-23 `ci` build and CPU selector: 4,302 passed, six skipped, zero failed. New tooling suites: 18 passed; existing viewers: 24 passed. Four sealed native measurements validate as schema v2, remain non-claim-eligible, and do not establish performance or quality.
- [METHOD-043](METHOD-043-thickness-and-curve-parts-comparison.md) owns the independent thickness and curve-aligned comparison, including asymmetric controls and any eventual native/config/UI integration. The operator subsequently approved it with a four-round cap.
