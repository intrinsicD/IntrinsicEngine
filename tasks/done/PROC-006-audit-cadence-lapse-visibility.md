---
id: PROC-006
theme: H
depends_on: [PROC-004]
---
# PROC-006 — Audit cadence lapse visibility

## Goal
- Make lapses of the two recurring human audits (weekly agent-output audit, repo-state drift audit) mechanically visible — in nightly CI output and in the generated session brief — without gating PR merges or adding external side effects.

## Non-goals
- No PR-blocking behavior: both audits are explicitly non-CI-enforced by design (`REVIEW-001`/`REVIEW-002`); this task only surfaces lapses.
- No automated issue/notification creation — for a single-human reviewer pool that adds noise, not signal.
- No changes to the audit checklists themselves.

## Context
- Owner/layer: agent process tooling (`tools/agents/`), `nightly-deep.yml`, session brief integration. No engine code.
- Today nothing notices a lapsed cadence: the agent-output audit targets roughly weekly windows ("missing a week is not a failure; the next reviewer extends the window") and the drift audit every 2–4 weeks, with reports at `docs/reports/<YYYY-MM-DD>-agent-output-audit.md` and `docs/reports/<YYYY-MM-DD>-drift-audit.md`. If audits silently stop, the workflow's main defense against cross-PR drift decays unnoticed.
- The useful surface is what agents read daily — the session brief from `PROC-004` — plus nightly CI logs. Depends on `PROC-004` Slice B for the brief surface; the standalone checker and nightly step are implementable independently if `PROC-004` is delayed.
- Thresholds are intentionally laxer than the cadence targets so the signal means "lapsed", not "one week late": defaults 14 days (agent-output) and 42 days (drift), both configurable by flag.

## Required changes
- [x] Add `tools/agents/check_audit_cadence.py`: parse the newest `docs/reports/*-agent-output-audit.md` and `*-drift-audit.md` dates from filenames, compare against `--max-age-agent-output-days` (default 14) and `--max-age-drift-days` (default 42), print per-cadence status with the newest report path, and exit nonzero only under `--strict` (default is report-only).
- [x] Treat "no report exists yet" as a lapse with an explicit "no report found" message rather than an error.
- [x] Add a non-blocking `Report audit cadence status` step to `.github/workflows/nightly-deep.yml` running the checker in report-only mode.
- [x] Integrate with the session brief generator (`PROC-004`): `generate_session_brief.py` includes an "Audits" line per cadence, reusing the checker's `cadence_status` as an importable function. Deviation: the brief lists the last-report **date** (a tree fact) rather than ok/overdue — a date-relative status would change with the passage of time and break the deterministic CI freshness check; overdue judgment stays in the checker/nightly output.

## Tests
- [x] `python3 tools/agents/check_audit_cadence.py` runs against the current tree and reports both cadences without crashing.
- [x] Verify threshold logic with `--max-age-agent-output-days 100000` (ok) vs `--max-age-agent-output-days 0 --strict` (nonzero exit).
- [x] Verify the "no report found" path by pointing `--reports-dir` at an empty temporary directory.
- [x] After `PROC-004` integration: regenerate the brief and confirm the "Audits" lines appear and `git diff --exit-code tasks/SESSION-BRIEF.md` passes.

## Docs
- [x] `tools/agents/README.md` documents the checker, defaults, and the deliberate non-gating policy.
- [x] `docs/agent/agent-output-review-checklist.md` and `docs/agent/drift-audit-checklist.md` each gain one line noting that lapses surface via the cadence checker and session brief (no procedural change).
- [x] Re-run the skill mirror sync for the touched checklist references.

## Acceptance criteria
- [x] A lapsed cadence is visible in nightly CI output and (post `PROC-004`) in `tasks/SESSION-BRIEF.md` within one regeneration.
- [x] No PR check fails because of a lapsed audit.
- [x] Both audit checklists point to the visibility mechanism.

## Verification
```bash
python3 tools/agents/check_audit_cadence.py
python3 tools/agents/check_audit_cadence.py --max-age-agent-output-days 0 --strict; test $? -ne 0
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making any PR-gating workflow depend on audit recency.
- Auto-filing tasks, issues, or notifications from the checker.

## Completion

- Completed 2026-06-09 on branch `claude/agentic-workflow-analysis-kohifk`.
- Commit: the PROC-006 implementation commit on that branch (checker,
  nightly-deep report-only step, session-brief audits section, checklist
  notes).
- Verified: ok path against the real reports (2026-05-28 agent-output,
  2026-06-06 drift), `--strict` lapse exit with a zero-day limit, and the
  empty-reports-dir "no report found" path.
