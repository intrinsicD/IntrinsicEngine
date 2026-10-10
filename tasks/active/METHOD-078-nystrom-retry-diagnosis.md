---
id: METHOD-078
theme: I
depends_on: [METHOD-077]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive diagnosis; deterministic checks accompany the diff; any results-bearing comparison requires its separate reviewed preflight.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-078 — Diagnose Nyström rejection and evaluate retrying

## Goal
1. "Dann bitte nyström Nutzung untersuchen und verbessern"

Continue METHOD-077. Prefer less total registration work at unchanged accuracy over a larger
accepted-iteration counter. Diagnose the first rejection and the subsequent suppression before
changing production selection policy. The operator may clarify this priority.

## Acceptance criteria
- [ ] Distinguish numerical/accuracy rejection, cost rejection, and cached rejection in the actual METHOD-077 configuration.
- [ ] Test a candidate against unchanged accuracy/fallback controls; preserve the baseline and reject unsupported speedup claims.
- [ ] Remove temporary diagnostic probes before delivering any production change; record what remains uncertain.

## Engine integration
| Field | Disposition |
| --- | --- |
| Least-structured input | Unchanged two point-position sets. |
| Compatible entity sources | Unchanged mesh vertices, graph nodes and point clouds. |
| RuntimeModule | Existing RegistrationOperations CPD; no new wiring. |
| Config/agent | Existing Nyström source, schedule and accuracy fields; no new fields. |
| UI | Existing CPD panel; no new controls. |
| Publication | Existing transformed-source positions, same cardinality. |
| End-to-end tests | Existing public Register path in the landmark runner; runtime Nyström tests before any production adoption. |

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryRegistrationSlowTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^CoherentPointDriftAccelerated\.'
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Diagnosis log — 2026-10-11
- Base `bbdf1af79`; existing eight Nyström tests pass. The direct-evaluator regression in
  `NystromCanRetryAfterTheSourceMovesCloser` fails before a fix: a fresh evaluator accepts the
  current input, a previously rejected evaluator does not try it. This is not a reproduction
  of METHOD-077 performance or proof that more retries help it.
- Ranked hypotheses: cached rejection blocks useful retries; the cost cap prevents larger
  landmark sets; or a narrow kernel genuinely fails the approximation's accuracy/positivity checks.
- Initial plan review: Codex → Claude, requested `claude-opus-5-5` / max; reported model/effort
  not independently established; session `134086c4-6a2b-4d22-a217-d67c322335f3`;
  packet SHA256 `09c2ce5515b175965b098f8956bd7edfe727d8658e488612b8f003361c11523e`.
  Verdict: **revise**. Read-only/no-recursion enforced by Read/Grep/Glob-only tools and strict MCP
  configuration. Actual answer: [initial review](../evidence/METHOD-078/reviews/initial-plan-claude.md).
  Accepted objection: unconditionally removing the cache could waste work relative to truncated
  exact evaluation. No production adoption without measuring that tradeoff.
- Temporary probes: `Geometry.Registration.CoherentPointDrift.EStep.cpp` emits `NYSTROM_DIAG`;
  the existing landmark runner emits `CPD_ITER` with `--trace 1` and accepts `--iterations`.
  This task owns their removal. The original METHOD-077 evidence is unchanged.
- Candidate deterministic check: 29/29 `CoherentPointDriftAccelerated` tests pass; baseline
  fails the added moved-source case. Logs are under `tasks/evidence/METHOD-078/verification/`.
- Diagnostic-driver self-test passes; removing the timing screen, phase parsing or failure
  verdict is detected. Independent preparation review: **APPROVE für die Diagnosevorbereitung**;
  exact bindings and actual answer in [review](../evidence/METHOD-078/reviews/preparation-codex.md).
  This approves preparation only, not the experiment or production selection.
- Strict task policy passes. Strict doc links retain the pre-existing METHOD-077 review-link
  defect tracked by BUG-241; frozen review evidence was not rewritten.
- First full preflight returned REVISE: external-timeout recovery and an old-result statement
  exposed during blind reconstruction. Both corrected; fresh blind sessions required.
  SIGALRM child termination/failure-before-report verified by `verification/test-timeout.py`;
  code delta independently approved. No scientific run has started.
- Both control configurations now pass technical-only execution with full size/configuration;
  no efficacy/timing values inspected. Pilot total resource bound revised from 1800 to 3600
  seconds to accommodate eleven per-cell 300-second limits plus report overhead. No cells,
  numerical gates, input data or native binaries changed; new digest requires both reviews.
