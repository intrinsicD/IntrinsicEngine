---
id: BUG-144
theme: H
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug144"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-07T23:58:08Z"
contract_schema: 1
contracts:
  - repo.agent-work-graph
---
# BUG-144 — Work-graph stale-lock breaker can steal a live lock

## Status

- Completed and retired on 2026-08-08.
- Lock ownership is now identified rather than inferred from elapsed time. Each
  holder publishes a record naming its token, pid, host, and acquisition time
  inside the lock directory. A waiter breaks the lock only when the holder is
  provably gone (same host, dead pid), when the record is unreadable past a
  30 s grace window covering the instant between `mkdir` and publishing, or
  when a foreign-host record is older than 900 s. Elapsed time alone is never
  sufficient, which is exactly what let the previous 30 s `mkdir`-mtime
  comparison rob a slow writer.
- The liveness check is deliberately conservative: anything other than a clean
  `ProcessLookupError` is treated as alive, so the failure mode is waiting, not
  stealing.
- Release now compares the recorded token and refuses to remove a lock it no
  longer owns, raising an actionable "taken over by another holder" error
  instead of deleting its successor's lock and cascading. When the critical
  section itself raised, the release stays quiet so the original exception is
  not masked.
- `task_claim.py` `release` and `recover` validate `TASK_ID_RE` before building
  any claim path, matching `acquire`. The dead `.events.json` filter in
  `list_runs` is gone: event files end in `.events.jsonl` and the glob is
  `*.json`, so it never fired.
- Five regressions were added. Against the unfixed tools the cascade test and
  the error-masking test both error, the provably-gone liveness test fails, and
  all three task-claim traversal subtests fail.
- Honest limitation: the live-slow-holder regression pins the new contract but
  cannot fail against the unfixed tool, because that version publishes no
  holder record at all — its lock directory is empty, so there is nothing to
  liveness-check. It is kept as a contract test, and the cascade and liveness
  regressions are the ones that discriminate the fix.
- Verified: 24 work-graph, 9 task-claim, and the workflow-evidence regressions
  pass, along with the recipe validator and the strict task, policy,
  state-link, maturity-followup, skill-mirror, ARA-claim, doc-link,
  root-hygiene, and whitespace gates.
- Completion commit: this retirement commit.

## Goal

- Make the `agent_work_graph.py` graph lock safe for a writer that legitimately
  holds it longer than the stale-lock threshold, and validate task IDs in the
  remaining `task_claim.py` path-building commands, so the projection-mismatch
  window `PROC-032` closed cannot be reopened by lock theft.

## Non-goals

- No new locking framework, lock service, or dependency; the directory mutex
  stays a directory mutex.
- No change to work-graph topology, node semantics, transition rules,
  permissions, retry budgets, or terminal source binding.
- No change to claim ownership semantics, lease duration, or the recovery flow.
- No production changes under `src/`.

## Context

- Owner: `tools/agents`; no engine layer changes.
- `PROC-032` introduced `_state_lock` in `tools/agents/agent_work_graph.py`, a
  directory mutex over `<git-common-dir>/intrinsic-agent-work-graphs/v1/.lock`
  with a 5 s acquisition deadline and a 30 s stale-lock breaker.
- The breaker compares `lock.stat().st_mtime`, which is stamped once at
  `mkdir` and never refreshed. A writer that holds the lock for more than 30 s
  — plausible in `finish`, where `_source_snapshot` hashes the whole changed
  surface and `sha256_worktree_artifact` hashes declared artifacts — has its
  lock broken by a waiter. The victim's `finally: lock.rmdir()` then removes
  the *new* holder's lock, so the theft can cascade.
- That is the one remaining path to the reader-visible append-to-replace
  window that `PROC-032`'s independent review required to be closed. It was
  found while auditing that task's final surface, after its review, so it was
  deliberately not folded into `PROC-032`'s scope.
- Separately, `task_claim.py` validates `TASK_ID_RE` in `acquire` but not in
  `release` or `recover`, which build `root / f"{args.task_id}.json"` directly.
  Both currently fail closed at the `path.is_file()` guard and write nothing,
  so today this is a file-existence oracle inside the Git common directory
  rather than a traversal write — but the asymmetry is exactly the kind of gap
  the `agent_work_graph.py` fix already closed on its own surface.
- Also cosmetic, in the same file: `list_runs` filters
  `path.name.endswith(".events.json")` while event files are `.events.jsonl`
  and the glob is `*.json`, so the filter is dead code.

## Required changes

- [x] Make lock ownership verifiable rather than mtime-inferred — for example
      write a holder record (pid, host, acquired-at) inside the lock directory,
      refresh it while held, and let a waiter break the lock only when the
      recorded holder is provably gone or its record is genuinely stale.
- [x] Ensure a broken-lock victim cannot remove a lock it no longer owns, so
      the failure mode is a clear error rather than a cascading steal.
- [x] Apply `TASK_ID_RE` validation in `task_claim.py` `release` and `recover`
      before any path is built, matching `acquire`.
- [x] Remove the dead `.events.json` filter in `list_runs`.

## Tests

- [x] Add a regression proving a waiter does not break a lock whose holder is
      alive and slow past the stale threshold.
- [x] Add a regression proving a victim of a legitimate break does not delete
      the succeeding holder's lock.
- [x] Add negative `task_claim.py` `release`/`recover` tests for traversal-shaped
      task IDs.
- [x] Existing work-graph, task-claim, and workflow-evidence regressions stay
      green.

## Docs

- [x] Update the work-graph failure-semantics section of
      `docs/agent/workflow-evidence.md` if the lock contract or its diagnostics
      change, and re-sync the generated skill mirrors.

## Acceptance criteria

- [x] A writer holding the lock beyond the stale threshold keeps it, and a
      contending reader either waits or fails with an actionable timeout.
- [x] No sequence of contending readers and writers can produce a
      `state/event projection mismatch` for a reader.
- [x] `task_claim.py` rejects invalid task IDs in every command that resolves a
      claim path.
- [x] The full agent-work-graph, task-claim, and workflow-evidence regression
      suites pass.

## Verification

```bash
python3 tests/regression/tooling/Test.AgentWorkGraph.py
python3 tests/regression/tooling/Test.TaskClaim.py
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tools/agents/agent_work_graph.py validate-recipe \
  --recipe tools/agents/work_graphs/review-diamond.v1.json
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes

- Replacing the directory mutex with a lock service, daemon, or new dependency.
- Widening the stale threshold as the fix instead of making ownership
  verifiable.
- Changing work-graph topology, transitions, permissions, or source-binding
  behavior.
- Modifying production engine code under `src/`.
