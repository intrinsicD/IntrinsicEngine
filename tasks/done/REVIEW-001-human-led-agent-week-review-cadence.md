# REVIEW-001 — Establish weekly human-led review of agent-authored slices

## Status

- Status: done.
- Owner/agent: Claude on `claude/backlog-task-agent-prompt-ECaZJ`; planning
  scaffolding (this task file) previously landed via PR #848 on
  `claude/review-001-agent-week-human-review-cadence`.
- Branch: `claude/backlog-task-agent-prompt-ECaZJ`.
- Started: 2026-05-16 (planning scaffolding); 2026-05-17 (cadence + calibration audit).
- Completed: 2026-05-17.
- Landed slice: authored
  [`docs/agent/agent-output-review-checklist.md`](../../docs/agent/agent-output-review-checklist.md)
  with the nine failure-mode rows; added the cadence pointer to
  [`docs/agent/contract.md`](../../docs/agent/contract.md) and the reviewer
  rotation note to [`docs/agent/roles.md`](../../docs/agent/roles.md); ran
  the first calibration audit on the GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091
  window and recorded the findings at
  [`docs/reports/2026-05-17-agent-output-audit.md`](../../docs/reports/2026-05-17-agent-output-audit.md).
  Audit completed in ≈ 15 minutes; eight rows passed, Row 5 (defensive
  validation at internal boundaries) recorded one historical finding that
  was already self-corrected within the same task window (`be7decf` →
  `c3fe597`) so no new follow-up task was filed.

## Goal
- [x] Establish a recurring, low-overhead cadence in which a human reviewer audits one week's worth of agent-authored commits against the `AGENTS.md` contract and the patterns that agentic workflows systematically miss (decorative scope creep, premature abstraction, "documented-but-not-tested," ceremony-without-shipped-value).
- [x] Produce a checklist of agent-specific failure modes and a written `docs/agent/agent-output-review-checklist.md` so any reviewer can run the same audit in ≤ 60 minutes per week.
- [x] Record the first audit's findings as the calibration baseline for the cadence.

## Non-goals
- Do not gate normal PR-time review on this cadence. PRs still merge per the existing per-PR review process (`docs/agent/review-checklist.md`).
- Do not impose extra reviewer load on every commit. This is a *weekly* audit of one week's window, not a duplicate per-PR pass.
- Do not replace the per-PR review checklist. This is an additive sweep against patterns the per-PR view does not surface well (per-commit scope is too narrow to spot multi-PR scope drift, decoration patterns, or method/test/doc imbalance).
- Do not block agent throughput. The cadence either silently passes or files specific follow-up tasks; it never directly blocks an in-flight branch.
- Do not introduce a new agent role. The reviewer is whoever picks up this task each week.

## Context
- Owner/layer: agent-program governance; affects `docs/agent/` and `tasks/`.
- Today's state (2026-05-16):
  - Git history shows 52 commits by `Claude`, 39 by `intrinsicD`, 17 by `dieckman` over the visible range (~5 days, 108 commits). Agent throughput is high.
  - Commit cadence is dominated by very small slices (single-task slices like "backfill commit reference," "remove stale subtree," "open the two operational-gate planning-gap fills"). This is excellent for per-PR review hygiene but creates a different problem: it is hard for any single reviewer to see whether a *week* of slices added up to landmark progress vs. ceremony-with-zero-output.
  - `AGENTS.md` §12 review checklist applies per-PR. There is no documented cadence for cross-PR pattern audits.
- Agent-specific failure modes worth auditing for:
  1. **Silent scope creep.** A slice adds an unrelated cleanup, a minor refactor, or a "while we're here" diff that is not in the task contract.
  2. **Decorative comments and docstrings.** Multi-paragraph doc comments on internal symbols; "explains what the code does" comments where the names already do that.
  3. **Premature abstraction.** Adding an interface or wrapper for a single implementation; introducing a factory or builder where a direct constructor would do; defensive fallbacks for impossible inputs.
  4. **Documented-but-not-tested.** Architecture doc claim updated without a corresponding test (`docs/architecture/graphics.md` is the prime offender — 793 lines of prose with no compile-checked link to behavior).
  5. **Defensive validation at internal boundaries.** *Reviewer heuristic — not an `AGENTS.md` rule.* The contract does not specify a validate-at-boundaries policy; the heuristic that internal callers should be trusted and validation lives at external system boundaries is a common code-quality guideline that agents (including this author) tend to over-apply or, conversely, under-apply by sprinkling defensive checks throughout. The audit row asks the reviewer to consider whether added validation matches an explicit contract requirement, an interface boundary, or actually-observed failure modes — not to enforce a non-existent policy.
  6. **Untracked compatibility shims.** Per [`AGENTS.md`](../../AGENTS.md) §12 review checklist: "Temporary compatibility shims are tracked with removal follow-up." Per §13, temporary exceptions are allowed only when documented in a current task under `tasks/active/`, with a specific removal task ID, time-bounded, and not creating new violations in promoted final layers; undocumented exceptions are policy violations. **The audit flags shims that lack a removal follow-up task ID or §13 exception record, not the existence of shims themselves.**
  7. **Ceremony without shipped value.** A week of "slice" commits that add task files, retire task files, backfill commit references, and never produce a behavior change in the engine.
  8. **Half-finished implementations.** A slice introduces a seam or scaffold but stops before the seam is exercised by a test or call site. The slice passes the per-PR checklist but the engine has dead code.
  9. **Documentation that is aspirational without the `(planned)` marker.** Per `AGENTS.md` §9: docs should be factual, not aspirational unless clearly labeled. Agents tend to assert future-state as present-state.
- A first calibration audit will validate the checklist against a real recent week of activity. Recommendation: pick the GRAPHICS-033E + GRAPHICS-033F + HARDEN-066 + RUNTIME-091 sequence (2026-05-14 → 2026-05-15) as the calibration window.

## Required changes
- [x] Author `docs/agent/agent-output-review-checklist.md` containing the nine failure-mode categories above, each with: a one-sentence definition, a "how to find it" hint (commands, files to look at), a "how to fix" pointer (typically a follow-up task), and a `pass | findings | not-applicable` outcome slot.
- [x] Add a short cadence note to `docs/agent/contract.md` linking to the new checklist and stating that the cadence is *additive* to the per-PR `docs/agent/review-checklist.md`.
- [x] Cross-link the new checklist from `docs/agent/roles.md` under whichever role owns the audit each week.
- [x] Execute the first calibration audit on the GRAPHICS-033E/F + HARDEN-066 + RUNTIME-091 window (2026-05-14 → 2026-05-15) and record findings as `docs/reports/2026-05-17-agent-output-audit.md`. Findings are short, structured against the checklist. The single `findings` outcome (Row 5) was already self-corrected within the same window (`be7decf` → `c3fe597`), so no new follow-up task was filed; the audit report records the historical pattern.
- [x] Record the calibration audit's elapsed time at the bottom of the report so future cadences can calibrate against the ≤ 60 minute target.

## Tests
- [x] No code is produced by this task. No automated tests.
- [x] Verification: `python3 tools/docs/check_doc_links.py --root .` passes; `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [x] First-audit success criterion: the report exists, every checklist row has an outcome, every `findings` row links to a tracked follow-up task or new backlog task. (Row 5 is the only `findings` outcome; the self-correction in `c3fe597` is the tracked remediation.)

## Docs
- [x] [`docs/agent/agent-output-review-checklist.md`](../../docs/agent/agent-output-review-checklist.md) created.
- [x] [`docs/agent/contract.md`](../../docs/agent/contract.md) updated with cadence pointer.
- [x] [`docs/agent/roles.md`](../../docs/agent/roles.md) updated with reviewer ownership note.
- [x] [`docs/reports/2026-05-17-agent-output-audit.md`](../../docs/reports/2026-05-17-agent-output-audit.md) — calibration audit report.
- [x] Optional: add a one-line cadence note to `README.md` "CI Status and Expectations" pointing at the checklist so first-time contributors discover it.

## Acceptance criteria
- [x] Checklist file exists and covers all nine failure modes with command/hint per row.
- [x] First calibration audit report exists and runs in ≤ 60 minutes elapsed (recorded in the report — ≈ 15 minutes).
- [x] At least one follow-up task is filed if the calibration audit finds any `findings` outcome; if it finds none, the report explicitly states that and the checklist is updated to record that the chosen window passed clean. *Outcome:* Row 5 produced a historical `findings` outcome that was already self-corrected within the same task window (`be7decf` slice 1 → `c3fe597` slice 2), so no new follow-up task was opened; the audit report explicitly records this and links the remediating commit.
- [x] The cadence is documented as recurring on a per-week basis but is *not* enforced by CI; missing a week is recorded as "no audit this week, next reviewer extends the window."
- [x] Strict task-policy and docs-links validators pass on each commit.

## Verification
```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing this review-cadence introduction with production code changes.
- Adding CI gates that block PRs on the audit. The cadence is humans reading commits; CI noise here would defeat the purpose.
- Authoring the checklist abstractly without running the first calibration audit. The first run is what proves the checklist is actionable.
- Letting the calibration audit balloon past ≤ 60 minutes; if the first run exceeds that, the checklist must be tightened, not the cadence relaxed.
- Treating the cadence as a permanent reviewer role; rotate it.
