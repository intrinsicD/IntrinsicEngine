---
id: PROC-023
theme: H
depends_on: []
---
# PROC-023 — Canonicalize skill-body content that outgrew its docs/agent source

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

- [ ] For each of the three skills, classify every body section as
      "summarizes source" / "extends source" / "exists only here".
- [ ] Promote or declare each "extends/only-here" section per the two
      resolutions above; update `docs/agent/*` and re-run
      `sync_skills.py --write` where content moves.
- [ ] Record the chosen model per skill in the skills `README.md` authority
      section.

## Tests

- [ ] `python3 tools/agents/sync_skills.py --check` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [ ] Affected `docs/agent/*` files and skills `README.md`.

## Acceptance criteria

- [ ] Every normative rule in the three skill bodies either appears in a
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
