---
id: PROC-015
theme: H
depends_on: []
---
# PROC-015 — Codify recurring diagnosis playbooks as skills (wave 1)

## Status

- Status: done (retired 2026-07-08). Authored in the same PR that seeded the
  task (agentic-workflow review batch on
  `claude/agentic-workflow-skills-review-tbnb0b`), following the
  PROC-001..009 fix-and-retire precedent.
- PR/commit: PR #1011 (`claude/agentic-workflow-skills-review-tbnb0b`),
  implementation commit `eed9853d`.
- Origin: agentic-workflow review (2026-07-08) mining of `tasks/done/`
  (601 retired tasks) for knowledge re-derived across multiple sessions.

## Goal

- Capture the three highest-cost recurring diagnosis playbooks from the
  retired-task history as self-contained discipline skills so future sessions
  load them instead of re-deriving them: Vulkan frame triage, GPU smoke
  authoring, and stale-build triage.

## Non-goals

- No changes to `intrinsicengine-diagnose`'s generic loop; the new skills are
  domain playbooks routed from it, not replacements.
- No new `docs/agent/*` source docs; these are cross-cutting discipline skills
  whose SKILL.md bodies are authoritative (same tier as
  `intrinsicengine-diagnose`).
- Wave-2 playbooks (import visibility, geometry IO format, sandbox input
  lifecycle) are owned by `PROC-018`/`PROC-019`/`PROC-020`, not this task.

## Context

- Evidence from the done-task corpus: the same Vulkan frame-debugging
  sub-lessons were re-derived across ≥14 tasks (`BUG-012`, `BUG-014`–`BUG-019`,
  `BUG-026`, `BUG-032`, `BUG-056`–`BUG-060`), with the bindless bridge slot
  defect shipping three separate times; the opt-in `gpu;vulkan` readback smoke
  shape was hand-rolled ~14 times (`BUG-024B`, `BUG-026B`, `BUG-035`,
  `BUG-060`, GRAPHICS-032D/033D/038E/076E/077E/078E/084C/089/090/092); and
  stale C++23-module/ccache artifacts consumed whole diagnosis sessions at
  least three times (`BUG-013`, the HARDEN-079/GEOM-021/022 module-split ASan
  ghost, the `BUG-016` retirement ICE).
- Owner/layer: skills surface under `tools/agents/skills/` only; no engine
  code.

## Required changes

- [x] Author `tools/agents/skills/intrinsicengine-vulkan-frame-triage/SKILL.md`
      (validation-first rule, per-stage readback bisection ladder from
      `BUG-016`, and the recurring engine invariants: bindless bridge slot
      ownership, render-id `+1` convention, integer-attachment clears, recipe
      clear propagation, QFOT pairing, transient-handle hygiene, Y
      conventions).
- [x] Author `tools/agents/skills/intrinsicengine-gpu-smoke-authoring/SKILL.md`
      (when a smoke is owed per the maturity taxonomy and the
      `BUG-024`→`BUG-024B` pattern, label policy, skip-vs-fail discipline,
      pixel-sampling idioms, `ci-vulkan` incantations, retirement citation
      discipline, and the `BUG-026` contract-test-fidelity warning).
- [x] Author `tools/agents/skills/intrinsicengine-stale-build-triage/SKILL.md`
      (clean-rebuild ladder, staleness signatures, related gotchas, and the
      do-not-fix-phantoms rule from `BUG-013`).
- [x] Register all three in the `intrinsicengine-core` routing table, the
      skills `README.md` discipline tier, and `intrinsicengine-diagnose`'s
      route-first note; add them to `intrinsicengine-handoff`'s suggested-skill
      list.

## Tests

- [x] `python3 tools/agents/sync_skills.py --check` passes (new skills are
      SKILL.md-only and require no reference mirrors).
- [x] `python3 tools/docs/check_doc_links.py --root .` passes for the new and
      edited files.

## Docs

- [x] `tools/agents/skills/README.md` updated (fifteen skills, six discipline
      skills, mirror table note) — shared with `PROC-016`.

## Acceptance criteria

- [x] All three skills exist with valid frontmatter (name + trigger-rich
      description) and are discoverable by skill auto-discovery.
- [x] Every claim in the skill bodies cites or paraphrases a retired task that
      evidences it; no aspirational content.
- [x] Routing surfaces (`intrinsicengine-core`, `intrinsicengine-diagnose`,
      skills README) name the new skills.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Engine code changes.
- Editing generated `references/` mirrors by hand.
- Weakening any check that currently runs strict in CI.
