---
id: CI-018
theme: H
depends_on: [CI-013, CI-014, CI-017]
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
# CI-018 — Admit hybrid impact-based verification selection

## Goal

- Admit a pull-request selection policy formed by the union of static build
  impact, compatible dynamic coverage impact, affected contract proofs, and
  risk sentinels only after matched full-control evidence demonstrates no
  unexplained quality miss.

## Non-goals

- No selective merge/deep matrix; merge groups retain every required full
  variant/capability class and deep retains expensive breadth.
- No coverage-only selection and no optimistic result from stale/mismatched
  coverage identities.
- No permanent manual exception list for unknown paths or missed dependencies.

## Context

- Owner: verifier impact policy and admission evidence; no engine layer
  changes.
- Static impact comes from `CI-014`; the quality oracle and revision-bound
  coverage/fault evidence come from `CI-017`; `CI-013` owns the only planner.
- The selected set is a union. A signal may add work but cannot subtract work
  required by another signal or by the profile's deterministic risk escalators.
- This task is claim-grade because it supports the safety and performance claim
  needed to stop running the full current PR matrix on every update.

## Right-sizing

- Element: hybrid selection could become a configurable rule engine with
  competing selectors.
- Simpler alternative: one deterministic set-union function over four graph
  projections inside the existing verifier, with explicit risk broadening.
- Blast radius: verifier policy, quality evidence, CI shadowing, tests, and
  docs; build/test authors do not call a new selection interface.
- Reintroduction trigger: another selection signal is added only when a
  retained miss demonstrates a gap and a scoped task supplies its proof and
  invalidation contract.

## Required changes

- [ ] Freeze a claim-grade shadow protocol and corpus covering implementation,
      module-interface, header, generated, shader/asset, CMake/toolchain/vcpkg,
      contract/task/policy, rename/delete, capability, unknown-path, and no-op
      route classes.
- [ ] Build the PR plan as static closure union compatible coverage-linked
      cases union affected catalog proofs union risk/cross-layer sentinels.
- [ ] Bind coverage edges to exact production compile identity, graph/schema,
      test inventory, environment, and source revision; mismatch broadens and
      refreshes rather than narrows.
- [ ] Define deterministic risk escalators for module/header/build/dependency,
      public contract, capability, test-policy, and unknown changes.
- [ ] Run the selected plan in shadow beside the complete control for the
      frozen historical/real-diff corpus and every deterministic fault/mutation
      seed from the protocol.
- [ ] Define a selection miss as any control failure, lost proof/capability,
      lost region/branch arm, or killed fault that the selected plan reports
      successful or absent; retain every miss and root-cause it.
- [ ] Predeclare focused/broad latency and executed-action thresholds and
      collect comparable samples without counting cache hits as executions.
- [ ] Enable authoritative PR omission only after zero unexplained selection
      misses, unchanged quality/reliability, and passing performance gates; a
      refuted result retains the safe broader route.

## Tests

- [ ] Add set-union, reason/explanation, stale-coverage, graph-change,
      risk-escalation, unknown-input, and no-tests-selected fail-closed tests.
- [ ] Inject failures behind each impact signal and prove the selected plan
      schedules the killing test/proof/capability sentinel.
- [ ] Replay the frozen corpus and compare exact logical statuses, quality
      signals, and required artifacts against the full control.
- [ ] Independently recompute selection-miss and performance summaries from raw
      results and audit the claim-grade bundle.

## Docs

- [ ] Document selection inputs, union semantics, risk escalators, freshness,
      fallback, miss definition, shadow protocol, and admitted/non-admitted
      disposition in test strategy and CI policy.
- [ ] Add supported or refuted safety/performance claims to the ARA ledger
      before promoting measured statements.
- [ ] Update the roadmap and retain links to all negative/miss evidence.

## Acceptance criteria

- [ ] Every selected action/case/proof has at least one graph explanation and
      every profile-mandatory signal can only add to the union.
- [ ] Missing, stale, ambiguous, or incompatible evidence selects the bounded
      broad route or fails closed.
- [ ] The frozen shadow corpus and fault seeds produce zero unexplained
      selection misses and no quality/capability/reliability regression.
- [ ] Focused and broad plans meet the predeclared wall-time and executed-work
      thresholds on comparable samples, or authoritative omission remains off.
- [ ] Claim-grade custody and independent audit pass before current full PR
      gates are reduced.

## Verification

```bash
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/ci/verify.py shadow --profile pr --protocol tasks/evidence/CI-018/experiment/protocol.yaml --output build/verification/selection-shadow
python3 tests/regression/tooling/Test.HybridImpactSelection.py
python3 tests/regression/tooling/Test.SelectionMissAccounting.py
python3 tools/agents/workflow_evidence.py validate --root . --require-complete CI-018
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Enabling selective omission before the frozen shadow/admission protocol
  passes.
- Letting coverage, labels, cache state, or an agent subtract a statically or
  contract-required check.
- Dropping or reclassifying a selection miss after its cause is discovered.
- Weakening the full merge/deep profiles to meet PR latency targets.
