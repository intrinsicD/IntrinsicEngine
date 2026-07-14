---
id: PROC-009
theme: H
depends_on: []
maturity_target: Retired
---
# PROC-009 — Import productivity skills into repo skill surface

## Goal
- Add the upstream `teach`, `grilling`, and `grill-me` productivity skills to
  the repository-local skill surface so `.claude/skills` and `.codex/skills`
  expose them through the same `tools/agents/skills/` root as the existing
  IntrinsicEngine skills.

## Non-goals
- No changes to engine code, CMake, runtime behavior, or CI build targets.
- No changes to the canonical IntrinsicEngine `docs/agent/*` mirror map.
- No semantic rewrite of the imported third-party skill instructions beyond
  repository-local provenance, root-hygiene guardrails, and index
  documentation.

## Context
- Owner/layer: process infrastructure under `tools/agents/skills/`.
- `.claude/skills` and `.codex/skills` are symlinks to
  `tools/agents/skills`, so one physical import covers all skill surfaces.
- Source: `mattpocock/skills` productivity skills at upstream commit
  `6eeb81b5fcfeeb5bd531dd47ab2f9f2bbea27461`.
- The upstream repository is MIT-licensed; keep the license notice with the
  imported skills.

## Required changes
- [x] Add `teach` with its companion format documents.
- [x] Add `grilling` and `grill-me`.
- [x] Add upstream license/provenance for the imported third-party skills.
- [x] Add repository-local guardrails where an upstream skill would otherwise
      create root-level learning-workspace files.
- [x] Update `tools/agents/skills/README.md` so these skills are listed as
      standalone third-party productivity skills rather than IntrinsicEngine
      canonical-doc mirrors.

## Tests
- [x] Verify the skill mirror/symlink gate still passes.
- [x] Verify docs links and task policy still pass.

## Docs
- [x] Update the skill README with the new skill count, provenance, and
      maintenance model.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after retiring this task.

## Acceptance criteria
- [x] `tools/agents/skills/teach/SKILL.md`,
      `tools/agents/skills/grilling/SKILL.md`, and
      `tools/agents/skills/grill-me/SKILL.md` exist.
- [x] `teach` companion format files are present.
- [x] The imported skills are visible through `.claude/skills` and
      `.codex/skills` by symlink resolution.
- [x] The existing `sync_skills.py --check` gate remains green.

## Verification
```bash
python3 tools/agents/sync_skills.py --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
git diff --check
```

## Completion notes
- PR/commit: this retirement commit.
- Completed on 2026-06-22. Imported `teach`, `grilling`, and `grill-me` from
  `mattpocock/skills` commit
  `6eeb81b5fcfeeb5bd531dd47ab2f9f2bbea27461`.
- Preserved the upstream MIT license notice in
  `tools/agents/skills/THIRD_PARTY_LICENSES.md`.
- Added a local `teach` guardrail so stateful learning artifacts are not
  created at the IntrinsicEngine repo root without an explicit workspace.
- Verification passed:
  `python3 tools/agents/sync_skills.py --check`,
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/validate_tasks.py --root tasks --strict`,
  `python3 tools/docs/check_doc_links.py --root .`, symlink file checks through
  `.claude/skills` and `.codex/skills`, and `git diff --check`.

## Forbidden changes
- Editing generated IntrinsicEngine skill reference copies directly.
- Adding copied third-party skills to `tools/agents/sync_skills.py` as if they
  were generated from `docs/agent/*`.
- Engine code or build-system changes.
