# Repo-state drift audit — 2026-06-06 (calibration)

This is the first calibration run of the repo-state drift audit driven by
[`docs/agent/drift-audit-checklist.md`](../agent/drift-audit-checklist.md),
installed by
[`REVIEW-002`](../../tasks/archive/REVIEW-002-recurring-drift-and-inconsistency-audit.md).
It validates that the nine-row checklist is actionable end-to-end against the
current repo state, as required by `REVIEW-002`.

It is **additive** to the window-scoped
[`agent-output-review-checklist.md`](../agent/agent-output-review-checklist.md)
(`REVIEW-001`): that audit reviews *recent commits*; this one audits *current
repo state*.

## Window

- State audited: current tree at commit `b7ab258`
  (`HARDEN-070: Drop dead null guards on reference-initialised helpers`),
  on branch `claude/friendly-mayer-122xY`, date 2026-06-06.
- Scope: whole tree (`src/`, `docs/`, `tasks/`, `tools/`, generated
  inventories, layering allowlist), excluding `src/legacy/` for the TODO/shim
  row per the checklist windowing rule.
- Build tree: `build/ci` (fresh `cmake --preset ci`, clang-20).

## Findings

| Row | Drift category | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | Generated-inventory drift | pass | `generate_module_inventory.py --root src` into a temp file `diff`s identically against `docs/api/generated/module_inventory.md` — no surface change is unsynced. |
| 2 | Layering-allowlist exception drift | pass | `check_layering_allowlist_quality.py --root . --strict` exits 0. All 81 rows point at `LEGACY-001` / `LEGACY-002`, both open backlog tasks; the `task: → tasks/done/` cross-reference loop prints nothing. (`LEGACY-002` is deliberately kept open as the allowlist umbrella owner pending a metadata-only rebind to the per-subtree `LEGACY-003..010` tasks seeded this session.) |
| 3 | Active-task branch drift | not-applicable | `tasks/active/` holds only `README.md`; there is no active task whose Status branch/PR could be stale. |
| 4 | Stale `(planned)` markers | pass | `git grep "(planned)"` over `docs/`+`src/` returns 4 hits, all in `agent-output-review-checklist.md` and the three prior `*-agent-output-audit.md` reports, where `(planned)` is discussed as a marker concept — not asserted against a now-landed feature. |
| 5 | Aspirational claim without marker | pass | `check_docs_sync.py --root .` (the factual-current-state gate) exits 0. Sampled present-tense README claims in touched areas (`src/runtime`, `src/graphics/renderer`) resolve to backing symbols/tests. Sampling-based. |
| 6 | Dead public seam | pass | Sampled the `RORG-036`-flagged single-consumer module `Extrinsic.Runtime.RenderWorldPool`: it has live non-test consumers (`Runtime.Engine.{cpp,cppm}`, `Runtime.RenderExtraction.{cpp,cppm}`), so it is not a dead seam. No dead exported seam found in the sample. Sampling-based. |
| 7 | Untracked TODO/shim drift | **findings** | Two untracked markers in promoted `src/`: (a) `src/core/Core.Filesystem.cppm:16` carries a bare `//TODO:` filewatcher/callback-registry design question (no task ID, "CallbaclRegistry" typo); (b) `src/runtime/Runtime.Engine.cppm:258` `GetStreamingGraph()` is `[[deprecated(... TaskGraph bridge is temporary)]]` and `src/runtime/README.md` calls it a "temporary compatibility bridge ... while migration is in progress", but neither names a removal task ID (unmet `AGENTS.md` §13). All other `TODO`/`temporary` matches are either task-ID-tracked (`TODO(GRAPHICS-018)`, `GRAPHICS-076E/076F — temporary ...`) or plain technical uses ("temporary staging buffer", "binding to a temporary"). |
| 8 | Naming inconsistency | pass | Probe for `Render_Graph` across `docs/`+`src/` returns zero files; the canonical spellings (`RenderGraph`/`render graph`) are used consistently in the sample. Sampling-based. |
| 9 | Cross-doc reference rot | pass | `check_doc_links.py --root .` reports no broken relative links (1600 checked). Only 3 anchor-bearing links (`](...#...)`) exist in `docs/`; sampled anchors resolve to current headers. Sampling-based. |

## Follow-ups

- Row 7 → [`HARDEN-078 — Track or resolve untracked TODO / temporary markers in promoted src`](../../tasks/archive/HARDEN-078-track-untracked-todo-temporary-markers.md).
  Records an owning task ID for the `GetStreamingGraph()` temporary bridge and
  resolves/tracks the `Core.Filesystem` TODO. Remediation is **not** done in
  this audit.

All other rows are clean (`pass`) or `not-applicable`; no further follow-ups
filed this cycle.

## Elapsed time

≈ 20 minutes (within the ≤ 45-minute target). Most rows are `git`/`python3`
one-liners; only Row 1 (inventory regen) and Row 6 (seam sample) touched build
state, and `build/ci` was already configured.

## Cadence note

The next run extends from this state. If a cycle is skipped, the next reviewer
records "no audit this cycle, next reviewer extends the window." This audit is
on-demand / every 2–4 weeks and is **not** CI-enforced.
