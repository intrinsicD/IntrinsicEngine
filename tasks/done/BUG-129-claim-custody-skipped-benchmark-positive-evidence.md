---
id: BUG-129
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-ClaimCustody"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-05T03:49:13Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-129 — Claim custody accepts skipped benchmarks as positive evidence

## Status
- Completed on 2026-08-05. The original METHOD-020 run-001 carries a
  structurally valid rejected audit, claim-grade non-execution is fail-closed,
  and independent fixed-surface review accepted revision `40232d16` with no
  findings.
- Commit: `40232d16`.

## Goal
- Make the METHOD-020 claim-grade Vulkan benchmark runner and benchmark
  custody fail closed when the requested backend did not execute successfully.

## Non-goals
- Do not invalidate genuine passed benchmark evidence or rewrite frozen
  experiment inputs.
- Do not redefine `claim_eligible` as an outcome disposition; it remains a
  source-custody property.
- Do not require Vulkan for the default CPU-supported test gate.

## Context
- Symptom: the METHOD-020 Vulkan runner returned zero for `status: skipped`,
  reported `actual_backend: gpu_vulkan_compute` with zero accepted/completed
  requests, and its canonical result could enter a positive experiment bundle.
- Expected behavior: every non-passed runner outcome exits nonzero, diagnostics
  distinguish requested from actual execution, and an accepted benchmark-backed
  audit requires canonical `execution_status: passed` and `status: passed`.
- Impact: an unavailable GLFW/Vulkan host could otherwise satisfy the automated
  positive-claim path with zero executions and zero-valued metrics.

## Required changes
- [x] Make the METHOD-020 benchmark runner return nonzero for skipped, failed,
      and error outcomes and report truthful actual-backend/fallback state.
- [x] Make benchmark-backed bundle audit reject every canonical result whose
      execution or recomputed status is not `passed`, while retaining the
      failed result as inspectable negative evidence.
- [x] Preserve historical passed bundle bindings without a schema rewrite.

## Tests
- [x] Add a tooling regression proving skipped benchmark output cannot receive
      an accepted experiment audit.
- [x] Add an executable regression proving the runner's unavailable-windowing
      path returns nonzero and does not claim actual Vulkan execution.
- [x] Run the complete benchmark-result and experiment-custody regression suites.

## Docs
- [x] Document the positive bundle audit rule and requested-versus-actual
      backend diagnostics in the canonical benchmark/workflow policy.
- [x] Update the bug index, session brief, and retirement log on closure; sync
      generated skill mirrors for changed `docs/agent/*` policy.

## Acceptance criteria
- [x] The original headless repro exits nonzero, reports `status: skipped`, and
      reports no actual Vulkan backend or observed fallback.
- [x] Custody creation may retain a non-passed result, but independent audit and
      completion cannot accept it as positive evidence.
- [x] Existing passed experiment bundles and strict repository validators remain green.
- [x] Fixed-surface independent review finds no blocker.

## Verification
```bash
python3 tests/regression/tooling/Test_BenchmarkResultValidator.py
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/benchmark/validate_benchmark_results.py --root tasks/evidence/METHOD-020/experiment/inputs --manifests-root benchmarks --strict
cmake --build --preset ci-vulkan --target IntrinsicLopFamilyGpuBenchmarkSmoke
ctest --test-dir build/ci-vulkan --output-on-failure -R 'LopFamilyGpuVulkanSmoke.UnavailableEnvironmentFailsClosed' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Treating `skipped`, `failed`, or `error` as a passing positive-claim result.
- Deleting or rewriting METHOD-020 run-001 instead of retaining its rejected audit trail.
- Weakening GPU/Vulkan opt-in labels or the default CPU test selector.
