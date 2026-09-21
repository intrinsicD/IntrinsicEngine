---
id: BUG-204
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive measurement; evidence is the runner diff and retained measurements
contract_schema: 1
contracts: []
contract_review: Compile measurement probe accounting only; no engine or catalogued subsystem contract changes.
---
# BUG-204 — Compile benchmark rejects header invalidation probes

## Goal
Allow header-touch scenarios to require their consuming translation units.

## Acceptance criteria
- [x] Optional `params.probe_sources` requires all declared consumers; source
      scenarios retain their touched-source default.
- [x] Both revision arms complete with actual header consumers checked.
- [x] Missing consumers, absent/empty header declarations, and source-default
      success/failure have regression coverage.
- [x] Compile-tooling tests and manifest/result validators pass.

## Completion — 2026-09-21
Fix commit: `defaafeac`; all acceptance criteria closed. This is a measurement
harness repair with executable positive and negative checks; no engine capability
or performance claim. No follow-up implementation remains.

The [recent-locality report](../../ara/evidence/tables/runtime270_recent_locality_measurement.md)
and [fixture-batch report](../../ara/evidence/tables/runtime270_fixture_batch_measurement.md)
retain all eight accepted results plus the initial rejected header attempt.
Original measured runner bytes and hashes remain in those bundles. The canonical
runner extracts the unchanged predicate for tests, so its hash differs; the
accepted measurements were not rerun or silently rebound.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root ara/evidence/diagnostics/runtime270_recent_locality/results --strict
python3 tools/benchmark/validate_benchmark_results.py --root ara/evidence/diagnostics/runtime270_fixture_batch/results --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

29 tests, 103 manifests and eight canonical result records pass. Claude Fable 5.1
fixed-diff follow-up accepts the failure cases. Strict docs sync and task policy
pass; the final integrated canonical CPU gate also passes (4,861 passes, zero
failures, one expected capability skip). No sanitizer/GPU execution.
