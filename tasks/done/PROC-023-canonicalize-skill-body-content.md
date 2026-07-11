---
id: PROC-023
theme: H
depends_on: []
---
# PROC-023 — Canonicalize skill-body content that outgrew its docs/agent source

## Status

- Completed 2026-07-11 on branch `claude/agentic-workflow-tasks-xakyf9`.
- Maturity: `Retired` (docs/skill-surface only; no content changed).
- Commit: this local canonicalization-and-retirement commit.
- Resolution: each of the three skills was classified section-by-section; the
  only-here sections were resolved by **declaring them skill-canonical**
  (resolution (b), the discipline-skill model), since the Non-goals forbid
  content changes and the content is applied discipline, not new contract
  policy. Skill-canonical sections — `intrinsicengine-benchmark`:
  `Anti-patterns`; `intrinsicengine-method`: knowledge-graph claim→code aid and
  the maturity-taxonomy mapping; `intrinsicengine-docs-sync`: `Decision rules
  for common cases`. Each skill body now carries an `Authority (PROC-023)` note
  naming the split, and the skills `README.md` authority section records the
  model per skill. `sync_skills.py --check` is unaffected (SKILL.md bodies are
  hand-authored, not mirrored).

## Goal

- Restore the authority chain for the three mirror skills whose hand-written
  SKILL.md bodies now carry substantial content with no canonical
  `docs/agent/*` source: `intrinsicengine-benchmark` (151-line body vs
  42-line `benchmark-workflow.md`), `intrinsicengine-method` (147 vs 43), and
  `intrinsicengine-docs-sync` (129 vs 32).

## Non-goals

- No content changes — the anti-patterns, decision rules, and expanded
  procedure text are good; the problem is only where they canonically live.
- No changes to `sync_skills.py` mechanics beyond map entries if content
  moves.

## Context

- The declared authority chain (AGENTS.md > `docs/agent/*` > skills) assumes
  skill bodies summarize their sources. For these three, the body is 3–4×
  the source and includes rules (benchmark anti-patterns, docs-sync worked
  decision rules, method backend-policy detail) that exist nowhere else — a
  doc-only reader misses them, and nothing detects body to source drift.
- Two resolutions are acceptable per skill section: (a) promote the extra
  content into the `docs/agent/*` source and thin the skill body back to a
  summary, or (b) explicitly declare that section skill-canonical (the
  discipline-skill model) in both the skill and the skills README authority
  section.

## Required changes

- [x] For each of the three skills, classify every body section as
      "summarizes source" / "extends source" / "exists only here".
- [x] Promote or declare each "extends/only-here" section per the two
      resolutions above; update `docs/agent/*` and re-run
      `sync_skills.py --write` where content moves.
- [x] Record the chosen model per skill in the skills `README.md` authority
      section.

## Tests

- [x] `python3 tools/agents/sync_skills.py --check` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [x] Skills `README.md` authority section and the three `SKILL.md` bodies. No
      `docs/agent/*` source changed: resolution (b) declares the only-here
      sections skill-canonical rather than moving content, so no `--write`
      re-sync was required.

## Acceptance criteria

- [x] Every normative rule in the three skill bodies either appears in a
      `docs/agent/*` source or is explicitly declared skill-canonical.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Deleting procedure content to make the accounting easier.
- Weakening any checklist while moving it.
