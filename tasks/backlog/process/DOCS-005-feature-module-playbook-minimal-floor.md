---
id: DOCS-005
theme: H
depends_on: []
---
# DOCS-005 — Add a minimal-feature floor + config-entry artifact to the feature-module playbook

## Goal
- Soften `docs/architecture/feature-module-playbook.md` so trivial research
  probes are not forced through the full vertical-slice ceremony (P1), and add a
  config/command lane artifact so new features are agent/config-drivable, not
  ImGui-only (P3). Docs-only.

## Non-goals
- Editing engine code.
- Removing the full vertical-slice contract for features that genuinely need it
  (the floor is an escape hatch, not the new default for everything).

## Context
- `feature-module-playbook.md` §4 mandates the full 4-part vertical slice for
  every new feature module, imposing ceremony on one-caller research probes —
  directly in tension with P1.
- §12 lists UI artifacts but omits the config/command lane, so the playbook
  nudges new features toward ImGui-only control — in tension with P3.
- Owner/layer: docs/architecture. Mirrored via `sync_skills.py` only if the doc
  is mirrored.

## Required changes
- [ ] Add a `§0 Floor` clause: a one-caller, no-backend-split, no-async
      data-driven feature may be a plain struct + free function; neither the
      4-part slice nor the full Config/Input/Result/Execute surface is required
      until it grows.
- [ ] Soften §1/§4 "Mandatory / every new feature module" to "grow the full
      contract when a second caller, a CPU/GPU backend split, or off-main-thread
      work appears".
- [ ] Add to §12 a fourth required artifact: a unified serializable
      config/command entry an agent and a config file can drive (not ImGui-only).
- [ ] Add a §13 gate row: "Is the full slice actually needed, or would a struct +
      function do?"

## Tests
- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.
- [ ] Skill mirrors in sync after `sync_skills.py --write` (if mirrored).

## Docs
- [ ] This task is docs-only (`docs/architecture/feature-module-playbook.md`).

## Acceptance criteria
- [ ] The playbook opens with the floor clause and lists the config/command
      artifact; §13 has the gate row.
- [ ] Doc links/mirrors green; no engine code touched.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --quiet -- tools/agents/skills docs/agent || echo "mirror/doc drift to review"
```

## Forbidden changes
- Editing engine code.
- Removing the full vertical-slice contract for features that need it.
