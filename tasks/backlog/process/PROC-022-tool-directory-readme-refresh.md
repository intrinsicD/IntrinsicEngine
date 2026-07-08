---
id: PROC-022
theme: H
depends_on: []
---
# PROC-022 — Refresh tools/* directory READMEs to match their contents

## Goal

- Bring `tools/agents/README.md`, `tools/ci/README.md`, and
  `tools/repo/README.md` back to factual current state: every script present
  is listed with its one-line purpose and CI wiring, and no "planned" entry
  describes a script that already exists.

## Non-goals

- No behavior changes to any script.
- No new tooling.

## Context

- 2026-07-08 audit findings: `tools/agents/README.md` lists
  `validate_method_manifests.py` under "Planned moves" though it exists and
  runs strict in `ci-docs.yml`, and omits `check_codex_config.py` and
  `check_todo_active_only.sh`; `tools/ci/README.md` documents 2 of 6 scripts
  (missing `check_prerequisites.py`, `time_command.py`, `touched_scope.py`,
  `run_clean_workshop_review.sh`); `tools/repo/README.md` omits
  `check_pr_contract.py` and `check_shader_outputs.py`.
- `AGENTS.md` §9: docs are factual current state, not aspirational.

## Required changes

- [ ] Reconcile each of the three READMEs against `ls` of its directory:
      add missing scripts (purpose + where CI invokes them), remove or
      re-label stale "planned" entries.
- [ ] Cross-check each script's CI claims against `.github/workflows/*.yml`.

## Tests

- [ ] `python3 tools/docs/check_doc_links.py --root .` passes.

## Docs

- [ ] The three READMEs themselves; no other docs owed.

## Acceptance criteria

- [ ] Every script under the three directories appears in its README, and
      every README entry corresponds to an existing script.

## Verification

```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Script behavior changes.
- Documenting aspirational tooling without a `(planned)` marker and owning
  task ID.
