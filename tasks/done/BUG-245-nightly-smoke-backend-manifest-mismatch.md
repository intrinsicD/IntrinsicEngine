---
id: BUG-245
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: existing nightly CI failure recorded from logs; no benchmark execution or performance claim.
contract_schema: 1
contracts: []
contract_review: Catalog inspected; this tracks result-backend and manifest consistency, without changing method behavior or engine integration.
---
# BUG-245 — Nightly smoke sealing rejects two emitted backend names

## Goal
Reconcile emitted backend identities with the result validator so nightly smoke sealing
succeeds without permitting unsupported backend claims.

## Evidence
Observed during the assets audit on 2026-10-16.
[Nightly run 38854737500](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38854737500),
commit `4a5eede778b0830787fa139156f468c7ecf553e4`, job `116823560958`,
step `Run benchmark smoke and selected deep benchmarks`: sealing rejects
`geometry.harmonic_field.smoke.json` with backend `cpu_reference_sparse_cholesky`
and `geometry.property_smoothing.variational_fit_solvers.smoke.json` with backend
`cpu_admm_sparse_cholesky`. The same job reports its preceding 5817 CPU tests and
62 selected slow tests passed. This is a sealing failure, not evidence of a numerical failure.
Raw job log was retrieved through the GitHub Actions API. No PR exists for this audit.

## Acceptance criteria
- [x] Establish which backend each runner actually executes and reconcile names with the result validator; benchmark manifests do not declare a backend.
- [x] Add a deterministic regression using saved or synthetic result records; keep rejection of unsupported backends.
- [x] Validate repaired records and confirm the owning CI sealing step succeeds. Any new results-bearing execution follows experiment pre-flight.

## Verification
```bash
python3 tests/regression/tooling/Test_BenchmarkManifestValidator.py
python3 tests/regression/tooling/Test_BenchmarkResultValidator.py
python3 tools/agents/check_task_policy.py --root . --strict
```
The downloaded `nightly-deep-reports` artifact contains only the passed halfedge smoke result: the workflow uploads `smoke_result.json`, while the two failing records were written as sibling files. They cannot be recovered from that artifact. Local synthetic records and an unknown-backend mutation verify the strict validator; a new owning nightly run must close criterion 3. No benchmark was rerun or performance result inferred.

Local acceptance source: Claude session `cfef44b9-d354-480c-a64f-83338d51f2c0` accepts criteria 1 and 2 on the fixed CI patch (`0f7fc69d93e4d6688e291de705962ce1138b3d43f776fbf99eaf2ecec6a8c935`). Consolidated verification is in [HARDEN-090](../active/HARDEN-090-src-audit-remediation.md).

## Completion
Completed 2026-10-19. Fix commit: `c8d6354a8`. Owning
[nightly-deep run 39203553565](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/39203553565)
on `64d691ae5`, step "Run benchmark smoke and selected deep benchmarks": the sealer
prints "Sealed 41 benchmark result(s) as schema v2", including
`geometry.harmonic_field.smoke.json` and
`geometry.property_smoothing.variational_fit_solvers.smoke.json`. Sealing runs
`validate_result_data` on every record before writing it
(`tools/benchmark/seal_benchmark_results.py`), the same per-record check as the
standalone `validate_benchmark_results.py --strict`; the standalone run only adds a
duplicate run/attempt-identity check. That standalone step is not reached in this run
because the smoke executable exits 2 for the unrelated BUG-222 atlas workload. No
benchmark was run for this retirement.
