# Process Backlog

Agentic-workflow and process-infrastructure hardening: keeping the agent
contract mirrors, task indexes, task metadata, and audit cadences mechanically
honest. These tasks change docs, task tooling, and CI policy surfaces only —
never engine code.

Origin: agentic-workflow review (2026-06-09) of `AGENTS.md`, `docs/agent/*`,
the skill mirrors, and the `tasks/` tree.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [PROC-001 - Skill mirror sync generator and CI gate](../../done/PROC-001-skill-mirror-sync-generator-and-ci-gate.md) (done 2026-06-09).
- [PROC-002 - Task ID uniqueness validation and allocation rule](../../done/PROC-002-task-id-uniqueness-and-allocation-rule.md) (done 2026-06-09).
- [PROC-003 - Split task index state from retirement history](../../done/PROC-003-split-task-index-state-from-retirement-history.md) (done 2026-06-09).
- [PROC-004 - Structured task front-matter and generated session brief](../../done/PROC-004-task-front-matter-and-generated-session-brief.md) (done 2026-06-09).
- [PROC-005 - Align structural-check mode text with strict CI reality](../../done/PROC-005-align-structural-check-mode-contract-text.md) (done 2026-06-09).
- [PROC-006 - Audit cadence lapse visibility](PROC-006-audit-cadence-lapse-visibility.md).
- [PROC-007 - Onboarding prompt tightening and loop-mode defaults](../../done/PROC-007-onboarding-prompt-tightening.md) (done 2026-06-09).
- [PROC-008 - Category README state/history split](PROC-008-category-readme-state-history-split.md).

## Convergence

- These tasks anchor **Theme H — Agentic workflow hardening**.
- Dependency order: `PROC-001` first (every other task edits docs that are
  mirrored into skills), then `PROC-005`, `PROC-002`, and `PROC-007`
  (independent of each other), then `PROC-003`, then `PROC-004`, then
  `PROC-006`.
- `PROC-001` owns the generate-and-verify sync between `docs/agent/*` and the
  three skill mirror roots.
- `PROC-002` owns task-ID uniqueness enforcement and the ID allocation rule.
- `PROC-003` owns moving retirement history out of
  `tasks/active/README.md` and `tasks/backlog/README.md` into an append-only
  retirement log.
- `PROC-004` owns machine-readable task metadata and the generated
  `tasks/SESSION-BRIEF.md`.
- `PROC-005` owns correcting the stale "warning mode" wording in the contract.
- `PROC-006` owns surfacing lapsed audit cadences.
- `PROC-008` owns extending the state/history split and link guard to the
  per-category READMEs (follow-up opened by `PROC-003`).
- `PROC-007` owns deduplicating contract restatements out of the onboarding
  prompt and giving loop mode explicit defaults and a checkpoint rule;
  `PROC-004` owns the prompt's reading-order change — the two prompt-touching
  scopes are disjoint.

Forbidden across all members: engine code changes, renaming retired task files,
weakening any check that currently runs strict in CI, and embedding
task-specific policy into `docs/agent/prompt/prompt.md`.
