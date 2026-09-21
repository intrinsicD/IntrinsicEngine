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
Allow a header-touch scenario to require declared consuming translation units rather than an impossible header compiler output.

## Context
The 2026-09-20 compilation comparison rebuilt the shared property header successfully, then failed the source-equality assertion in `tools/analysis/benchmark_compile_iteration.py`. Source resolution correctly names translation units, never included headers. The first attempt is retained at `/tmp/intrinsic-compile-savings-results-20260920`; the restarted population uses a distinct output directory. This is a measurement-harness failure, not an engine compile failure.

## Acceptance criteria
- [x] Add optional scenario-to-source-list `params.probe_sources`; preserve the original touched-source default and require all declared consumers.
- [x] Complete both revision arms with the header probe checking curvature and geodesics translation units.
- [x] Run the existing compile-hotspot tooling tests and manifest/result validators.

## Verification
```bash
python3 tests/regression/tooling/Test.CompileHotspots.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/intrinsic-compile-savings-results-20260920-v2/results --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Verification result — 2026-09-20
All four corrected samples passed the declared-consumer check and canonical result validation; 26 compile-tooling tests and 102 manifests passed. Fix is present in the working diff; this note retains the observed harness failure until that diff is committed.


## 2026-09-21 revalidation
Reused the existing fix without further runner changes for RUNTIME-270's fixture
batch. All eight header probes across four ABBA samples require every declared
consumer and pass; full observed source sets are retained in
`ara/evidence/diagnostics/runtime270_fixture_batch/summary.json`. Four results
validate, all 26 tooling tests pass, and 103 manifests validate. No open code or
verification finding remains. Committing the existing fix and retiring this note
with that commit reference remain pending; the runner was not changed this session.

## Closure verification — 2026-09-21
The unchanged consumer predicate now has a small callable seam and three regression
tests covering all-consumer success, either consumer missing, absent/empty header
declarations, and source-default success/failure. The measured runner bytes remain
in each retained evidence bundle as `runner.py`; extraction changes the canonical
runner hash without changing the predicate or the original results. C103/C104
measurements are complete and are not rerun for this testability-only extraction.
