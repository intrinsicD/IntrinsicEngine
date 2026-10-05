# tools/agents

Agent workflow and task policy tooling.

## Current scripts

- `check_task_policy.py` validates required task directories, rejects legacy root planning files, and delegates strict structured-task checks. Runs strict in `ci-docs.yml`; run it locally as `python3 tools/agents/check_task_policy.py --root . --strict`.
- `validate_tasks.py` validates task IDs, required sections, completion metadata for `tasks/done/`, and checkbox todos in actionable sections. Baseline retired tasks under `tasks/done/` and `tasks/archive/` must stay byte-identical except for task-link directory rewrites that follow a linked task to its current, resolving lifecycle path. Invoked by `check_task_policy.py`.
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
- `skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py`
  inventories missing file synopses and current-state README violations while
  separating objective errors from comment/organization findings that require
  human review. CI tests the auditor; whole-tree debt remains report-only.

## Supporting directories

- `skills/` is the physical skill root; `.claude/skills` and `.codex/skills`
  symlink to it. Skill wrappers and discipline utilities are hand-authored
  there, while mapped `references/` files are generated by `sync_skills.py`.
  Edit the canonical `docs/agent/*` source, never a generated reference.
- `fixtures/protected-synthetic/` is the result-free public fixture used by
  protected-custody regressions.
- `work_graphs/` contains checked-in strict JSON topology. The default
  `review-diamond.v1.json` has one write lane, three parallel read-only checks,
  a join, a high-risk independent gate, and one final source-binding node.

## MCP bridge

`mcp_bridge.py` connects an MCP client (the `intrinsic-sandbox` entry in
`.mcp.json` for Claude Code or `.codex/config.toml` for Codex) to a Sandbox started
with `--agent-socket`; stdlib only. The launch commands resolve the current Git
root, so starts from subdirectories and worktrees use their own bridge. See
[the agent control lane](../../docs/architecture/agent-control-lane.md), which holds the tool catalog.
The bridge is one single-threaded `selectors` loop, so calls are concurrent: `tools/call`
is forwarded under a fresh `bridge-N` id (params and `_meta.progressToken` untouched),
Sandbox notifications such as `notifications/progress` are forwarded verbatim and `ping`
is answered at once. `--timeout` (default 120 s) is per call: on expiry the client gets an
error result saying the call may still be running in the Sandbox (poll `jobs_list` or
`scene_entities`), the connection stays open and a late reply is dropped.
`notifications/cancelled` drops the pending call and is forwarded to the Sandbox; if the
Sandbox exits, pending calls fail with an error result. While the Sandbox is away and the
client has requested a connection with `sandbox_status` and sent `notifications/initialized`, the bridge probes the socket every
`--probe-interval` seconds (default 2) and announces the Sandbox's (possibly changed) tools
with `notifications/tools/list_changed`; `sandbox_status` forces a probe. A call whose send
failed is retried once on a new connection. MCP versions 2025-06-18, 2025-03-26 and
2024-11-05 are negotiated. Clients that cache tool schemas can use the always-visible
`sandbox_tools` (summaries, or a full schema with `{"name":"scene_entities"}`) and
`sandbox_call` (`{"name":"scene_entities","arguments":{}}`). Calls use the same engine
registry, validation, read-only policy, progress, cancellation and deadline handling.
Inspect the selected operation's schema and annotations before calling it; the generic
call tool is conservatively annotated as mutating.
Client permission rules for named tools do not cover the same operation through
`sandbox_call`; restrict the generic tool too when using such rules. Engine read-only
mode still applies to every operation.
Loading the bridge or listing tools does not acquire the engine; `sandbox_status`
requests the connection, as does an explicit catalog or engine call. After release or
a busy refusal, only `sandbox_status` resumes acquisition. The engine admits one client; a contender gets an actionable
`agent_busy` status and pauses retries until the next explicit `sandbox_status`.
`sandbox_disconnect` releases the connection and pauses automatic reconnect until
`sandbox_status` is called. It refuses while tool replies are pending; already
timed-out or cancelled operations can still finish after release.
Regression cases: `tests/regression/tooling/Test.McpBridge.py`.
