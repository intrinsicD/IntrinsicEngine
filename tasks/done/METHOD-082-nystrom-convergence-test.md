---
id: METHOD-082
theme: I
depends_on: [METHOD-081]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive convergence experiment; reviewed packet, raw outputs and referee decisions live with task evidence.
contract_schema: 1
contracts: []
contract_review: experiment only; existing public method, engine integration, configuration and publication contracts are unchanged.
---
# METHOD-082 — Test Nyström row repair through convergence

## Goal
1. "Dann bitte einmal testen, vollständige Konvergenz usw"

Operator-directed work outside Framework24 P0. Extend METHOD-081's short prefixes to
regular termination on the existing full/partial synthetic fixtures and seeds 1–3.
The prior instruction "Ok ohne Claude" applies to the two independent Codex reviews.

## Acceptance criteria
- [x] Frozen convergence plan and complete-verdict controls pass two independent preflight reviews.
- [x] Every planned cell has a retained outcome or explicit unevaluated disposition, raw report, convergence/endpoint/truth diagnostics and bounded replay checks.
- [x] An independent raw-output audit accepts the bounded interpretation; results and remaining uncertainties are recorded without modifying the algorithm.

## Verification
```bash
python3 tasks/evidence/METHOD-082/check.py
python3 tasks/evidence/METHOD-082/verification/postrun-check.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
```

## Scope and reuse
Reuse METHOD-081's retained baseline/candidate native runners and its trace/plot helpers.
Its archived controller requires fixed 7/2-step prefixes, so this separate experiment owns
the longer plan, termination classification, truth checks and report. No engine, caller,
fixture generator, config, export, dependency or C++ test changes. Existing METHOD-081
analytic regression evidence remains applicable; new Python controls cover this evaluator.
Plan counter-review: `/root/method082_plan`, approved with interpretation/validation notes.

## Result record
The [version1 report](../evidence/METHOD-082/postrun/report.html) owns the final interpretation,
including the interrupted traces and uncertainties; ARA C123/C124 retain the refuted
predictions. Original raw outputs/report stay unchanged, bound by
[execution binding](../evidence/METHOD-082/reviews/execution-binding.json).
The [independent raw audit](../evidence/METHOD-082/reviews/final-raw-results-audit.txt)
accepts the scientific interpretation; the [final report review](../evidence/METHOD-082/reviews/final-report-review.txt)
accepts all three criteria and the closure-only updates.
This task ends at a bounded diagnostic result, with no new engine maturity claim.
Diagnosis of the shared partial-overlap failure is outside this test's scope.

## Completion
Completed 2026-10-12. Frozen input commit: `8c3ffaaa7b396a5c875ca32b77c34d3b1abbc60e`.
Verification in this session: complete-verdict controls and six rejected mutations,
native-failure-shape regression before/after, final-configuration technical checks,
the frozen experiment, independent raw audit, and interrupted-report before/after
coverage/hash check. No new C++ build/test claim. Task/ARA/workflow validators pass;
strict documentation links retain 24 pre-existing METHOD-077 failures owned by BUG-241.
