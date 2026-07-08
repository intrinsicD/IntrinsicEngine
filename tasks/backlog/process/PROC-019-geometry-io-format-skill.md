---
id: PROC-019
theme: H
depends_on: []
---
# PROC-019 — Author the geometry-io-format skill (playbook wave 2)

## Goal

- Author `intrinsicengine-geometry-io-format`: the template for adding a
  geometry importer/exporter, so future format slices instantiate the proven
  shape instead of re-deriving it.

## Non-goals

- No engine code changes.
- No changes to existing GEOIO tests or fixtures.

## Context

- The identical slice shape was instantiated ~35 times across
  `GEOIO-002A`..`002AG` and `GEOIO-003`: strict parsing with
  untrusted-header-count validation (`BUG-033`), an explicit diagnostics
  taxonomy, determinism + round-trip + non-finite fail-closed tests,
  committed fixture conventions, and `CPUContracted` closure wording with an
  explicit statement of whether an `Operational` follow-up is owed.
- Future formats will recur; the template belongs in a skill, not in each
  task author's memory.
- Skill tier: cross-cutting discipline skill (SKILL.md-only).

## Required changes

- [ ] Author `tools/agents/skills/intrinsicengine-geometry-io-format/SKILL.md`
      distilling the GEOIO-002 series shape (pick 2–3 exemplar slices and
      cite them), including the fixture and diagnostics conventions.
- [ ] Register the skill in the `intrinsicengine-core` routing table and the
      skills `README.md` discipline tier.

## Tests

- [ ] `python3 tools/agents/sync_skills.py --check` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [ ] Skills `README.md` discipline-tier table updated.

## Acceptance criteria

- [ ] Skill exists with valid frontmatter and is auto-discoverable.
- [ ] The template matches what the retired GEOIO slices actually did (verify
      against at least three of them before writing).

## Verification

```bash
python3 tools/agents/sync_skills.py --check
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Engine code changes.
- Restating the runtime visibility contract owned by `PROC-018`.
