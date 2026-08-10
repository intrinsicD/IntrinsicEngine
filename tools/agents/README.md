# tools/agents

Agent workflow and task policy tooling.

## Current scripts

- `check_task_policy.py` validates required task directories, rejects legacy root planning files, and delegates strict structured-task checks. Runs strict in `ci-docs.yml`; `check_todo_active_only.sh` is a thin compatibility wrapper for it.
- `validate_tasks.py` validates task IDs, required sections, completion metadata for `tasks/done/`, and checkbox todos in actionable sections. Invoked by `check_task_policy.py`.
- `workflow_evidence.py` records exact command receipts, generates completion
  reports from task/Git/artifact facts, seals completed dirty reports against
  an exact commit containing their unchanged evidence, appends high-risk
  handoff/review records, and validates enrolled retirement evidence.
- `experiment_custody.py` freezes claim-grade protocols, initializes
  non-overwriting runs, journals cells, builds/audits portable bundles, and
  enforces protected prospective authorization and one-shot attempts. Its
  benchmark-result input routes canonical schema-v2 payloads through the same
  bundle/audit custody without granting claim eligibility.
- `task_claim.py` atomically coordinates task and optional path claims through
  the Git common directory shared by worktrees; each acquisition has a unique
  generation and no daemon is involved.
- `agent_work_graph.py` validates checked-in schema-v1 work-graph recipes and
  manages one claimed non-micro task's live node state, bounded reopen,
  audited next-slice advancement from an exact clean commit, exact-generation
  claim-handoff resume, node-addressed notes, permission checks, writer-frozen
  review binding, locked inspection, terminal surface binding, and hash-chained
  event trace in the Git common directory. It never launches an agent or
  replaces task/evidence/review authority.
- `check_task_maturity_followups.py` validates that open backend-facing `CPUContracted` maturity closures name an operational owner or explicitly state that no operational follow-up is owed. Invoked by `check_task_policy.py`.
- `check_task_state_links.py` validates that task links and nearby lifecycle status claims agree with the actual `tasks/backlog/`, `tasks/active/`, and `tasks/done/` location of the referenced task ID. Runs strict in `ci-docs.yml`.
- `check_codex_config.py` validates `.codex/config.yaml` stays meaningful and policy-light (delegating authority to `AGENTS.md` rather than duplicating it). Runs strict in `ci-docs.yml`.
- `validate_method_manifests.py` validates method manifest files under `methods/**/method.yaml` against the method-manifest schema (IDs, required fields, backend/paper metadata, path existence). Runs strict in `ci-docs.yml`.
- `check_ara_claims.py` validates the Agent-Native Research Artifact under `ara/`: required layer files, the `ara/PAPER.md` `## Layers` index, claim-heading form and ID uniqueness, the nine required claim fields, the `Status` disposition vocabulary, `Dependencies` resolution across the `C`/`K`/`A`/`H` namespaces, `Proof` path existence (and that a `supported`/`refuted` claim cites at least one), `From staging` observation IDs, and that `AGENTS.md` points at the ledger. Warning mode by default; runs strict in `ci-docs.yml`. Policy: `docs/agent/ara-evidence-policy.md` (§8b of `AGENTS.md`).
- `generate_session_brief.py` derives `tasks/SESSION-BRIEF.md` from open-task front-matter (active tasks; per-theme unblocked/blocked backlog with first unmet dependency). Deterministic, committed, freshness-checked by `ci-docs.yml` (`--check`). Regenerate after opening, retiring, or re-gating any task.
- `check_audit_cadence.py` reports whether the weekly agent-output audit (default limit 14 days) and the repo-state drift audit (default 42 days) have lapsed, from report filenames under `docs/reports/`. Deliberately non-gating: nightly report-only step plus last-report dates in the session brief; `--strict` is for local use only.
- `sync_skills.py` mirrors canonical `docs/agent/*` (plus `tasks/templates/task.md`) into the physical skill root `tools/agents/skills/`, rewriting relative links for the mirror location. `.claude/skills` and `.codex/skills` are symlinks to that root. `--write` regenerates; `--check` (the `ci-docs.yml` gate) fails on any divergence, missing file, or broken skills symlink. `resync_skills.sh` is a thin `--write` wrapper.
- `check_todo_active_only.sh` is a compatibility wrapper (task-system migration carryover) that execs `check_task_policy.py --root . --strict`; not wired into any workflow directly.

## Supporting directories

- `skills/` is the physical skill-mirror root written by `sync_skills.py`; `.claude/skills` and `.codex/skills` symlink to it. Edit the canonical `docs/agent/*` sources, never the mirror.
- `fixtures/protected-synthetic/` is the result-free public fixture used by
  protected-custody regressions.
- `work_graphs/` contains checked-in strict JSON topology. The default
  `review-diamond.v1.json` has one write lane, three parallel read-only checks,
  a join, a high-risk independent gate, and one final source-binding node.
