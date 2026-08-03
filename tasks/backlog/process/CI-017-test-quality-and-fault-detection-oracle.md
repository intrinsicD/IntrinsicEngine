---
id: CI-017
theme: H
depends_on: [CI-012, CI-015]
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
# CI-017 — Establish a test-quality and fault-detection oracle

## Goal

- Establish an independent quality oracle that combines exact inventory and
  status, contract proofs, source/branch reach, mutation/seeded-fault
  detection, capability evidence, and reliability so faster plans cannot hide
  weaker tests.

## Non-goals

- No claim that line/region coverage alone establishes assertion quality.
- No mutation of third-party, generated, benchmark, test, or unsupported
  backend code and no requirement to run the complete mutation corpus on every
  PR.
- No automatic quarantine, retry-to-green, threshold weakening, or selective
  reporting of surviving mutations/faults.

## Context

- Owner: test-quality evidence and scheduled deep verification; no production
  behavior changes are shipped by this task.
- Existing CPU source-coverage parity is retained as reach evidence. Mutation
  and deterministic fault seeds add assertion-sensitivity evidence; contract,
  sanitizer, and non-skipped Vulkan results remain separate classes.
- This task is claim-grade because its baseline will support later claims that
  selection, grouping, and reuse preserve quality. The protocol, scope,
  summaries, thresholds, and killing gates must be frozen before results.
- Expensive breadth belongs in `deep`; PR and merge profiles consume stable
  sentinels and the last compatible quality baseline.

## Right-sizing

- Element: a quality oracle could become an opaque score framework or a second
  test runner.
- Simpler alternative: compose the existing exact coverage/inventory proofs
  with one deterministic mutation/fault runner and retain each evidence class
  separately.
- Blast radius: test/coverage tooling, scheduled CI, fixtures, evidence, and
  docs; production behavior is held fixed for baseline comparisons.
- Reintroduction trigger: a new quality signal gets its own adapter only when a
  present failure mode is not observed by the existing explicit dimensions.

## Required changes

- [ ] Freeze a claim-grade protocol defining production scope, exclusions,
      mutation operators, deterministic seeded-fault corpus, contract proof
      mapping, capability/sanitizer expectations, reliability measures,
      statistical units, summaries, thresholds, and killing gates.
- [ ] Integrate exact logical inventory/status and current source-region/
      branch-arm evidence with graph/action/product identities.
- [ ] Add a deterministic mutation/fault runner that records every generated,
      invalid, killed, survived, timed-out, and uncovered mutant/seed without
      overwriting negative outcomes.
- [ ] Define small stable fault sentinels for merge/PR profiles and complete
      eligible mutation breadth for the scheduled deep profile.
- [ ] Record flake, retry, timeout, skip, and quarantine rates by exact case and
      environment without retrying failures into success.
- [ ] Validate required ASan, UBSan, and non-skipped promoted-Vulkan evidence as
      distinct graph/result classes.
- [ ] Emit a revision-bound quality baseline and comparison report that rejects
      lost cases, proofs, regions/arms, mutation score, fault kills,
      capabilities, or reliability outside predeclared allowances.

## Tests

- [ ] Seed known detectable and equivalent/invalid mutations and prove
      classification, timeout handling, reproduction, and summary recomputation.
- [ ] Remove an assertion, contract proof, covered branch arm, capability
      result, and fault sentinel in fixtures and prove each loss blocks.
- [ ] Prove changes to production compile identity, test inventory, operators,
      seeds, toolchain, or environment invalidate comparisons.
- [ ] Run the frozen smoke/deep population and independently audit the portable
      result bundle before accepting the baseline.

## Docs

- [ ] Document what each quality signal proves and does not prove, the mutation
      scope/operator policy, schedules, reproduction, baselines, and surviving
      faults in test strategy and CI policy.
- [ ] Add any repeatable quality/capability statement to the ARA claim ledger
      before promoting it into current-state docs.
- [ ] Update the verification roadmap with the accepted oracle or the explicit
      refutation/blocker.

## Acceptance criteria

- [ ] The frozen protocol and all raw/summary/gate records pass claim-grade
      custody and independent recomputation audit.
- [ ] Every quality dimension is revision/product/environment bound and losses
      fail closed under a matched comparison.
- [ ] Mutation/fault results account for all attempted items, including
      invalid, equivalent-candidate, surviving, timeout, and infrastructure
      outcomes.
- [ ] PR/merge sentinels plus scheduled deep breadth are explicitly mapped; no
      expensive deep check silently disappears from all profiles.
- [ ] Coverage, mutation/fault, contract, sanitizer, Vulkan, and reliability
      signals remain distinct rather than collapsed into one opaque score.

## Verification

```bash
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/ci/quality_oracle.py run --profile smoke --output build/verification/quality-smoke
python3 tools/ci/quality_oracle.py run --profile deep --protocol tasks/evidence/CI-017/experiment/protocol.yaml --output build/verification/quality-deep
python3 tests/regression/tooling/Test.QualityOracle.py
python3 tests/regression/tooling/Test.MutationFaultAccounting.py
python3 tools/agents/workflow_evidence.py validate --root . --require-complete CI-017
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Improving a score by excluding surviving, timed-out, uncovered, or failed
  mutation/fault records after results are visible.
- Treating aggregate coverage percentage as a replacement for exact lost
  region/branch-arm comparison.
- Mutating third-party/generated/test code or production code outside the
  frozen eligible scope.
- Adding automatic retries or quarantine to manufacture a green baseline.
