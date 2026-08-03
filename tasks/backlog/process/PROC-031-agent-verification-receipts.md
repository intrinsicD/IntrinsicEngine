---
id: PROC-031
theme: H
depends_on: [CI-013, CI-018]
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
# PROC-031 — Bind agent workflow to unified verification receipts

## Goal

- Make agent edit loops and task completion consume the same admitted verifier
  profiles as CI and require a fresh, final-diff verification receipt before a
  task can be presented as complete.

## Non-goals

- No replacement of task claims, branch/worktree isolation, command evidence,
  handoffs, independent reviews, or claim-grade/protected custody.
- No autonomous lowering of test, sanitizer, capability, contract, or review
  requirements by an agent.
- No agent-specific selector or long-lived orchestration service.

## Context

- Owner: agent/task workflow docs, validators, and verifier integration; no
  production engine code.
- `CI-013` provides plans/receipts and `CI-018` provides the admitted impact
  policy. Agents are another consumer, not another authority.
- Separate claimed worktrees may share read-only content-addressed results, but
  ownership and dirty-surface evidence remain worktree-specific.
- This materially changes task authoring/completion workflow, so canonical
  agent docs, templates, skill mirrors, catalog routing, validators, and
  regression proofs must update together.

## Right-sizing

- Element: agent integration could add an agent-only selector, coordinator, or
  receipt facade.
- Simpler alternative: extend the existing task report schema with the unified
  verifier receipt and invoke the same short-lived CLI from claimed worktrees.
- Blast radius: agent/task docs, templates, validators, tooling tests, skills,
  and CI policy; no production engine code.
- Reintroduction trigger: a new agent adapter exists only when a present agent
  environment cannot invoke the common CLI/file contract and the adapter has a
  named owner/removal test.

## Control surfaces

- Config: task workflow profile and verifier profile mapping; policy-selected
  minimums are monotonic.
- UI: N/A.
- Agent/CLI: `watch` during edits, `plan/explain` for review, and final receipt
  binding during task evidence generation.

## Required changes

- [ ] Define deterministic task-risk/profile → minimum verifier-profile and
      required variant/capability mapping; agents may add checks but cannot
      subtract or downgrade the mapping.
- [ ] Integrate `verify --profile edit --watch` with claimed worktree changes,
      bounded cancellation, exact failure reproduction, and shared read-only
      cache use.
- [ ] Require a final verification plan and successful receipt generated after
      the final source/task/docs diff stabilizes; any subsequent content,
      graph, policy, inventory, environment, or receipt-artifact change makes
      completion evidence stale.
- [ ] Bind the verifier receipt into
      `tasks/evidence/<TASK-ID>/report.yaml` and validate exact source revision
      or dirty worktree content digest, plan, logical inventory/results, seeds,
      and artifacts.
- [ ] Preserve high-risk independent review and claim-grade/protected custody;
      reviewers see the exact verifier plan/receipt and cannot accept a stale
      surface.
- [ ] Add bounded diagnostics for missing prerequisites, unavailable
      capabilities, cache/service outages, broad fallbacks, failed actions, and
      genuinely untested surfaces.
- [ ] Update task template/format, workflow-evidence policy, AGENTS contract,
      review checklist, skill mirrors, validators, and fixtures as one
      prospective schema migration.

## Tests

- [ ] Add regression tests for profile mapping, attempted downgrade, extra
      checks, stale final diff, stale graph/policy/inventory/environment,
      receipt tampering, separate worktrees, cache hits, partial runs, and
      unavailable capability evidence.
- [ ] Prove a cache hit is recorded as reuse while the trusted original
      execution remains traceable and exact.
- [ ] Prove high-risk review and claim-grade/protected completion still reject
      missing or stale fixed-surface evidence.
- [ ] Run task, workflow-evidence, claim, skill-sync, and docs validators over
      updated and legacy fixtures.

## Docs

- [ ] Update canonical `docs/agent/*`, `AGENTS.md`, task templates, review
      checklists, onboarding examples, and `tools/agents/README.md`.
- [ ] Re-run `tools/agents/sync_skills.py --write` and verify every generated
      mirror is current.
- [ ] Update the verification roadmap and process indexes without claiming CI
      cutover before `CI-019`/`CI-020`.

## Acceptance criteria

- [ ] Every enrolled non-micro task maps to a deterministic minimum verifier
      profile and required evidence classes; no caller can downgrade it.
- [ ] Completion evidence is bound to a successful receipt for the exact final
      surface and becomes stale after any relevant change.
- [ ] Local/agent and CI plans for identical inputs normalize to the same graph
      actions, tests, reasons, and profile policy.
- [ ] Existing task claims, high-risk review, and claim-grade/protected custody
      remain independently enforced.
- [ ] The schema migration, canonical docs, templates, validators, fixtures,
      and generated skill mirrors are synchronized in the same change.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tests/regression/tooling/Test.AgentVerificationReceipt.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Letting agent confidence, task wording, or cache availability lower a policy-
  selected verifier profile.
- Treating an old receipt or a receipt for a different worktree/diff as task
  completion evidence.
- Collapsing verifier receipts, task evidence, independent review, and
  experiment custody into one unauditable status.
- Modifying production engine code.
