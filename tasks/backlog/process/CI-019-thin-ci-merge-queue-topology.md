---
id: CI-019
theme: H
depends_on: [BUILD-006, CI-016, CI-018]
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
# CI-019 — Make CI thin and run full confidence once per merge group

## Goal

- Replace duplicated workflow policy with thin lifecycle wrappers around the
  unified verifier, build each required variant once, and run the complete
  confidence matrix once per merge-group candidate while preserving stable
  terminal checks and safe fallback.

## Non-goals

- No deletion of legacy workflow/tooling paths before the `CI-020` soak and
  retirement gate.
- No reliance on undocumented repository settings or unavailable runners/cache
  services.
- No merging on a PR-only selective result without a successful matching merge
  candidate result.

## Context

- Owner: `.github/workflows`, CI policy, protected repository settings, and
  verifier lifecycle adapters; no engine layer changes.
- `CI-018` admits PR selection; `BUILD-006` selects the build backend;
  `CI-016` supplies trusted exact reuse. This task composes them and must not
  reinterpret their policy in YAML.
- Workflow jobs report stable terminal contexts even when a profile contains no
  C++ work, a draft suppresses heavy work, or an implementation job is
  legitimately skipped.
- Merge queue and branch/ruleset settings are external state. Checked-in
  workflow support and owner activation/proof are both required before the
  topology is called operational.

## Right-sizing

- Element: workflow consolidation could create a repository-specific CI
  orchestration framework.
- Simpler alternative: keep YAML responsible only for event refs,
  permissions/secrets, and invoking the versioned verifier profile.
- Blast radius: workflows, verifier adapters, repository settings, CI tests,
  and docs; no engine or test assertion implementation changes.
- Reintroduction trigger: a CI-provider adapter is added only when a second
  present provider must run the same profile and cannot invoke the CLI/receipt
  contract directly.

## Control surfaces

- Config: versioned verifier lifecycle policy and repository settings runbook.
- UI: GitHub branch/ruleset/merge-queue administration only.
- Agent/CLI: workflow dispatch/reproduction and result-receipt inspection.

## Required changes

- [ ] Make PR, merge-group, main-fallback, manual, and scheduled workflows thin
      wrappers that resolve event refs then invoke a named verifier profile.
- [ ] Remove duplicated configure/build/test command policy from workflow YAML;
      stable result jobs validate the verifier receipt and implementation-job
      lifecycle rather than reconstructing selection.
- [ ] On PR updates, run structural plus admitted affected verification and
      cancel stale candidate work without cancelling protected main/manual/deep
      evidence.
- [ ] On `merge_group`, run the complete required unsanitized CPU, ASan, UBSan,
      promoted Vulkan, Release/SLO, and cheap-slow matrix once for the batch.
- [ ] Build each variant/action identity once and fan out exact artifacts or
      action-cache results; prohibit independent jobs from recompiling the same
      variant closure without a recorded reason.
- [ ] Keep scheduled deep coverage, stress/fuzz/mutation, complete slow,
      benchmarks, and vendor/driver lanes explicit and non-overlapping.
- [ ] Configure trusted cache write/read roles, artifact retention, stable check
      names, branch/ruleset required checks, merge queue, and a documented main-
      push fallback until protection is proven.
- [ ] Add a repository-settings audit that verifies the checked-in assumptions
      and fails the operational transition when permissions/settings are absent.
- [ ] Publish per-profile wall/queue time, executed action-seconds, CPU/GPU
      minutes, cache hits/misses/errors, duplicate-action count, and artifact
      digests.

## Tests

- [ ] Add static workflow tests for every event/draft/skip/cancel/failure
      transition, stable result context, exact ref resolution, and verifier
      profile invocation.
- [ ] Prove one action execution per variant identity across fanout and detect
      duplicate builds/tests mechanically.
- [ ] Exercise cache/service unavailability and prove the workflow falls back
      to correct execution rather than success or deadlock.
- [ ] Run PR and merge-group shadow candidates and compare exact required
      variants, logical results, quality receipts, and artifacts with the
      current topology.
- [ ] Audit live repository settings/required contexts/merge queue and retain a
      machine-readable activation result.

## Docs

- [ ] Update CI policy, contributor/agent reproduction guidance, workflow
      diagrams, stable contexts, settings runbook, failure triage, fallback,
      and cost telemetry.
- [ ] Update the roadmap to `Operational` only after both checked-in workflows
      and external settings are proven.

## Acceptance criteria

- [ ] Workflow YAML owns event/ref/secrets/permissions only; verification policy
      and commands come from the versioned verifier profile.
- [ ] Every candidate produces stable terminal check contexts that cannot hide
      a failed/cancelled/missing required implementation.
- [ ] A merge-group candidate executes the full required matrix once and each
      build/test action identity has no unexplained duplicate execution.
- [ ] Required settings, trust roles, merge queue, and main fallback are audited
      and reproducible; absent permissions remain a blocker rather than an
      undocumented manual step.
- [ ] Shadow candidates have exact result/quality parity and meet the declared
      profile wall-time and total-work gates before authority moves.

## Verification

```bash
python3 tests/regression/tooling/Test.WorkflowVerificationProfiles.py
python3 tests/regression/tooling/Test.WorkflowCandidateLifecycle.py
python3 tools/ci/audit_repository_verification_settings.py --root . --strict
python3 tools/ci/verify.py validate-receipt --profile merge --input build/verification/merge-receipt.json
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Reintroducing separate shell command matrices or selectors in individual
  workflow files.
- Allowing a skipped/missing/cancelled required implementation to yield a
  successful stable result.
- Removing the main fallback before merge protection is machine-proven.
- Claiming merge-queue operation from checked-in YAML alone.
