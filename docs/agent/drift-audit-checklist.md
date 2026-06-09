# Repo-state drift audit checklist

This checklist drives the **repo-state drift audit** introduced by
[`REVIEW-002`](../../tasks/done/REVIEW-002-recurring-drift-and-inconsistency-audit.md).
It inspects the **whole current tree** (not a window of recent commits) for
accumulated drift between code, docs, tasks, generated inventories, and tracked
migration exceptions.

It is **additive** to the window-scoped
[`agent-output-review-checklist.md`](agent-output-review-checklist.md)
(`REVIEW-001`): that checklist audits *recent commits*; this one audits
*current repo state*. Neither gates PR merges.

## How to run

- Cadence: on demand, or every 2–4 weeks, or before a branch-cluster
  retirement. The [`/loop`](../../README.md) skill can invoke it on an interval;
  nothing in CI enforces it. Missing a cycle is recorded as "no audit this
  cycle, next reviewer extends the window."
- Reviewer: the same rotating reviewer pool that owns `REVIEW-001` (see
  [`roles.md`](roles.md)). Not a permanent role.
- Budget: keep the run **≤ 45 minutes**. If a row is too noisy to triage in
  budget, tighten or window the row rather than relaxing the cadence.
- Output: write a dated report to `docs/reports/<YYYY-MM-DD>-drift-audit.md`
  using the same shape as the calibration report
  [`docs/reports/2026-06-06-drift-audit.md`](../reports/2026-06-06-drift-audit.md):
  a window block (commit SHA + date), one findings-table row per checklist row
  with outcome `pass | findings | not-applicable`, follow-ups, and elapsed time.
- Remediation happens in follow-up tasks, **not** in the audit. For every
  `findings` row, file a follow-up backlog task under the appropriate
  `tasks/backlog/<area>/` directory and link it from the report.

Most rows need only `git`/`python3`; rows that need build artifacts say so. A
fresh `cmake --preset ci` tree is sufficient where artifacts are needed.

## Drift categories

Row numbers are stable so reports can reference them directly.

### Row 1 — Generated-inventory drift

- **Definition:** a fresh `module_inventory.py` run differs from the committed
  inventory (`AGENTS.md` §9 requires the inventory refreshed after module
  surface changes).
- **Command:**
  ```bash
  python3 tools/repo/generate_module_inventory.py --root src --out /tmp/inv.fresh.md
  diff -u docs/api/generated/module_inventory.md /tmp/inv.fresh.md
  ```
- **Outcome:** `pass` if `diff` is empty; `findings` if it differs.
- **Fix pointer:** regenerate the inventory in the PR that changed the module
  surface, or file a docs-sync follow-up if the drift is pre-existing.

### Row 2 — Layering-allowlist exception drift

- **Definition:** a `tools/repo/layering_allowlist.yaml` row whose `task:` owner
  now lives in `tasks/done/` (or whose `expires:` clause elapsed). `AGENTS.md`
  §13 requires exceptions bounded and owned by an open task.
- **Command:**
  ```bash
  python3 tools/repo/check_layering_allowlist_quality.py --root . --strict
  grep -nE '^\s+task:' tools/repo/layering_allowlist.yaml \
    | sed -E 's/.*task:\s*"?([A-Za-z0-9-]+)"?.*/\1/' | sort -u \
    | while read -r tid; do
        ls tasks/done/${tid}-*.md >/dev/null 2>&1 \
          && echo "DRIFT: allowlist owner ${tid} is retired"; done
  ```
- **Outcome:** `pass` if the strict check exits 0 and the loop prints nothing.
- **Fix pointer:** rebind the row to the current per-subtree/per-area owner
  (metadata-only, `HARDEN-069`-style), or drop the row if the edge is gone.

### Row 3 — Active-task branch drift

- **Definition:** a `tasks/active/*.md` whose Status references a branch no
  longer present locally or a PR already merged/closed — the task should have
  moved to `tasks/done/` or re-opened with a new branch.
- **Command:**
  ```bash
  ls tasks/active/*.md 2>/dev/null | grep -v README   # enumerate
  # for each, cross-check the Status branch against: git branch -a
  ```
- **Outcome:** `not-applicable` if `tasks/active/` holds only `README.md`;
  otherwise `pass`/`findings` per the branch/PR cross-check.
- **Fix pointer:** retire the task to `tasks/done/` or re-open it with a live
  branch in its Status block.

### Row 4 — Stale `(planned)` markers

- **Definition:** documentation carries a `(planned)` marker against a
  feature/symbol that now exists in source (`AGENTS.md` §9: factual
  current-state replaces aspirational wording once code lands).
- **Command:**
  ```bash
  git grep -n "(planned)" -- 'docs/**' 'src/**'
  # for each hit, git grep the named symbol/feature in src/ to see if it landed
  ```
- **Outcome:** `pass` if every `(planned)` hit is either meta-discussion of the
  marker itself (checklists/reports) or genuinely un-landed; `findings` if a
  marker names a now-existing feature.
- **Fix pointer:** replace the marker with a current-state assertion in a
  docs-sync follow-up.

### Row 5 — Aspirational claim without marker

- **Definition:** present-tense docs assert a behavior that `git grep` against
  current source cannot locate (the mirror of Row 4).
- **Command:**
  ```bash
  python3 tools/docs/check_docs_sync.py --root .
  # plus: sample present-tense behavioral claims in touched-area READMEs and
  # git grep the asserted symbol/path in src/
  ```
- **Outcome:** `pass` if the docs-sync gate is clean and the sample finds a
  backing symbol/test for each claim; `findings` otherwise. Sampling-based.
- **Fix pointer:** add the missing implementation/test, or downgrade the claim
  to `(planned)` with an owning task.

### Row 6 — Dead public seam

- **Definition:** a symbol exported by a `.cppm` interface with no non-test,
  non-self call site (`AGENTS.md` §5 + the per-PR checklist treat half-finished
  seams as a tracked failure mode).
- **Command:**
  ```bash
  # sample recently-added exported symbols; for each:
  git grep -lE "import\s+<ExportingModule>" -- 'src/**' ':!<exporting file>*'
  # zero non-test consumers across several frames = candidate dead seam
  ```
- **Outcome:** `pass` if sampled seams have a live non-test consumer; `findings`
  if a sampled exported seam has only test/self callers. Sampling-based.
- **Fix pointer:** wire the consumer, or remove the seam, in a scoped follow-up.

### Row 7 — Untracked TODO/shim drift

- **Definition:** `git grep` for `TODO|FIXME|XXX|HACK` under `src/` (excluding
  `src/legacy/`) returns entries with no task ID, or
  `shim|backcompat|temporary` phrasing describing a migration bridge without a
  removal task ID (`AGENTS.md` §13).
- **Command:**
  ```bash
  git grep -nE '(TODO|FIXME|XXX|HACK)([^A-Za-z0-9]|$)' -- 'src/**' ':!src/legacy/**'
  git grep -nE '\b(shim|backcompat|temporary)\b' -- 'src/**' ':!src/legacy/**'
  ```
  A `TODO(TASK-ID)` / `// GRAPHICS-NNN — temporary ...` form is **tracked**
  (pass); a bare `TODO:` / `[[deprecated(... temporary ...)]]` with no task ID
  is a finding. "temporary staging buffer", "temporary scratch", "binding to a
  temporary" and similar plain technical uses are not findings.
- **Outcome:** `pass` if every match is tracked or a plain technical use;
  `findings` otherwise.
- **Fix pointer:** add the owning task ID to the marker, or file a follow-up to
  resolve/remove it.

### Row 8 — Naming inconsistency

- **Definition:** one concept rendered with divergent casing/punctuation across
  docs or modules (e.g. `RenderGraph` vs `Render_Graph` vs `render-graph`).
  Sampling, not enumeration.
- **Command:**
  ```bash
  git grep -cE "Render_Graph" -- 'docs/**' 'src/**'   # example probe; vary the concept
  ```
- **Outcome:** `pass` if the sampled concepts use one canonical spelling;
  `findings` if a concept is split across spellings in prose/identifiers.
- **Fix pointer:** normalize to the canonical spelling in a docs/rename
  follow-up.

### Row 9 — Cross-doc reference rot

- **Definition:** a doc cross-link names a file that `check_doc_links.py` does
  not flag but whose anchor/section header has since been renamed
  (`#some-old-anchor`). Sampling.
- **Command:**
  ```bash
  python3 tools/docs/check_doc_links.py --root .          # file-level rot
  git grep -ohE '\]\([^)]*#[a-z-]+\)' -- 'docs/**'        # sample anchor links
  # spot-check a few anchors against the target file's current headers
  ```
- **Outcome:** `pass` if `check_doc_links` is clean and sampled anchors resolve;
  `findings` if a sampled anchor points at a renamed header.
- **Fix pointer:** repair the anchor in a docs-sync follow-up.

## Related

- [`agent-output-review-checklist.md`](agent-output-review-checklist.md) — the
  window-scoped weekly audit (`REVIEW-001`) this checklist complements.
- [`contract.md`](contract.md) — expanded agent contract and audit cadence.
- [`roles.md`](roles.md) — reviewer rotation.
- [`/AGENTS.md`](../../AGENTS.md) — authoritative repository contract (§9 docs
  sync, §13 temporary exceptions).

## Lapse visibility

The cadence is not CI-enforced; lapses surface non-blockingly via
`python3 tools/agents/check_audit_cadence.py` (run nightly in
`nightly-deep.yml`) and the audits section of `tasks/SESSION-BRIEF.md`.
