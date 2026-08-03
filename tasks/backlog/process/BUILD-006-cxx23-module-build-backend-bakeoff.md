---
id: BUILD-006
theme: H
depends_on: [CI-013, CI-014, BUILD-005]
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# BUILD-006 — Run a C++23-module build and cache backend bake-off

## Goal

- Select the simplest build/action-cache backend that meets the declared edit,
  PR, merge, correctness, invalidation, portability, diagnostics, and operating
  cost gates for IntrinsicEngine's C++23 named-module graph.

## Non-goals

- No predetermined adoption of REAPI, Bazel, sccache, a remote executor, or a
  custom build system.
- No performance claim from unmatched commits, runners, cache states, or test
  inventories.
- No permanent dual-backend maintenance and no engine source optimization.

## Context

- Owner: build backend and action-cache integration; no engine layer change.
- The control is the current preset-driven CMake/Ninja/Clang path with the
  `BUILD-005` environment/action identity. Candidates must include the smallest
  viable module-safe cache extension and, when operationally available, one
  remote-action-native alternative.
- Named-module interface/layout invalidation, cross-root reuse, clean-vs-cached
  parity, IDE/debug tooling, diagnostics, dependency scanning, and rollback are
  killing gates.
- This is claim-grade because it will support a performance and capability
  decision. Freeze the protocol and thresholds before reading candidate
  results; negative and inconclusive outcomes remain first-class.

## Right-sizing

- Element: evaluating multiple backends could leave a permanent abstraction
  layer and dual maintenance path.
- Simpler alternative: use bounded experiment adapters, select one production
  path, and delete rejected adapters after the decision.
- Blast radius: build/config/cache tooling, benchmark evidence, CI, and docs;
  engine source is fixed across matched comparisons.
- Reintroduction trigger: another backend is evaluated only when a measured
  accepted-path threshold reopens and a budgeted candidate is operational.

## Backends

- Backend axis: current CMake/Ninja control, smallest viable content-addressed
  extension, and one operationally available remote-action candidate. A
  missing candidate is a recorded blocker, not an invented comparison.

## Required changes

- [ ] Freeze a claim-grade benchmark protocol with exact source revision,
      environment, fixtures/diffs, cache states, runner classes, repetitions,
      metrics, thresholds, killing gates, and cost accounting.
- [ ] Exercise implementation-unit, module-interface, header, generated input,
      CMake/preset, vcpkg/toolchain, shader, and no-op changes under cold, warm,
      clean-root, cross-root, and corrupted-entry conditions.
- [ ] Compare time to first failure/result, end-to-end profile wall time,
      executed action-seconds, bytes transferred/stored, cache hit/miss/error
      classes, and setup/maintenance burden.
- [ ] Prove exact build outputs, logical test inventory/results, sanitizer and
      capability identities, and module invalidation against the control.
- [ ] Evaluate local IDE/debugger/compile-commands support, failure explanation,
      offline behavior, service availability, security/trust model, and
      rollback complexity.
- [ ] Record one retain/adopt/reject decision with explicit rationale. If the
      selected backend is hard to reverse and surprising, add an ADR only after
      the evidence exists.
- [ ] Bind the selected backend and its version/configuration to the evidence
      graph; delete experimental adapters that were rejected unless a named
      follow-up owns them.

## Tests

- [ ] Run every invalidation fixture from `BUILD-005` against each candidate
      and reject any false hit, false miss outside the declared allowance, or
      stale BMI/object/link result.
- [ ] Compare exact test inventories and per-case status across control and
      candidate outputs for all required variants exercised by the protocol.
- [ ] Inject cache corruption, unavailable service, partial upload/download,
      cancellation, and retry scenarios and prove deterministic safe fallback.
- [ ] Collect the predeclared comparable sample population and validate all
      benchmark/result payloads without selective exclusions.

## Docs

- [ ] Publish the frozen protocol, result bundle, cost/operations assessment,
      limitations, negative evidence, and decision under the benchmark/report
      policy.
- [ ] Update build/setup/CI docs and the verification roadmap with the selected
      backend or the explicit decision to retain the control.
- [ ] Add an ADR only if the final choice meets all three ADR criteria.

## Acceptance criteria

- [ ] Every candidate is evaluated on the same exact revision, environment,
      workload identities, and predeclared gates.
- [ ] The selected path has zero unexplained correctness/inventory/invalidation
      divergence and a tested rollback/fallback.
- [ ] Adoption meets the predeclared latency and executed-work benefit after
      storage, transfer, setup, and maintenance costs; otherwise the current
      path is explicitly retained.
- [ ] Claim-grade custody, portable result bundle, and independent audit pass
      before any performance/capability statement is promoted.
- [ ] No rejected experimental backend remains a permanent maintenance path.

## Verification

```bash
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/ci/run_build_backend_bakeoff.py --protocol tasks/evidence/BUILD-006/experiment/protocol.yaml
python3 tests/regression/tooling/Test.BuildBackendParity.py
python3 tools/agents/workflow_evidence.py validate --root . --require-complete BUILD-006
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Choosing a backend before the frozen matched comparison completes.
- Excluding failed/cold/invalidation samples after results are visible.
- Trading correctness, debuggability, offline fallback, or cache trust for a
  wall-time result.
- Maintaining two production build authorities after the decision.
