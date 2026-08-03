---
id: CI-020
theme: H
depends_on: [CI-019, PROC-031]
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.task-contract-discovery
---
# CI-020 — Cut over verification and retire legacy policy

## Goal

- Make the admitted evidence graph/verifier the sole verification policy
  authority after a predeclared soak and rollback exercise, then delete the
  duplicate routing, discovery, setup, workflow, and agent-policy paths.

## Non-goals

- No deletion before every predecessor is operational and the final matched
  shadow/soak gates pass.
- No removal of GoogleTest or CTest as assertion/IDE compatibility surfaces.
- No rewriting of production tests or engine code merely to make the migration
  look complete.

## Context

- Owner: final CI/test/agent verification migration and legacy retirement; no
  engine layer changes.
- `CI-019` establishes hosted lifecycle authority and `PROC-031` establishes
  agent/task authority. This task closes dual authority rather than adding
  another feature.
- The soak protocol must be fixed before authority changes and must cover every
  route, required variant/capability, cache miss/hit/outage, cancellation,
  failure, and rollback class.
- CTest labels/aggregates may remain generated projections for IDE and direct
  reproduction, but they cannot retain independent routing semantics.

## Right-sizing

- Element: final cutover could preserve a permanent compatibility facade and
  therefore two policy authorities.
- Simpler alternative: retain only graph-derived CTest/IDE projections and
  delete every adapter that fails the deletion test after rollback is proven.
- Blast radius: verification/CI/agent tooling, test registration, docs, and
  migration ratchets; no production engine code.
- Reintroduction trigger: compatibility code returns only for a named present
  consumer that cannot use the authoritative projection and must carry its own
  removal task.

## Maturity

- Target: `Retired` for legacy verification policy. `Operational` verifier/CI
  evidence is owned by `CI-019` and `PROC-031`; no further `Operational`
  follow-up is owed after this task deletes dual authority.

## Required changes

- [ ] Freeze and execute a final soak protocol over representative PR updates,
      merge groups, main fallback, manual/deep runs, local/agent worktrees,
      every route class, cache states/outage, cancellation, intentional
      failures, and capability availability.
- [ ] Compare exact plans, logical inventories/statuses, contract proofs,
      coverage/mutation/fault/reliability signals, sanitizer/Vulkan/Release
      evidence, artifacts, and profile timing/total-work telemetry with the
      accepted controls.
- [ ] Promote one versioned verifier/profile/environment/schema bundle to
      authority and exercise rollback to the previous accepted bundle without
      using an unversioned build directory or hand-edited workflow.
- [ ] Remove hand-maintained touched-scope owner/path routing and obsolete
      compatibility adapters after their graph-derived replacements are
      proven.
- [ ] Remove labels/aggregate lists as primary policy, ordinary PRE_TEST
      rediscovery in required profiles, obsolete grouped replacement wrappers,
      duplicated timing/selection scripts, repeated workflow setup/commands,
      and stale agent command prose.
- [ ] Preserve CTest/IDE projections and focused reproduction commands only
      where they consume the authoritative graph and pass the deletion test.
- [ ] Add static ratchets that reject reintroduction of workflow command
      matrices, manual path routing, unversioned receipt/schema use, or a second
      profile authority.
- [ ] Reconcile or delete every temporary migration flag, adapter, generated
      artifact, allowlist entry, doc promise, and follow-up; no permanent
      compatibility shim remains.

## Tests

- [ ] Run and independently review the complete final soak; every expected
      failure must be observed by both control and candidate, with zero
      unexplained misses or quality/capability loss.
- [ ] Exercise authority promotion, rollback, cache disabled/unavailable,
      graph corruption/staleness, verifier failure, and repository-settings
      drift.
- [ ] Add ratchet tests for deleted scripts/path maps/PRE_TEST authority,
      workflow command duplication, task/agent stale commands, and independent
      label policy.
- [ ] Run full unsanitized CPU, ASan, UBSan, promoted Vulkan, Release/SLO,
      complete source coverage, mutation/fault smoke, ordinary slow, and
      structural validation on the final surface.
- [ ] Measure matched edit/PR/merge wall time and executed action-seconds and
      publish supported or refuted target dispositions without hiding misses,
      cache errors, retries, or failed samples.

## Docs

- [ ] Promote the verification architecture from `roadmap` to the correct
      current-state status and rewrite test/CI/agent docs around the sole
      authority.
- [ ] Update `AGENTS.md`, test/benchmark CI policy, workflow and setup docs,
      task templates/checklists/skills, and remove every obsolete command.
- [ ] Record final supported/refuted performance, quality, parity, and
      capability claims in the ARA ledger before current-state publication.
- [ ] Update process indexes, append the retirement narrative, and regenerate
      `tasks/SESSION-BRIEF.md` at closure.

## Acceptance criteria

- [ ] Every predecessor is retired with current evidence and the predeclared
      final soak reports zero unexplained plan/result/quality/capability miss.
- [ ] One versioned verifier/profile/environment/schema bundle is the sole
      policy authority across local, agent, PR, merge, and deep execution.
- [ ] Rollback is exercised, bounded, and does not depend on raw build-tree
      state or resurrect a second source of policy.
- [ ] Legacy manual routing, duplicated workflow commands/setup, ordinary
      required-profile PRE_TEST discovery, obsolete grouping/timing adapters,
      and stale agent commands are deleted or retained only as graph-derived
      compatibility projections with a documented deletion-test reason.
- [ ] Full required correctness, sanitizer, Vulkan, Release, coverage,
      quality, slow, structural, and task-evidence gates pass on the final
      source surface.
- [ ] Final latency/total-work targets are reported from comparable evidence;
      a refuted target remains visible and does not weaken quality gates.

## Verification

```bash
python3 tools/ci/verify.py run --profile merge --cache-mode off --output build/verification/final-merge.json
python3 tools/ci/verify.py run --profile deep --output build/verification/final-deep.json
python3 tests/regression/tooling/Test.VerificationAuthorityRatchet.py
python3 tests/regression/tooling/Test.WorkflowVerificationProfiles.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root . --require-complete CI-020
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Leaving two active selectors/profile definitions “for safety” after the
  retirement gate.
- Removing a full confidence or deep quality class because a latency target was
  missed.
- Retaining a compatibility adapter without a present consumer and explicit
  deletion-test justification.
- Presenting planned, cached, skipped, or stale evidence as executed proof.
