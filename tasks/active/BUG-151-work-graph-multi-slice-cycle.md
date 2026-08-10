---
id: BUG-151
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-bug151"
branch: "bug-151-work-graph-slice-cycles"
worktree: "/tmp/intrinsic-bug151-worktree"
claimed_at: "2026-08-10T23:10:10Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.agent-work-graph]
---
# BUG-151 — Work graph cannot advance a declared multi-slice task

## Goal
- Add an explicit, auditable next-slice transition so a claimed multi-slice
  task can begin a fresh bounded plan/write/review cycle without erasing prior
  attempts, weakening within-cycle review gates, or mixing unrelated commits
  into the new slice surface.

## Non-goals
- No unbounded writer reopen, attempt-counter mutation inside a failed review
  cycle, manual Git-common-dir state editing, or second run identity per task.
- No agent launcher, scheduler, daemon, database, plugin framework, or new
  workflow authority.
- No method, benchmark, engine, runtime, config, UI, GPU, or production change.
- No change to task claims, completion reports, independent-review custody, or
  experiment-custody authority.

## Context
- Symptom: METHOD-038 declared four slices, but its only writer node reached
  attempt `4/4` after two implementation checkpoints plus evidence/epilogue
  rebindings. `reopen` rejects the exhausted node and `start` rejects a second
  run for the same task, so the next declared corpus slice has no supported
  writer transition.
- Expected behavior: a clean, active, explicitly multi-slice task can advance
  to a new cycle whose repair attempts are bounded independently, whose source
  baseline starts at an exact clean commit, and whose prior cycle remains in
  the append-only event history with an honest reviewed or rolled-forward
  disposition.
- Impact: agents must otherwise bypass the graph, reset local state manually,
  contaminate another task's review surface, or prematurely split/retire a task
  after its claim-grade evidence identity is frozen.
- Owner: `tools/agents`, the repository agent-work-graph contract, regression
  tests, task/process docs, and generated skill mirrors. No engine layer is
  involved.

## Right-sizing
- Element: multi-slice continuation could grow into a general workflow
  scheduler or nested-run framework (right-sizing heuristics 2, 5, and 8).
- Simpler alternative: add one `advance-slice` command to the existing plain
  JSON/JSONL CLI. It records the prior node projection and source bindings,
  advances a scalar slice index, resets one caller-declared plan subgraph, and
  rebases that subgraph to the current clean commit.
- Blast radius: `tools/agents/agent_work_graph.py`, its isolated regression,
  canonical workflow/task docs, generated skill mirrors, and task records.
  No C++ module, engine target, external dependency, or runtime path changes.
- Reintroduction trigger: richer cycle topology is reconsidered only when a
  second checked-in recipe cannot express its repeated region through the
  existing `--from-node` rule and a separate high-risk task owns that evidence.

## Required changes
- [ ] Add a deterministic failing regression reproducing METHOD-038's
      succeeded-at-budget writer, unstarted review descendants, committed stale
      source binding, and rejected continuation.
- [ ] Add `advance-slice` with explicit owner, reason, and repeated-subgraph
      root; require a live exact claim, an active task with `## Slice plan`, a
      clean worktree, and a reset region containing every active writer and
      final surface binder.
- [ ] Preserve the one-run identity and append-only event chain while recording
      prior slice index, node projection, source digests/revision, disposition,
      accepted stale state, and the exact new slice baseline.
- [ ] Reset per-cycle attempts/status/artifacts only inside the repeated
      subgraph, retain node notes and ancestors, clear review/final bindings,
      and expose the current slice index in human/JSON status.
- [ ] Permit either a fully succeeded prior cycle or an explicitly authorized
      pre-review checkpoint with zero downstream review attempts; reject
      running nodes, failed/blocked nodes, started partial review, recipe/claim
      drift, dirty worktrees, inactive tasks, or insufficient reset regions.

## Tests
- [ ] Prove the exact exhausted-writer reproduction advances only with the
      explicit pre-review and stale-source acknowledgements, preserves prior
      event data, rebases to the clean commit, and starts the next plan at
      attempt zero.
- [ ] Prove normal fully succeeded cycles advance without recovery flags.
- [ ] Prove rejection for dirty state, backlog/done task state, missing slice
      plan, wrong actor/claim generation, recipe drift, running nodes,
      failed/blocked outcomes, partial review attempts, incomplete reset
      regions, and missing recovery acknowledgements.
- [ ] Re-run all agent-work-graph, task-claim, workflow-evidence, task-policy,
      and documentation synchronization regressions affected by the change.

## Docs
- [ ] Update `docs/agent/workflow-evidence.md` and
      `docs/agent/task-format.md` with slice-cycle semantics, safety checks,
      recovery flags, and the boundary from bounded repair attempts.
- [ ] Update the work-graph tool inventory/help where current CLI commands are
      enumerated, then regenerate skill mirrors and `tasks/SESSION-BRIEF.md`.
- [ ] Record the METHOD-038 reproduction and final disposition factually; do
      not describe the repair as method evidence.

## Acceptance criteria
- [ ] METHOD-038 can enter a fresh slice cycle from an exact clean post-repair
      commit without manual graph-state edits and without including BUG-151 in
      its new slice source surface.
- [ ] Prior node attempts, notes, source bindings, and reviewed versus
      rolled-forward disposition remain reconstructible from the hash-chained
      event history.
- [ ] A failed/blocked or partially reviewed cycle cannot use
      `advance-slice` to evade its repair budget or findings join.
- [ ] The next slice independently enforces the recipe's original per-node
      attempt limits and exact writer/final source bindings.
- [ ] Strict workflow/task/docs validators and the isolated regression suite
      pass on the final surface.

## Verification
```bash
python3 -m unittest tests.regression.tooling.Test.AgentWorkGraph
python3 -m unittest tests.regression.tooling.Test.TaskClaim
python3 -m unittest tests.regression.tooling.Test.WorkflowEvidence
python3 tools/agents/agent_work_graph.py validate-recipe \
  --recipe tools/agents/work_graphs/review-diamond.v1.json
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_docs_sync.py --root . --diff-mode \
  --base-ref HEAD^ --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_root_hygiene.py --root . --strict
git diff --check
```

## Forbidden changes
- Resetting or deleting prior attempts/events instead of recording a new slice.
- Advancing past a started, failed, blocked, or unresolved review cycle.
- Rebasing the next slice to a dirty or caller-invented source identity.
- Treating slice advancement as task completion, independent review,
  verification evidence, or experiment authorization.
- Editing METHOD-038 method/task/evidence bytes inside BUG-151.

## Maturity
- Target: `Operational` repository workflow behavior through the checked-in
  CLI and isolated regressions. No engine-backend `Operational` follow-up is
  owed.
