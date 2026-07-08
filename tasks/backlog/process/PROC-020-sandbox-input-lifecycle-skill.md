---
id: PROC-020
theme: H
depends_on: []
---
# PROC-020 — Author the sandbox-input-lifecycle skill (playbook wave 2)

## Goal

- Author `intrinsicengine-sandbox-input-lifecycle`: the runtime frame-loop
  wiring pitfalls around input capture, window lifecycle, and edit-flush
  ordering that have regressed repeatedly in the sandbox.

## Non-goals

- No engine code changes.
- Vulkan frame-content debugging stays in
  `intrinsicengine-vulkan-frame-triage`; this skill owns input/lifecycle
  wiring only.

## Context

- Recurring regressions this skill would codify: ImGui capture must gate
  camera/gizmo/pick input (`BUG-017`, `BUG-036`); window-close must route
  `WindowCloseEvent` → `RequestExit()` and re-check before renderer work,
  with idle-wait-before-GPU-teardown ordering — broken three separate times
  (`BUG-027`, `BUG-037`, `BUG-054`); UI/gizmo edits after the fixed-step
  bundle need the pre-render transform flush (`BUG-024`); blocking decode on
  the platform poll thread stalls `PollEvents` (`BUG-021`
  drop-import); screen-space +Y-down and orbit-camera sign conventions
  (`BUG-020`, `BUG-039`→`BUG-040` fix-of-a-fix); HiDPI window-vs-framebuffer
  cursor scaling (`BUG-026`).
- Skill tier: cross-cutting discipline skill (SKILL.md-only).

## Required changes

- [ ] Author `tools/agents/skills/intrinsicengine-sandbox-input-lifecycle/SKILL.md`
      with the pitfalls above, each citing its evidencing retired task(s).
- [ ] Register the skill in the `intrinsicengine-core` routing table and the
      skills `README.md` discipline tier.

## Tests

- [ ] `python3 tools/agents/sync_skills.py --check` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [ ] Skills `README.md` discipline-tier table updated.

## Acceptance criteria

- [ ] Skill exists with valid frontmatter and is auto-discoverable.
- [ ] Every pitfall cites a retired task; no aspirational content.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Engine code changes.
- Duplicating the frame-triage invariants owned by the wave-1 skills.
