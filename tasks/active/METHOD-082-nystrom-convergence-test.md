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
- [ ] Frozen convergence plan and complete-verdict controls pass two independent preflight reviews.
- [ ] Every planned cell has a retained outcome or explicit unevaluated disposition, raw report, convergence/endpoint/truth diagnostics and bounded replay checks.
- [ ] An independent raw-output audit accepts the bounded interpretation; results and remaining uncertainties are recorded without modifying the algorithm.

## Verification
```bash
python3 tasks/evidence/METHOD-082/check.py
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
