---
id: ARCH-018
theme: J
depends_on:
  - ARCH-017
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "agent/arch-018-feature-parity-goal"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T12:31:28Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
contract_review: "This task clarifies the repository mission, product convergence gate, and task-selection policy: complete Framework24 feature parity is the behavioral baseline, while IntrinsicEngine architecture and implementations remain independently chosen and improvable. It changes no engine API, data-domain, publication, backend, or control-surface contract."
---
# ARCH-018 — Clarify Framework24 feature parity and implementation freedom

## Status

- Completed and retired on 2026-08-13 at `Retired` policy maturity. The
  authoritative goal now treats feature parity as a behavioral coverage floor
  and explicitly permits independently better architecture and implementation;
  it makes no claim that parity is already achieved.
- Completion commit: this implementation/retirement commit.

## Goal

- Make complete user-facing Framework24 feature parity the unambiguous product
  baseline while stating that IntrinsicEngine may redesign architecture and
  implementations to achieve a more modular, reliable, usable, and performant
  result.

## Non-goals

- No engine, renderer, geometry-method, importer, benchmark, UI, or shader
  implementation changes.
- No parity, performance, capability, or visual-quality claim.
- No redesign of the agent workflow, evidence workflow, or its execution cost;
  that concern is intentionally deferred.
- No requirement to reproduce Framework24's internal architecture, APIs,
  algorithms, source organization, or OpenGL implementation.

## Context

- Owner: repository mission and Theme J work-selection policy.
- `ARCH-017` made Framework24 replacement the immediate gate, but its wording
  can be read as protecting the current architecture as a fixed design or as
  permitting selected golden workflows to substitute for full feature parity.
- The user clarified that Framework24 defines the feature and observable
  workflow baseline only. IntrinsicEngine's architecture and implementations
  are means, and should change when a better modular, extensible, reliable, or
  performant design requires it.
- Existing layer and evidence rules remain the current contract; changing them
  still requires an explicit reviewed architecture decision rather than being
  silently blocked or silently bypassed by this clarification.

## Required changes

- [x] State in the authoritative mission that full user-facing feature parity
      is mandatory and that “better” is an additional quality bar, not a
      substitute for missing features.
- [x] Distinguish behavioral parity from implementation parity: architecture,
      APIs, algorithms, source layout, and graphics implementation need not
      match Framework24 and may be redesigned through the normal architecture
      workflow.
- [x] Tighten the feature inventory and `REVIEW-004` so every registered
      user-facing row requires a working equivalent or strict superset with no
      lost user outcome.
- [x] Align Theme J and generic task-selection prose with the clarified goal.
- [x] Record the user-revised architecture decision without rewriting the
      sealed `ARCH-017` historical record.
- [x] Regenerate the session brief and synchronized skill mirrors.

## Tests

- [x] Pass strict task, task-state, ARA, documentation-link, docs-sync, and
      skill-sync validation.
- [x] Pass the task-policy regression surface affected by the mission and
      picker clarification.

## Docs

- [x] Keep `AGENTS.md`, `docs/agent/contract.md`, the product convergence
      scorecard, feature inventory, Theme J map, final audit, and generic
      session prompt mutually consistent.
- [x] Keep all wording factual: this policy correction does not claim current
      feature parity or implementation superiority.

## Acceptance criteria

- [x] A reader cannot interpret the goal as copying Framework24 internals.
- [x] A reader cannot interpret “better architecture” or six golden workflows
      as permission to omit a Framework24 user-facing feature.
- [x] Architecture and implementation improvement remain explicitly allowed,
      while current repository invariants can change only through their normal
      reviewed contract process.
- [x] No production, method, benchmark, shader, or test implementation file is
      changed.
- [x] Independent fixed-surface review accepts the exact high-risk policy
      clarification.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/agents/sync_skills.py --check
python3 tests/regression/tooling/Test.ValidateTasks.py
python3 tools/agents/generate_session_brief.py --check
```

## Forbidden changes

- No product implementation or benchmark work in this task.
- No copy-the-legacy-design requirement.
- No feature omission justified only by a different architecture or algorithm.
- No workflow-speed/process redesign in response to the deferred concern.
- No weakening of architecture, correctness, benchmark, review, or evidence
  gates to make the parity scorecard appear complete.
