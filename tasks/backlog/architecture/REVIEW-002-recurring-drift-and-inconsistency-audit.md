# REVIEW-002 — Recurring repo-state drift and inconsistency audit

## Goal

- [ ] Establish a recurring, agent-runnable **repo-state drift audit** that
  inspects the whole tree (not a window of recent commits) for accumulated
  drift between code, docs, tasks, generated inventories, and tracked
  migration exceptions, and produces a dated report under
  `docs/reports/<YYYY-MM-DD>-drift-audit.md`.
- [ ] Author `docs/agent/drift-audit-checklist.md` so any reviewer or agent
  can execute the same audit deterministically from the checklist.
- [ ] Run the first calibration audit and record the elapsed time so future
  cadences can calibrate against a ≤ 45-minute target.

## Non-goals

- Do not duplicate REVIEW-001's per-window human review of agent-authored
  commits. REVIEW-001 audits *recent commits*; this task audits *current
  repo state*.
- Do not gate PR-time CI on this audit. The cadence is on-demand or
  scheduled (e.g. via the `/loop` skill); CI noise here would defeat the
  point.
- Do not invent new validators when an existing tool under
  `tools/agents/`, `tools/docs/`, or `tools/repo/` already covers the
  signal. The audit composes existing tools and adds only the semantic
  spot-checks they cannot express.
- Do not fix drift inside this task. Findings are recorded and follow-up
  backlog tasks are filed; remediation happens in those tasks.

## Context

- Owner/layer: agent-program governance; affects `docs/agent/`,
  `docs/reports/`, and `tasks/`.
- REVIEW-001 (`tasks/done/REVIEW-001-...`) already covers a weekly
  human-led audit of the last week of agent commits against nine
  failure-mode rows. It is window-scoped and does not surface drift that
  builds up over many windows — for example, a `(planned)` marker placed
  six weeks ago against a feature that has since landed, or a layering
  allowlist entry whose `task:` field now points to `tasks/done/`.
- Existing static validators the audit composes:
  - `python3 tools/agents/validate_tasks.py --root tasks --strict`
  - `python3 tools/agents/check_task_policy.py --root . --strict`
  - `python3 tools/docs/check_doc_links.py --root .`
  - `python3 tools/docs/check_docs_sync.py --root .`
  - `python3 tools/repo/check_layering.py --root src --strict`
  - `python3 tools/repo/check_layering_allowlist_quality.py --root .`
  - `python3 tools/repo/check_test_layout.py --root . --strict`
  - `python3 tools/repo/generate_module_inventory.py --root src --out <tmp>`
    (compared against `docs/api/generated/module_inventory.md`).
- Drift categories the audit must surface (each becomes one row of the
  checklist). Numbering is stable so reports can reference rows directly.

  1. **Generated-inventory drift.** A fresh
     `generate_module_inventory.py` run produces output that differs from
     the committed `docs/api/generated/module_inventory.md`. Per
     `AGENTS.md` §9, the inventory must be refreshed after module
     surface changes.
  2. **Layering-allowlist exception drift.** Entries in
     `tools/repo/layering_allowlist.yaml` whose `task:` field references
     a task that now lives in `tasks/done/`, or whose `expires:` clause
     has clearly elapsed. Per `AGENTS.md` §13, exceptions must be
     bounded and tracked in `tasks/active/`.
  3. **Active-task branch drift.** A `tasks/active/*.md` file whose
     Status block references a branch that is no longer present locally
     or whose linked PR is merged/closed — the task should have moved to
     `tasks/done/` or been re-opened with a new branch.
  4. **Stale `(planned)` markers.** Documentation carries a `(planned)`
     marker against a feature/symbol that now exists in source. Per
     `AGENTS.md` §9, factual current-state replaces aspirational
     wording once the code lands.
  5. **Aspirational claim without marker.** Present-tense docs assert a
     behavior that `git grep` against current source cannot locate. The
     mirror of row 4. Same `AGENTS.md` §9 anchor.
  6. **Dead public seam.** A symbol exported by a `.cppm` module
     interface that has no non-test, non-self call site. Per
     `AGENTS.md` §5 and the per-PR review checklist, half-finished
     seams are a tracked failure mode; this row catches the steady-state
     accumulation of them.
  7. **Untracked TODO/shim drift.** `git grep` for `TODO|FIXME|XXX|HACK`
     under `src/` returns entries that do not reference an existing
     task ID, or `shim|backcompat|legacy|temporary` phrasing without a
     `tasks/active/` §13 exception record.
  8. **Naming inconsistency.** The same concept is rendered with
     divergent casing or punctuation across docs or modules (for
     example `RenderGraph` vs `Render_Graph` vs `render-graph` in
     prose). The audit samples; it does not attempt to enumerate every
     instance.
  9. **Cross-doc reference rot.** A doc cross-link names a file that
     `check_doc_links.py` does not flag but whose anchor or section
     header has since been renamed (`#some-old-anchor`). Sampling-based.

- The audit is recurring but the **task file is one-shot**: REVIEW-002
  installs the checklist and writes the calibration report, then retires
  to `tasks/done/`. Subsequent runs of the audit write new reports under
  `docs/reports/<YYYY-MM-DD>-drift-audit.md` without opening a new task.
- Suggested cadence: every 2–4 weeks, or on demand before a
  branch-cluster retirement. The `/loop` skill can invoke the checklist
  on an interval; nothing in CI enforces it.

## Required changes

- [ ] Author `docs/agent/drift-audit-checklist.md` containing the nine
  drift-category rows above. Each row records: a one-sentence
  definition; the concrete command(s) or `git grep` pattern that
  surfaces the signal; what counts as a `finding` vs `pass` vs
  `not-applicable`; and the fix pointer (which follow-up task type to
  open, or which existing tool to extend).
- [ ] Add a short cadence note to `docs/agent/contract.md` linking to
  the new checklist alongside the existing pointer to
  `agent-output-review-checklist.md`, and stating that REVIEW-002 is
  *additive* to REVIEW-001 (state audit vs. window audit).
- [ ] Cross-link the new checklist from `docs/agent/roles.md` under the
  same rotating reviewer role that owns REVIEW-001.
- [ ] Execute the first calibration audit on the current `HEAD` and
  record findings as `docs/reports/<YYYY-MM-DD>-drift-audit.md`. The
  report uses the same shape as
  `docs/reports/2026-05-17-agent-output-audit.md`: window block (here,
  the current commit SHA and date), one findings-table row per
  checklist row with `pass | findings | not-applicable`, follow-ups,
  and elapsed time.
- [ ] For every `findings` row in the calibration report, file a
  follow-up backlog task under the appropriate `tasks/backlog/<area>/`
  directory and link it from the report. Do not remediate inside this
  task.
- [ ] Retire this task to `tasks/done/` once the checklist, contract
  pointer, and calibration report have landed.

## Tests

- [ ] No new C++ code is produced. The checklist commands must run
  cleanly on a fresh `cmake --preset ci` build tree (where build
  artifacts are needed; most rows are git/python-only).
- [ ] `python3 tools/agents/validate_tasks.py --root tasks --strict`
  passes after this task file lands and after the checklist + report
  land.
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict`
  passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes
  (calibration report and checklist are link-clean).
- [ ] Calibration success criterion: every checklist row has an
  outcome; every `findings` outcome links to a tracked follow-up task;
  total elapsed time is recorded.

## Docs

- [ ] `docs/agent/drift-audit-checklist.md` created.
- [ ] `docs/agent/contract.md` updated with cadence pointer.
- [ ] `docs/agent/roles.md` updated with reviewer ownership note (same
  rotating reviewer as REVIEW-001).
- [ ] `docs/reports/<YYYY-MM-DD>-drift-audit.md` — calibration report.
- [ ] Optional: one-line pointer added to `tasks/backlog/README.md`
  under a "Recurring audits" subsection so future reviewers discover
  both REVIEW-001 and REVIEW-002 from the backlog index.

## Acceptance criteria

- [ ] Checklist file exists and covers all nine drift categories with
  a concrete command/pattern per row and a `pass | findings |
  not-applicable` outcome slot.
- [ ] Calibration report exists, runs in ≤ 45 minutes elapsed (recorded
  in the report), and every row has an outcome.
- [ ] At least one follow-up backlog task is filed if calibration finds
  any `findings` outcome; if calibration is clean, the report
  explicitly states that and the checklist records the clean baseline.
- [ ] Cadence is documented as recurring on demand or every 2–4 weeks
  but is **not** enforced by CI; missing a cycle is recorded as "no
  audit this cycle, next reviewer extends the window."
- [ ] Strict task-policy, task-validation, and docs-links validators
  pass on each commit landing this task.

## Verification

```bash
# Repo-state checks composed by the audit.
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_layering_allowlist_quality.py --root .
python3 tools/repo/check_test_layout.py --root . --strict

# Generated-inventory drift (row 1).
python3 tools/repo/generate_module_inventory.py \
    --root src --out /tmp/module_inventory.fresh.md
diff -u docs/api/generated/module_inventory.md /tmp/module_inventory.fresh.md \
    || echo "DRIFT: module inventory differs from a fresh regeneration."

# Allowlist exception drift (row 2). Sample pattern; the checklist
# spells out the full grep + tasks/done/ cross-reference.
grep -nE '^\s+task:' tools/repo/layering_allowlist.yaml \
    | awk -F'"' '{print $2}' | sort -u \
    | while read -r tid; do
        if [ -f "tasks/done/${tid}"*.md ]; then
            echo "DRIFT: layering allowlist references retired task ${tid}."
        fi
      done

# Untracked TODO/shim drift (row 7). Window the grep to src/ only; the
# checklist documents the noise threshold.
git grep -nE '(TODO|FIXME|XXX|HACK)([^A-Za-z0-9]|$)' -- 'src/**'
git grep -nE '(shim|backcompat|legacy|temporary)' -- 'src/**' 'docs/**'
```

The remaining rows (3 — active branch drift; 4 / 5 — `(planned)` and
aspirational; 6 — dead seams; 8 — naming inconsistency; 9 — cross-doc
anchor rot) are documented in the checklist with their specific
commands so reviewers do not have to derive them from this task file.

## Forbidden changes

- Mixing this audit introduction with production code changes, layering
  refactors, or doc rewrites of unrelated areas.
- Adding CI gates that fail PRs on drift findings. The cadence is
  humans/agents inspecting repo state on demand; CI enforcement belongs
  to dedicated follow-up tasks per signal, not to this audit.
- Authoring the checklist abstractly without running the first
  calibration audit. The first run is what proves the checklist is
  actionable end-to-end.
- Letting the calibration audit exceed ≤ 45 minutes; if the first run
  exceeds that, tighten the checklist (drop or window noisy rows) rather
  than relax the cadence.
- Treating the cadence as a permanent reviewer role; rotate it through
  the same pool as REVIEW-001.
- Remediating findings inside this task. Each finding becomes a
  separate backlog task in the appropriate `tasks/backlog/<area>/`
  directory; this task only files them.
