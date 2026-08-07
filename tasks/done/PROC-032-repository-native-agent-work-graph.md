---
id: PROC-032
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-AgentGraph"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-06T22:12:10Z"
contract_schema: 1
contracts:
  - repo.task-contract-discovery
  - repo.agent-work-graph
---
# PROC-032 — Repository-native agent work graph

## Status

- Completed and retired on 2026-08-07. `tools/agents/agent_work_graph.py`,
  the checked-in ten-node `intrinsic.review-diamond.v1` recipe, the
  Git-common-dir run state and hash-chained event trace, the
  `repo.agent-work-graph` contract, and twenty isolated regressions are in
  place, with `ci-docs` executing the recipe validator and the regression
  suite.
- Independent review revision 1 returned `revision-requested` with four
  blockers and one robustness finding. Revision 2 resolves all five on the
  final surface: `validate_recipe_data` now requires a standard-active
  write-capable node and a standard-active final surface binder; the live
  claim binding compares an exact per-acquire claim-record digest alongside
  owner/branch/worktree on `show`, every transition, and `abort`; the review
  surface is frozen when the write lane finishes and every downstream
  transition fails closed until that lane is reopened; `show`/`list`
  serialize their snapshot under the graph lock; and CLI task IDs are
  validated before any Git-common-dir path is resolved.
- The graph remains observability and control-flow evidence only. Task,
  claim, verification, independent-review, and experiment-custody authorities
  are unchanged, and `CI-012`, `CI-013`, and `PROC-031` retain the planned
  verifier-receipt integration.
- Follow-up [`BUG-144`](../backlog/bugs/BUG-144-work-graph-lock-breaker-and-claim-path-validation.md)
  owns the stale-lock-breaker and `task_claim` path-validation residuals found
  while auditing this task's final surface; neither is reachable through a
  reviewed workflow today.
- Completion commit: this retirement commit.

## Goal

- Add an operational, repository-native agent work graph that makes one
  claimed task's current step, ready work, bounded retries, review joins, and
  terminal disposition observable without replacing existing task, claim,
  verification, review, or experiment authorities.

## Non-goals

- No production changes under `src/`; Core `TaskGraph`, RenderGraph, and
  runtime streaming graphs remain unrelated execution mechanisms.
- No agent/model process launcher, daemon, database, RPC service, plugin
  framework, or external orchestration dependency.
- No generated topology, conditional model-authored edges, or concurrent
  write-capable agents in one worktree.
- No implementation of the verification evidence graph or unified verifier;
  `CI-012`, `CI-013`, and `PROC-031` retain those responsibilities.
- No change to the co-scientist experiment runner in this task.

## Context

- Owner: `tools/agents`, canonical agent workflow documentation, structural
  tooling regressions, and `ci-docs`; no engine layer changes.
- The user explicitly adopted the bounded repository-side work-graph direction
  after reviewing the knowledge-graph versus agent-graph analysis.
- Checked-in recipe data defines topology. Live state belongs under the Git
  common directory so sibling worktrees can inspect it without creating a
  tracked multi-writer status file. Task files and generated `SESSION-BRIEF`
  remain the durable scope/dependency authority.
- The work graph consumes an existing live task claim and workflow profile.
  It must never grant ownership, lower a profile, accept an independent gate
  from the writer, or substitute node status for a command receipt or review.

## Right-sizing

- Element: an agent work graph can become a general scheduler, framework, or
  second workflow database.
- Simpler alternative: one short-lived Python CLI over a strict JSON recipe,
  an atomic current-state JSON record, and an append-only JSONL event trace.
- Blast radius: `tools/agents`, its regression tests, canonical workflow docs,
  generated skill mirrors, task/process indexes, and `ci-docs`; no `src/`,
  CMake, runtime, or external service dependency.
- Reintroduction trigger: a framework or resident service is reconsidered only
  after a measured task corpus proves that plain-file checkpoint, replay, or
  human-gate operation is inadequate and a separate high-risk task names the
  present consumers.

## Control surfaces

- Config: schema-versioned checked-in work-graph recipe JSON.
- UI: N/A; human-readable and JSON CLI status are the presentation surfaces.
- Agent/CLI: validate, start/resume, list/show, begin, finish, reopen, note, and
  abort commands over a claimed task.

## Required changes

- [x] Add a strict schema-v1 recipe validator with unique node IDs, known
      dependencies, acyclic topology, workflow-profile activation, explicit
      permissions, bounded attempts, and ordered write-capable nodes.
- [x] Add atomic Git-common-dir run state plus an append-only hash-chained event
      trace bound to task, owner, branch, worktree, recipe digest, task profile,
      and source surface.
- [x] Implement deterministic node transitions, actor/permission checks,
      independent-review separation, bounded reopen with descendant
      invalidation, explicit claim-handoff resume, node-addressed conversational
      notes, abort, and terminal success only after every active node succeeds.
- [x] Add a checked-in review-diamond recipe with one write-capable implementer,
      parallel read-only architecture/verification/docs reviews, a findings
      join, profile-gated independent review, and final surface binding.
- [x] Add a canonical `repo.agent-work-graph` contract, route this task through
      it, and wire its executable proof into `ci-docs`.

## Tests

- [x] Add isolated temporary-Git-worktree regressions for recipe validation,
      claim enforcement, profile pruning, ready-state fan-out, writer and
      independent-review permissions, claim-handoff resume, bounded reopen,
      notes/event chaining, source staleness, abort, and successful terminal
      closure.
- [x] Run the default recipe validator and the complete agent-work-graph
      regression directly.
- [x] Run the existing task-claim and workflow-evidence regressions to prove
      their independent authorities remain unchanged.

## Docs

- [x] Document graph ownership, state, lifecycle, commands, failure semantics,
      and the boundary from knowledge/task/verification/production graphs in a
      canonical agent workflow page.
- [x] Update `AGENTS.md`, workflow evidence, onboarding, tools inventory,
      architecture navigation, process indexes, and generated skill mirrors.
- [x] Keep `CI-012`, `CI-013`, and `PROC-031` explicitly planned rather than
      describing verifier-receipt integration as operational.

## Acceptance criteria

- [x] A claimed non-micro task can start the default graph and expose exactly
      which nodes are ready, running, blocked, skipped by profile, or complete.
- [x] The tool rejects absent/stale/mismatched claims, invalid recipes,
      unauthorized writers, writer-as-independent-reviewer, illegal
      transitions, retry-budget exhaustion, recipe drift, and stale final
      source bindings with actionable diagnostics.
- [x] A recovered replacement claim can explicitly resume an unfinished run,
      preserving completed work and attempt history while invalidating any
      abandoned running node and its descendants.
- [x] A blocked findings join can reopen the single write lane, invalidate its
      descendants, and rerun the review diamond without losing the immutable
      event history.
- [x] Later ideas, constraints, findings, and decisions can be attached to the
      node where they must be honored without mutating graph topology.
- [x] The completed graph is observability and control-flow evidence only; all
      existing task, verification, independent-review, and experiment-custody
      completion gates remain independently enforced.
- [x] Strict structural validators and focused workflow regressions pass on
      the final surface.

## Verification

```bash
python3 tools/agents/agent_work_graph.py validate-recipe \
  --recipe tools/agents/work_graphs/review-diamond.v1.json
python3 tests/regression/tooling/Test.AgentWorkGraph.py
python3 tests/regression/tooling/Test.TaskClaim.py
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root . --strict
git diff --check
```

## Forbidden changes

- Treating work-graph state as task ownership, verification execution,
  independent-review acceptance, or claim-grade evidence.
- Letting a recipe or caller lower task profile, expand tool permissions, or
  run parallel write-capable nodes.
- Adding framework-shaped abstraction around one CLI/file implementation.
- Modifying production engine code or the planned unified verifier scope.
- Mixing unrelated workflow cleanup or co-scientist behavior changes into this
  task.
