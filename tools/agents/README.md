# tools/agents

Agent workflow and task policy tooling.

## Current scripts

- `check_task_policy.py` validates required task directories, rejects legacy root planning files, and delegates strict structured-task checks. Runs strict in `ci-docs.yml`; `check_todo_active_only.sh` is a thin compatibility wrapper for it.
- `validate_tasks.py` validates task IDs, required sections, completion metadata for `tasks/done/`, and checkbox todos in actionable sections. Invoked by `check_task_policy.py`.
- `check_task_maturity_followups.py` validates that open backend-facing `CPUContracted` maturity closures name an operational owner or explicitly state that no operational follow-up is owed. Invoked by `check_task_policy.py`.
- `check_task_state_links.py` validates that task links and nearby lifecycle status claims agree with the actual `tasks/backlog/`, `tasks/active/`, and `tasks/done/` location of the referenced task ID. Runs strict in `ci-docs.yml`.
- `check_codex_config.py` validates `.codex/config.yaml` stays meaningful and policy-light (delegating authority to `AGENTS.md` rather than duplicating it). Runs strict in `ci-docs.yml`.
- `validate_method_manifests.py` validates method manifest files under `methods/**/method.yaml` against the method-manifest schema (IDs, required fields, backend/paper metadata, path existence). Runs strict in `ci-docs.yml`.
- `generate_session_brief.py` derives `tasks/SESSION-BRIEF.md` from open-task front-matter (active tasks; per-theme unblocked/blocked backlog with first unmet dependency). Deterministic, committed, freshness-checked by `ci-docs.yml` (`--check`). Regenerate after opening, retiring, or re-gating any task.
- `check_audit_cadence.py` reports whether the weekly agent-output audit (default limit 14 days) and the repo-state drift audit (default 42 days) have lapsed, from report filenames under `docs/reports/`. Deliberately non-gating: nightly report-only step plus last-report dates in the session brief; `--strict` is for local use only.
- `sync_skills.py` mirrors canonical `docs/agent/*` (plus `tasks/templates/task.md`) into the physical skill root `tools/agents/skills/`, rewriting relative links for the mirror location. `.claude/skills` and `.codex/skills` are symlinks to that root. `--write` regenerates; `--check` (the `ci-docs.yml` gate) fails on any divergence, missing file, or broken skills symlink. `resync_skills.sh` is a thin `--write` wrapper.
- `check_todo_active_only.sh` is a compatibility wrapper (task-system migration carryover) that execs `check_task_policy.py --root . --strict`; not wired into any workflow directly.

## Supporting directories

- `skills/` is the physical skill-mirror root written by `sync_skills.py`; `.claude/skills` and `.codex/skills` symlink to it. Edit the canonical `docs/agent/*` sources, never the mirror.
