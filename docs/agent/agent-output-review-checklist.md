# Agent-output Weekly Review Checklist

Use this checklist for the weekly human-led audit of agent-authored commits.
The cadence is *additive* to the per-PR review at
[`docs/agent/review-checklist.md`](review-checklist.md); it does not gate PR
merges and does not impose per-commit reviewer load. Each weekly sweep covers
roughly one week of slices, runs in ≤ 60 minutes, and either records "pass" for
each row or files a tracked follow-up task.

The audit is driven by [`REVIEW-001`](../../tasks/done/REVIEW-001-human-led-agent-week-review-cadence.md);
findings are saved under `docs/reports/<YYYY-MM-DD>-agent-output-audit.md`.

## How to run the audit

1. Pick a window (a calendar week, or a coherent sequence of completed
   tasks). Record the window's commit range:

   ```bash
   git log --pretty=format:"%h %ad %s" --date=short --no-merges \
       --since=<START> --until=<END>
   ```

2. For each row below, decide one of `pass`, `findings`, or
   `not-applicable`. `not-applicable` is allowed only when the window
   contains no commits that could surface the failure mode (e.g.,
   docs-only windows skip rows 4 and 8).

3. For every `findings` row, link an existing follow-up task or open a
   new backlog task under the appropriate domain directory and reference
   the audit report from that task.

4. Record total elapsed time at the bottom of the audit report so future
   cadences can calibrate against the ≤ 60-minute target. If a sweep
   exceeds the budget, tighten the checklist rather than relax the
   cadence.

5. Missing a week is not a failure; the next reviewer extends the
   window. The cadence is *not* enforced by CI; lapses surface
   non-blockingly via `python3 tools/agents/check_audit_cadence.py`
   (nightly) and the audits section of `tasks/SESSION-BRIEF.md`.

The cadence rotates: the reviewer is whoever picks up the audit that
week, not a permanent role. See [`docs/agent/roles.md`](roles.md).

## Failure modes

Each row has the same four parts: definition, how to find it, how to fix
it, and an outcome slot. The numbering is stable so audit reports can
reference rows directly.

### Row 1 — Silent scope creep

- **Definition:** a slice adds an unrelated cleanup, a minor refactor,
  or a "while we're here" diff that is not in the task's `Required
  changes`, `Tests`, or `Docs` sections.
- **How to find it:** `git diff --stat <merge-base>..<head>` and compare
  touched files to the task's required-changes checklist. Files outside
  the task's named scope are the candidates.
- **How to fix it:** open a follow-up backlog task for the unrelated
  cleanup, or revert it in a separate commit. See `AGENTS.md` §5 (keep
  patches small and scoped to one task) and §12 (scope matches exactly
  one task).
- **Outcome:** `pass | findings | not-applicable`.

### Row 2 — Decorative comments and docstrings

- **Definition:** multi-paragraph doc comments on internal symbols, or
  comments that explain *what* the code does where the names already
  carry that information.
- **How to find it:** `git diff <merge-base>..<head> -- '*.cpp' '*.cppm'
  '*.hpp' '*.h'` and look at added comment blocks. Hot spots: header
  comments on new files, `///` blocks on internal helpers.
- **How to fix it:** open a docs-touch backlog task asking the next
  toucher of the file to trim. Do not file a standalone cleanup task
  unless the comment density is unusually high.
- **Outcome:** `pass | findings | not-applicable`.

### Row 3 — Premature abstraction

- **Definition:** adding an interface, wrapper, factory, or builder for
  a single implementation; introducing a hook seam with no second caller
  in sight.
- **How to find it:** look for new abstract base classes, single-method
  interfaces, or `Make*`/`Build*` helpers added in the same commit as
  their first and only caller. `grep -nE 'class I[A-Z]|virtual .*= 0;'`
  on the added module list is a starting point.
- **How to fix it:** open a backlog task to fold the abstraction back
  into its concrete caller, or document why the seam exists (planned
  second caller, test-only mocking surface, layering boundary).
- **Outcome:** `pass | findings | not-applicable`.

### Row 4 — Documented-but-not-tested

- **Definition:** an architecture/README claim is updated without a
  corresponding test that would fail if the claim regressed.
- **How to find it:** `git diff <merge-base>..<head> -- 'docs/**'
  'src/**/README.md'` for added behavioral claims, then `git diff
  <merge-base>..<head> -- 'tests/**'` for matching tests. Claims about
  "X happens after Y" or "Z is never called from W" should have a
  contract or unit test pointer.
- **How to fix it:** open a follow-up task to add the missing test, or
  weaken the doc claim to factual current state. Per `AGENTS.md` §9,
  docs must be factual.
- **Outcome:** `pass | findings | not-applicable`.

### Row 5 — Defensive validation at internal boundaries

- **Definition (reviewer heuristic, not an `AGENTS.md` rule):** added
  validation on inputs from trusted internal callers, where no
  contract, interface boundary, or observed failure mode justifies the
  check. The contract trusts internal code and validates at system
  boundaries (user input, external APIs). The audit row asks the
  reviewer to consider whether each added check matches an explicit
  contract requirement, an interface boundary, or an actually-observed
  failure — not to enforce a non-existent policy.
- **How to find it:** look for new `if (!ptr) return;`, range/format
  pre-checks before calls into internal helpers, or AND-clauses that
  combine two validators where one already covers the contract.
- **How to fix it:** if the check duplicates a stronger validator
  upstream, remove it and add a regression test for the upstream
  contract; if it guards an externally-supplied input, leave it.
- **Outcome:** `pass | findings | not-applicable`.

### Row 6 — Untracked compatibility shims

- **Definition:** a temporary compatibility shim, deprecated alias, or
  backwards-compatibility branch added without a `tasks/active/`
  exception record, a specific removal task ID, and a time bound, per
  `AGENTS.md` §13. The audit flags shims that lack a removal follow-up
  or §13 exception record, not the existence of shims themselves.
- **How to find it:** `git grep -nE 'TODO|FIXME|deprecated|legacy|shim|
  backcompat|temporary'` on touched files, cross-referenced against
  `tasks/active/` for exception records.
- **How to fix it:** either add the §13 exception record + removal task
  ID, or remove the shim if the migration is already complete.
- **Outcome:** `pass | findings | not-applicable`.

### Row 7 — Ceremony without shipped value

- **Definition:** a window of "slice" commits that adds task files,
  retires task files, backfills commit references, and never produces a
  behavior change in the engine. Per-task ceremony (one task-file edit
  per landed slice) is expected and not a finding; the failure mode is
  a *whole window* of ceremony.
- **How to find it:** `git log --no-merges --shortstat <window>` and
  classify each commit as code, code+docs, docs-only, or task-only.
  Flag if the window contains zero code commits, or if code commits
  are dwarfed by task/docs maintenance.
- **How to fix it:** raise the cadence with the next reviewer and the
  task-program owner; consider whether the backlog needs unblocking
  work rather than more planning slices.
- **Outcome:** `pass | findings | not-applicable`.

### Row 8 — Half-finished implementations

- **Definition:** a slice introduces a seam, module, or scaffold but
  stops before the seam is exercised by a test or call site. The slice
  passes the per-PR checklist but the engine carries dead code.
- **How to find it:** for each new public symbol introduced in the
  window, `git grep -n '<symbol>'` to confirm at least one
  non-test-fixture call site exists, or at least one test that exercises
  the seam end-to-end. Symbols defined only in their own module +
  headers are candidates.
- **How to fix it:** open a follow-up task to wire the seam to its
  consumer, or delete the seam if its motivating use case has dropped.
- **Outcome:** `pass | findings | not-applicable`.

### Row 9 — Aspirational documentation without `(planned)` marker

- **Definition:** docs assert future-state as present-state without the
  `(planned)` marker. `AGENTS.md` §9 requires factual current-state docs
  unless clearly labeled.
- **How to find it:** `git diff <merge-base>..<head> -- 'docs/**'
  'src/**/README.md'` for assertions in present tense ("the runtime
  wires X", "the bundle activates Y") and cross-check against current
  source. Use `git grep -n '<asserted symbol>'` on `main` to verify.
- **How to fix it:** weaken the wording to factual current state, add
  the `(planned)` marker, or accelerate the corresponding code change.
- **Outcome:** `pass | findings | not-applicable`.

## Related

- [`/AGENTS.md`](../../AGENTS.md) — authoritative repository contract; §9 (docs
  sync), §12 (per-PR review checklist), §13 (temporary migration exceptions).
- [`docs/agent/contract.md`](contract.md) — expanded contract; the cadence is
  described in its "Weekly agent-output review cadence" section.
- [`docs/agent/review-checklist.md`](review-checklist.md) — per-PR review
  checklist (not replaced by this cadence).
- [`docs/agent/roles.md`](roles.md) — reviewer ownership for the weekly
  cadence.
- [`tasks/done/REVIEW-001-human-led-agent-week-review-cadence.md`](../../tasks/done/REVIEW-001-human-led-agent-week-review-cadence.md)
  — driving task; calibration window and acceptance criteria.
