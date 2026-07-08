---
id: PROC-018
theme: H
depends_on: []
---
# PROC-018 — Author the import-visibility-contract skill (playbook wave 2)

## Goal

- Author `intrinsicengine-import-visibility-contract`: the checklist a new or
  changed import/materialization path must satisfy so that a "successful"
  import is actually visible and selectable in the sandbox.

## Non-goals

- No engine code changes; the skill codifies invariants that already hold.
- No overlap with `intrinsicengine-geometry-io-format` (`PROC-019`), which
  owns the parser/exporter slice shape — this skill owns the runtime
  materialization/visibility contract.

## Context

- ~12 retired tasks are the same failure class: import succeeds but nothing
  visible or pickable appears (`BUG-022` non-manifold no-entity, `BUG-023`
  off-origin culling + no camera focus, `BUG-038` silent drop failures,
  `BUG-041`/`BUG-043`/`BUG-044`/`BUG-045`/`BUG-047`/`BUG-048`/`BUG-050`
  normals/UVs dropped or overwritten at four different stages,
  `ASSETIO-006`/`007`/`008`).
- The checklist was assembled piecemeal across those bugs: render-critical
  component parity with `ReferenceTriangle` (GeometrySources, RenderSurface,
  SelectableTag, VisualizationConfig, stable id), count-matched `v:normal`
  (authored preserved, area-weighted fallback), resolved `v:texcoord` policy,
  culling bounds + one-shot camera focus for off-origin geometry,
  post-process must not overwrite recomputed attributes, and
  receipt/queue/completion logging so failures are never silent.
- Skill tier: cross-cutting discipline skill (SKILL.md-only), like
  `PROC-015`'s wave-1 skills.

## Required changes

- [ ] Author `tools/agents/skills/intrinsicengine-import-visibility-contract/SKILL.md`
      with trigger-rich frontmatter and the checklist above, each item citing
      its evidencing retired task.
- [ ] Register the skill in the `intrinsicengine-core` routing table and the
      skills `README.md` discipline tier.

## Tests

- [ ] `python3 tools/agents/sync_skills.py --check` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [ ] Skills `README.md` discipline-tier table updated.

## Acceptance criteria

- [ ] Skill exists with valid frontmatter and is auto-discoverable.
- [ ] Every checklist item cites a retired task; no aspirational content.

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Engine code changes.
- Restating the geometry-IO parser slice shape owned by `PROC-019`.
