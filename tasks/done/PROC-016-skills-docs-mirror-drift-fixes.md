---
id: PROC-016
theme: H
depends_on: []
---
# PROC-016 — Fix skills/docs mirror drift and dead routings

## Status

- Status: done (retired 2026-07-08). Fixed in the same PR that seeded the
  task (agentic-workflow review batch on
  `claude/agentic-workflow-skills-review-tbnb0b`).
- PR/commit: PR #1011 (`claude/agentic-workflow-skills-review-tbnb0b`),
  implementation commit `9c2379df`.
- Origin: agentic-workflow review (2026-07-08) of `AGENTS.md`, `docs/agent/*`,
  and the skill mirror surfaces.

## Goal

- Remove every mechanical drift and dead routing found by the 2026-07-08
  review: stale skills README, unmirrored review docs, the
  `intrinsicengine-zoom-out` model-routing dead end, the divergent
  test-category taxonomy, and the stale `contract.md` layering table.

## Non-goals

- No policy changes — every edit restores an existing rule's single source of
  truth or fixes a factual claim.
- Canonicalizing the hand-written SKILL.md bodies that outgrew their
  `docs/agent/*` sources is owned by `PROC-023`.
- CI wiring for `check_docs_sync.py` strict mode is owned by `PROC-021`.

## Context

- `tools/agents/skills/README.md` claimed 13 `docs/agent` files (there are
  14), embedded an obsolete `cp`-based resync script that skips
  `sync_skills.py` link rewriting (following it would fail the CI `--check`),
  and described the existing CI drift gate as hypothetical.
- `docs/agent/clean-workshop-review.md` and
  `docs/agent/drift-audit-checklist.md` were absent from `REFERENCE_MAP` in
  `tools/agents/sync_skills.py`, so a skills-only agent never saw them.
- `intrinsicengine-zoom-out` is `disable-model-invocation: true`, yet the
  `intrinsicengine-core` routing table and `intrinsicengine-handoff` told the
  model to consult it — a route the model cannot follow.
- Test-category taxonomy: `AGENTS.md` §7 defines categories
  `unit, contract, integration, regression, benchmark, slo` with
  `gpu/vulkan/glfw` as capability labels, but `contract.md`,
  `review-checklist.md`, and `architecture-review-checklist.md` listed `gpu`
  as a category and omitted `slo`.
- `contract.md`'s layering table lacked the `graphics/assets`,
  `graphics/vulkan`, and `platform` rows present in `AGENTS.md` §2.
- `docs/agent/prompt/prompt.md` was missing from the `AGENTS.md` "Related
  expanded docs" table despite uniquely holding loop-mode policy.
- `tasks/README.md` contradicted `prompt.md` on whether in-progress
  single-slice work may stay in `tasks/backlog/`.

## Required changes

- [x] Rewrite the stale sections of `tools/agents/skills/README.md` (counts,
      sync mechanism, symlink layout, remove the harmful `cp` script).
- [x] Add `clean-workshop-review.md` and `drift-audit-checklist.md` to
      `REFERENCE_MAP` under `intrinsicengine-review` and re-run
      `sync_skills.py --write`; surface both in the review skill's procedure
      table, references list, and frontmatter description.
- [x] Reword the zoom-out routing in `intrinsicengine-core` and
      `intrinsicengine-handoff` to "read the SKILL.md directly / suggest the
      user run it".
- [x] Align the test-category taxonomy in `contract.md`,
      `review-checklist.md`, and `architecture-review-checklist.md` with
      `AGENTS.md` §7.
- [x] Add the missing layering rows to `contract.md`.
- [x] Add the `docs/agent/prompt/prompt.md` row to the `AGENTS.md` "Related
      expanded docs" table.
- [x] Clarify in `tasks/README.md` that single-slice work may be worked and
      retired directly from `tasks/backlog/`.
- [x] Point the task-workflow skill's grilling section at the `grilling`
      skill as the authority for interview mechanics.
- [x] Fix the stale cadence-pointer note in
      `agent-output-review-checklist.md`.

## Tests

- [x] `python3 tools/agents/sync_skills.py --check` passes with the two new
      mirrors (files=16).
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [x] All edits above are docs/skills; no separate docs update owed.

## Acceptance criteria

- [x] Every `docs/agent/*.md` file is reachable through a skill mirror or the
      `AGENTS.md` routing table, and the skills README's claims match the
      tree.
- [x] No skill routes the model to a `disable-model-invocation` skill.
- [x] Exactly one test-category taxonomy exists across contract, checklists,
      and `AGENTS.md`.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Engine code changes.
- Hand-editing generated `references/` mirrors.
- Changing any policy while fixing its restatements.
