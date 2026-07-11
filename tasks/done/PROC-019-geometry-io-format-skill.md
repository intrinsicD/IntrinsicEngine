---
id: PROC-019
theme: H
depends_on: []
---
# PROC-019 — Author the geometry-io-format skill (playbook wave 2)

## Status

- Completed 2026-07-11 on branch `claude/agentic-workflow-tasks-xakyf9`.
- Maturity: `Retired` (docs/skill-surface only; no engine code).
- Commit: this local skill-authoring-and-retirement commit.
- Outcome: authored
  `tools/agents/skills/intrinsicengine-geometry-io-format/SKILL.md`, a
  SKILL.md-only discipline skill distilling the GEOIO-002 slice shape
  (layering, `Core::Expected`/`*IOWriteStatus` API, untrusted-header-count
  validation from `BUG-033`, diagnostics taxonomy, `unit;geometry`
  round-trip/determinism/fail-closed tests, fixture conventions, and
  `CPUContracted` closure wording). The template was verified against three
  exemplar slices before writing — `GEOIO-002B` (PLY ASCII exporter),
  `GEOIO-002D` (binary STL importer), `GEOIO-002E` (binary PLY importer) — and
  the API/diagnostics shape cross-checked against the live
  `Geometry.HalfedgeMesh.IO.cppm` surface. Registered in the
  `intrinsicengine-core` routing table and the skills `README.md` discipline
  tier (seven → eight; total sixteen → seventeen). `sync_skills.py --check`,
  `check_doc_links.py`, and strict `check_task_policy.py` pass; the skill
  auto-discovers.

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

- [x] Author `tools/agents/skills/intrinsicengine-geometry-io-format/SKILL.md`
      distilling the GEOIO-002 series shape (pick 2–3 exemplar slices and
      cite them), including the fixture and diagnostics conventions.
- [x] Register the skill in the `intrinsicengine-core` routing table and the
      skills `README.md` discipline tier.

## Tests

- [x] `python3 tools/agents/sync_skills.py --check` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [x] Skills `README.md` discipline-tier table updated.

## Acceptance criteria

- [x] Skill exists with valid frontmatter and is auto-discoverable.
- [x] The template matches what the retired GEOIO slices actually did (verify
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
