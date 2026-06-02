# HARDEN-077 — Enforce operational follow-ups for ambiguous maturity closures

## Goal
- Convert the review-only rule that `CPUContracted` is not `Operational` into a
  lightweight task-policy check and template convention so graphics, Vulkan,
  runtime-composition, and other backend-facing task closures must name an
  operational follow-up or explicitly state that no operational follow-up is
  owed.

## Non-goals
- Do not require GPU or Vulkan execution for every task.
- Do not reinterpret already-retired task history unless a deterministic,
  low-noise violation is found.
- Do not block CPU-only tooling, docs, or pure API hygiene tasks from closing at
  `CPUContracted` when no operational backend dimension exists.
- Do not change the maturity taxonomy names.

## Context
- Owning subsystem/layer: task workflow policy under `tools/agents/`,
  `tasks/templates/`, and `docs/agent/`.
- The task workflow already states that a `CPUContracted` task for graphics,
  Vulkan, runtime composition, pass command bodies, or similar backend-facing
  seams should name the `Operational` follow-up or explicitly record the
  deferral. Today this is enforced by review, not tooling.
- The repository quality assessment identified a hidden risk: strong process
  discipline can make partially operational systems look healthier than they
  are when a task has CPU/null coverage but no backend-labeled evidence.
- This task should make future ambiguity harder to introduce without forcing
  every WIP slice to become fully operational in one patch.
- Related audit record:
  [`docs/reviews/2026-06-02-repository-quality-assessment.md`](../../../docs/reviews/2026-06-02-repository-quality-assessment.md).

## Required changes
- [ ] Add a focused task-maturity follow-up check, either as a new
  `tools/agents/check_task_maturity_followups.py` script or as a clearly named
  pass inside `tools/agents/check_task_policy.py`.
- [ ] The check must inspect task files containing `## Maturity` and flag
  backend-facing ambiguous closures when they mention `CPUContracted` without
  either `Operational owned by <TASK-ID>` or an explicit `no Operational
  follow-up is owed` statement.
- [ ] Keep the first implementation deterministic: scope matching to task files
  whose title, path, or maturity/context text contains terms such as
  `graphics`, `render`, `Vulkan`, `GPU`, `runtime composition`, `pass command`,
  `asset ingest`, or `hot reload`.
- [ ] Report path, line number, detected maturity phrase, and the missing
  follow-up requirement.
- [ ] Add regression tests under `tests/regression/tooling/` for a failing
  graphics `CPUContracted` closure, a passing graphics closure with an
  operational follow-up, and a passing CPU-only tooling closure.
- [ ] Update `tasks/templates/task.md` so the optional `## Maturity` example
  explicitly shows both accepted forms: operational follow-up task ID, or
  explicit no-follow-up statement.
- [ ] Update `docs/agent/task-maturity.md` and
  `tools/agents/skills/intrinsicengine-task-workflow/SKILL.md` if the wording
  changes the author-facing policy.

## Tests
- [ ] Add a Python regression test, for example
  `tests/regression/tooling/Test.TaskMaturityFollowups.py`.
- [ ] The failing fixture must cover a backend-facing `CPUContracted` task with
  no operational follow-up.
- [ ] The passing fixtures must cover both accepted forms: named operational
  follow-up and explicit no-follow-up statement.
- [ ] Existing task-policy and task-validation checks continue to pass on the
  repository.

## Docs
- [ ] Update `docs/agent/task-maturity.md` with the exact accepted wording for
  `CPUContracted` backend-facing closures.
- [ ] Update `docs/agent/task-format.md` if the task authoring guide needs the
  same wording.
- [ ] Update `tasks/templates/task.md` optional `## Maturity` comment.
- [ ] If the skill text changes, update
  `tools/agents/skills/intrinsicengine-task-workflow/SKILL.md` to stay in sync
  with the docs.

## Acceptance criteria
- [ ] A backend-facing `CPUContracted` task fixture without an operational
  follow-up fails strict mode.
- [ ] A backend-facing `CPUContracted` task fixture with `Operational owned by
  <TASK-ID>` passes.
- [ ] A backend-facing `CPUContracted` task fixture with `no Operational
  follow-up is owed` passes.
- [ ] A CPU-only tooling task with `CPUContracted` maturity does not fail just
  because it has no backend dimension.
- [ ] Current repository task files pass after any targeted wording fixes.

## Verification
```bash
python3 tests/regression/tooling/Test.TaskMaturityFollowups.py
python3 tools/agents/check_task_maturity_followups.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Making backend-labeled tests mandatory for CPU-only tasks.
- Allowing vague wording such as "later" or "future work" as a substitute for a
  concrete follow-up task ID.
- Retrofitting broad historical task rewrites just to satisfy the checker.
- Changing maturity taxonomy definitions without a separate architecture/policy
  task and docs update.
