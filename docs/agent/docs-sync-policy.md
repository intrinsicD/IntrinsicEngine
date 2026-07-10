# Docs Sync Policy

Documentation updates are required in the same PR when code/structure/policy changes.

## Required docs updates by change type

- **Architecture/layering changes**
  - Update `AGENTS.md` (if contract-level) and relevant `docs/architecture/*`.
- **Migration/path moves**
  - Update `docs/migration/*`, links, and inventories.
- **Task/process changes**
  - Update `tasks/*` records and `docs/agent/*` where process rules are affected.
- **Method or benchmark infrastructure changes**
  - Update `docs/methods/*` or `docs/benchmarking/*` plus validators and manifests.
- **CI/workflow changes**
  - Update workflow docs/process checklists.

## Quality gates

- Docs should describe current behavior/state, not aspirational plans, unless clearly labeled as roadmap/migration.
- Cross-links must be valid.
- Generated inventories should be refreshed when impacted by structure changes.
- Generated agent-workflow artifacts are CI-freshness-checked: `tasks/SESSION-BRIEF.md`
  (`python3 tools/agents/generate_session_brief.py`) after any task-tree change, and the
  skill mirror (`python3 tools/agents/sync_skills.py --write`) after `docs/agent/*` or
  `tasks/templates/task.md` changes.

## Automation

- Run `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  for a local warning-mode docs-sync preview against changed files.
- `ci-docs.yml` fetches full history and runs the same changed-file comparison
  with `--strict`; a missing required documentation update is a merge blocker.
- `ci-docs.yml` also runs
  `python3 tools/agents/check_task_state_links.py --root . --strict`, enforcing
  that task links and nearby lifecycle claims match the task's actual
  `backlog`/`active`/`done` location.
- Rule mappings live in `tools/docs/docs_sync_rules.yaml`.
