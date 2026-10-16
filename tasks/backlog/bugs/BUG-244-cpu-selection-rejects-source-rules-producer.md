---
id: BUG-244
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: deterministic CI selection repair and local regression checks; no results-bearing experiment.
contract_schema: 1
contracts: []
contract_review: Catalog inspected; this tracks CI test-producer reconciliation, without changing engine contracts or task schema.
---
# BUG-244 — CPU selection rejects a source-rule test without a producer

## Goal
Restore main CPU test selection without excluding source-rule coverage or weakening producer checks.

## Evidence
Observed during the assets audit on 2026-10-16, before source changes.
[Main run 38869189381](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/38869189381),
commit `66da33fd6bfabb20408607b8361354fc9adb420d`, job `116868991959`,
step `Capture CPU test selection`: `BLOCKED: selected CTest test
'RenderWorldPoolEngineWiring.SourceRules' does not map to a registered producer` (exit 3).
The build had completed; the full CPU test step did not run. The later missing
`test.json` timing report is a consequence, not an independent root cause.
Raw job log was retrieved through the GitHub Actions API. No PR exists for this audit.

## Acceptance criteria
- [x] Diagnose and reconcile source-rule registration with CPU selection's producer model.
- [x] Add a regression that rejects missing real producers while accepting intended source-rule tests.
- [ ] The owning main CI selection and CPU gate pass without suppressing coverage.

Local acceptance source: Claude session `cfef44b9-d354-480c-a64f-83338d51f2c0` accepts criteria 1 and 2 on the fixed CI patch (`0f7fc69d93e4d6688e291de705962ce1138b3d43f776fbf99eaf2ecec6a8c935`). Main CI has not been rerun; criterion 3 remains open. Consolidated implementation verification is in [HARDEN-090](../../active/HARDEN-090-src-audit-remediation.md).

## Verification
```bash
python3 tests/regression/tooling/Test.CpuTestSelection.py
python3 tests/regression/tooling/Test.TestGateRouting.py --build-dir build/ci --aggregate IntrinsicCpuTests
python3 tools/ci/cpu_test_selection.py capture --build-dir build/ci --preset ci --expected-sanitizer none --output build/ci/test-selection/selection.json
python3 tools/agents/check_task_policy.py --root . --strict
```
