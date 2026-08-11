# Docs Sync Policy

Documentation updates are required in the same PR when code/structure/policy changes.

## Required docs updates by change type

- **Architecture/layering changes**
  - Update `AGENTS.md` (if contract-level) and relevant `docs/architecture/*`.
- **Migration/path moves**
  - Update `docs/migration/*`, links, and inventories.
- **Task/process changes**
  - Update `tasks/*` records and `docs/agent/*` where process rules are affected.
  - For enrolled work, keep `tasks/evidence/<TASK-ID>/report.yaml` bound to the
    final changed-source digest. Generate it only after task, documentation,
    and generated-mirror changes are stable.
- **Method or benchmark infrastructure changes**
  - Update `docs/methods/*` or `docs/benchmarking/*` plus validators and manifests.
- **CI/workflow changes**
  - Update workflow docs/process checklists.
- **Source documentation changes**
  - Apply [`source-documentation-policy.md`](../../../../../docs/agent/source-documentation-policy.md) to
    materially changed module interfaces, headers, comments, and README files.
  - Update a source-tree README only when its current role, ownership,
    navigation, configuration, or verification entry points change. Record
    feature chronology and future work in task/ADR/migration/report records.

## Quality gates

- Docs should describe current behavior/state, not aspirational plans, unless clearly labeled as roadmap/migration.
- README files are current-state entry points even when a roadmap or migration
  exists elsewhere; link to the purpose-built record instead of copying its
  history or plans into the README.
- Cross-links must be valid.
- Generated inventories should be refreshed when impacted by structure changes.
- Generated agent-workflow artifacts are CI-freshness-checked: `tasks/SESSION-BRIEF.md`
  (`python3 tools/agents/generate_session_brief.py`) after any task-tree change, and the
  skill mirror (`python3 tools/agents/sync_skills.py --write`) after `docs/agent/*` or
  `tasks/templates/task.md` changes.
- Enrolled task evidence is validated with
  `python3 tools/agents/workflow_evidence.py validate --root .`. Retirement
  requires a complete report; high-risk and higher profiles also require a
  current revision-bound handoff and accepted independent review.

## Automation

- Run `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`
  for a local warning-mode docs-sync preview against changed files.
- `ci-docs.yml` fetches full history and runs the same changed-file comparison
  with `--strict`; pull requests and merge groups supply both event base/head
  SHAs through `--base-ref` and `--head-ref`, while local use defaults the head
  to `HEAD`. A missing required documentation update is a merge blocker.
- `ci-docs.yml` also runs
  `python3 tools/agents/check_task_state_links.py --root . --strict`, enforcing
  that task links and nearby lifecycle claims match the task's actual
  `backlog`/`active`/`done` location.
- Rule mappings live in `tools/docs/docs_sync_rules.yaml`.
