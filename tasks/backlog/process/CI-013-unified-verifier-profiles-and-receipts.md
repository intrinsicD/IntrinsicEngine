---
id: CI-013
theme: H
depends_on: [CI-012]
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
# CI-013 — Add unified verifier profiles and receipts

## Goal

- Provide one repository entrypoint that deterministically plans and executes
  `edit`, `pr`, `merge`, and `deep` verification profiles from the versioned
  evidence graph and emits a revision-bound result receipt.

## Non-goals

- No selective omission beyond the checks already authorized by current
  policy; new profiles run in shadow/compatibility mode until `CI-018`.
- No build/test result cache, test sharding redesign, or workflow cutover.
- No resident daemon, plugin framework, RPC service, or general job scheduler.

## Context

- Owner: `tools/ci` verification planning/execution; no engine layer changes.
- Present consumers are local developers, agents, PR CI, and merge/deep CI, so
  one shared entrypoint is justified. Plain records/functions are the default.
- `CI-012` supplies the only graph authority. CTest commands and current
  workflows remain compatibility executors during this task.
- The result receipt complements task workflow evidence; it does not replace
  task claims, command receipts, fixed-surface reviews, or experiment custody.

## Right-sizing

- Element: a shared verifier can drift into a daemon, plugin system, or custom
  scheduler.
- Simpler alternative: one short-lived Python entrypoint over plain graph and
  receipt records, delegating execution to the selected build/test tools.
- Blast radius: `tools/ci`, tooling tests, and docs; current workflows call it
  only in shadow and no engine layer imports it.
- Reintroduction trigger: an RPC/daemon seam is reconsidered only when a
  present remote executor cannot consume the file/CLI contract and its second
  caller is named in an active task.

## Control surfaces

- Config: checked-in profile policy with schema/version and deterministic
  preview validation.
- UI: N/A; IDEs may invoke the CLI but do not own policy.
- Agent/CLI: `plan`, `run`, `explain`, and bounded `watch` operations.

## Required changes

- [ ] Implement one `tools/ci/verify.py` entrypoint with explicit `plan`,
      `run`, `explain`, and `watch` commands and the four named profiles.
- [ ] Make planning side-effect-free and machine-readable before any configure,
      build, test, cache, or external action begins.
- [ ] Bind each plan to base/head or working-tree content, graph/schema digest,
      policy version, preset/variant identities, selected actions/tests,
      reasons, resources, and fallback disposition.
- [ ] Emit an append-safe receipt containing exact argv, statuses, per-logical-
      case results, skips, seeds, timings, diagnostics/artifact digests, and
      plan/environment identities.
- [ ] Reject stale plans/receipts when the diff, graph, policy, inventory,
      environment, or selected inputs change.
- [ ] Adapt the current touched-scope and CTest commands behind the verifier so
      compatibility output can be compared without duplicating policy.
- [ ] Keep `watch` bounded to repository changes and cancellation-safe; it must
      not introduce a long-lived shared service or hidden writer.

## Tests

- [ ] Add golden-schema and round-trip tests for every profile, dirty and clean
      revisions, no-change plans, broad fallback, cancellation, partial
      execution, and stale receipt rejection.
- [ ] Prove plan output is deterministic and `explain` accounts for every
      selected action/test and every broadening decision.
- [ ] Compare compatibility executions with the current commands for exact
      selected inventory and logical result status.
- [ ] Prove an agent or caller can add explicit checks but cannot lower the
      policy-selected profile or remove mandatory actions.

## Docs

- [ ] Document the CLI/profile/receipt schemas and reproduction path in
      `tools/ci/README.md` and `docs/architecture/test-strategy.md`.
- [ ] Update the roadmap with the implemented compatibility surface; do not
      claim selection, cache, or workflow cutover.

## Acceptance criteria

- [ ] Local, agent, PR, and merge/deep callers can preview the same normalized
      plan from the same graph and inputs.
- [ ] Every completed or partial run has a deterministic receipt with per-case
      diagnostics and an exact reproduction command.
- [ ] Changed inputs invalidate stale receipts and no caller can downgrade a
      mandatory profile/action.
- [ ] Current authoritative routes and results remain unchanged while the new
      entrypoint runs in shadow/compatibility mode.

## Verification

```bash
python3 tools/ci/verify.py plan --profile edit --base-ref origin/main --head-ref HEAD --output build/verification/edit-plan.json
python3 tools/ci/verify.py plan --profile pr --base-ref origin/main --head-ref HEAD --output build/verification/pr-plan.json
python3 tests/regression/tooling/Test.VerifyProfiles.py
python3 tests/regression/tooling/Test.VerificationReceipt.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Copying workflow shell command lists into the verifier as an independent
  source of truth.
- Treating a planned or cached action as executed evidence.
- Accepting an unbound receipt after its source, graph, policy, environment, or
  test inventory changes.
- Modifying production engine code.
