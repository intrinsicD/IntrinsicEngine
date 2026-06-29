---
id: DOCS-005
theme: H
depends_on: []
maturity_target: Retired
completed_on: 2026-06-29
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
- Status: done; owner/agent: Codex; branch: `main` local iteration.
- Slice plan: docs-only completion in one slice. Update the playbook wording,
  retire the task, regenerate `tasks/SESSION-BRIEF.md`, and run docs/task
  structural checks.
- `feature-module-playbook.md` §4 mandates the full 4-part vertical slice for
  every new feature module, imposing ceremony on one-caller research probes -
  directly in tension with P1.
- §12 lists UI artifacts but omits the config/command lane, so the playbook
  nudges new features toward ImGui-only control - in tension with P3.
- Owner/layer: docs/architecture. Mirrored via `sync_skills.py` only if the doc
  is mirrored.

## Required changes
- [x] Add a `§0 Floor` clause: a one-caller, no-backend-split, no-async
      data-driven feature may be a plain struct + free function; neither the
      4-part slice nor the full Config/Input/Result/Execute surface is required
      until it grows.
- [x] Soften §1/§4 "Mandatory / every new feature module" to "grow the full
      contract when a second caller, a CPU/GPU backend split, or off-main-thread
      work appears".
- [x] Add to §12 a fourth required artifact: a unified serializable
      config/command entry an agent and a config file can drive (not ImGui-only).
- [x] Add a §13 gate row: "Is the full slice actually needed, or would a struct +
      function do?"

## Tests
- [x] `python3 tools/docs/check_doc_links.py --root .` passes.
- [x] Skill mirrors in sync after `sync_skills.py --write` (the edited
      architecture doc is not mirrored, so no mirror files changed).

## Docs
- [x] This task is docs-only (`docs/architecture/feature-module-playbook.md`).

## Acceptance criteria
- [x] The playbook opens with the floor clause and lists the config/command
      artifact; §13 has the gate row.
- [x] Doc links/mirrors green; no engine code touched.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/sync_skills.py --write && git diff --quiet -- tools/agents/skills docs/agent || echo "mirror/doc drift to review"
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes
- Editing engine code.
- Removing the full vertical-slice contract for features that need it.

## Completion notes
- PR/commit: this retirement commit.
- Completed on 2026-06-29 at `Retired` maturity. The playbook now includes a
  minimal-feature floor for one-caller synchronous probes, softens the full
  vertical-slice contract to the point where a feature grows past that floor,
  and adds a serializable config/command entry for UI-backed feature
  discoverability.
