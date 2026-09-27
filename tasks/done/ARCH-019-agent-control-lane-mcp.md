---
id: ARCH-019
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive docs-only architecture decision; evidence is the ADR diff, doc-link and task-policy checks.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# ARCH-019 — ADR and architecture doc: agent control lane (MCP)


## Completion — 2026-09-27
Commit: the enclosing `claude/agent-lane` commit records this retirement.
ADR 0029 and `docs/architecture/agent-control-lane.md` record the decision,
ownership, threading contract, tool naming (as implemented: `scene_*`, `entity_*`,
`config_*`, `preview_*`, `history`, `jobs`, `log` read-only; `select_entity`,
`import_file`, `show_property`, `config_apply`, `run_mesh_operation`, `undo`, `redo`
mutating), exclusions, limitations and the planned capability tasks.

## Goal
Record the architecture decision for an MCP agent control lane into a running
Sandbox: transport, layer ownership, main-thread marshalling, read-only versus
mutating tools, the binding exclusions and bounds, and the rule that every agent
capability is a thin caller of the same runtime functions the Sandbox UI uses.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Existing agent/CLI lane: `EngineConfigControl` in `src/runtime/Config/Runtime.EngineConfigControl.cppm` with `RuntimeConfigControlSource::AgentCli`, documented in `docs/architecture/runtime-config-control.md` (created by the retired RUNTIME-131). It has no process-external transport.
- Transport decision (operator, 2026-09-27): **attach-to-running only**. The MCP server runs inside the user-launched Sandbox and listens on an owner-only Unix-domain socket (a loopback-only TCP port only where `AF_UNIX` is unavailable) behind an opt-in launch flag. A stdlib-only Python stdio↔socket bridge (`tools/agents/mcp_bridge.py`) is what the MCP client launches from `.mcp.json`. There is no `--mcp-stdio` mode and no HTTP transport (no `cpp-httplib`/INFRA task). Headless batch (RUNTIME-282) is a separate user-facing CLI capability, not an MCP transport.
- Layer ownership: `Platform.LocalSocket` (PLATFORM-007, platform→core, POSIX sockets, no JSON); `Runtime.AgentOperations` registry and `EditorCommandHistory` label-prefix scope (RUNTIME-287); `Runtime.AgentServer` / `AgentServerModule` MCP JSON-RPC over the socket (RUNTIME-288); `src/app/Sandbox/main.cpp` composes the module only behind the flag (`app -> runtime` only); bridge and `.mcp.json` entry (PROC-035). JSON stays private to implementation units; interfaces exchange strings.
- Marshalling: the server's transport thread only queues requests; tools execute on the main thread from a `FramePhase::UiBuild` frame hook (`EngineSetup::RegisterFrameHook`) with a per-frame call and time budget against the module's own `EditorWorkspaceAttachment`. Long work returns a pending job identity; no tool blocks the frame.
- Schemas (operator decision): declarative per-section `ConfigFieldSpec` tables generate the JSON Schema and replace the mechanical parts of each `Validate` (CORE-010, RUNTIME-276). Enums stay integer-coded in payloads; schemas carry `x-enum-names`.
- Principle: every capability a human can use gets Sandbox UI integration; MCP tools call the same runtime functions; reuse existing owners and add only necessary code. Mutations executed on behalf of an agent carry an `Agent: ` history label prefix and use `RuntimeConfigControlSource::AgentCli`.

## Binding exclusions and bounds
- Only registry-registered operations exist: no generic ECS/component/property write, no script evaluation, no RHI/device access, no scheduler/memory/debug-layer/validation settings (boot-only config fields stay rejected by `ApplyEngineConfigHotSubset`).
- Filesystem: every path is canonicalized (`weakly_canonical`) and must lie inside an allowed root (`--agent-root`, default current directory and `assets/`); no delete tool; no overwrite of a source asset imported in this session unless explicitly requested per call.
- No engine-originated network traffic: the only sockets are the opt-in Unix-domain/loopback listener. No credentials or secrets in any payload. No layout or settings persistence.
- No process-exit tool in attach mode: the user owns the Sandbox process.
- Bounds: request body ≤ 8 MiB, paged value/index results, capture images ≤ 2048² by default, job waits ≤ 60 s, per-frame call budget, bounded queue depth (excess → JSON-RPC busy error). `--agent-readonly` rejects every mutating tool.

## Acceptance criteria
- [x] `docs/adr/0029-agent-control-lane.md` (from `docs/adr/template.md`) records context, decision (attach-to-running over Unix-domain/loopback socket + Python bridge), rejected alternatives (client-launched stdio mode, in-process HTTP with a new dependency, a generic scene-write tool), consequences, the exclusions and the bounds above; `docs/adr/index.md` lists it.
- [x] `docs/architecture/agent-control-lane.md` describes current ownership per layer, the thread/frame-hook marshalling contract, the operation registry and naming convention (`*_get/_list/_read/_stats/_schema/_preview` read-only; `*_apply/_run/_import/_export/_capture`, `undo`, `redo`, `checkpoint_*` mutating), `readOnlyHint`, `Agent: ` history labels, `--agent-socket`/`--agent-readonly`/`--agent-root`, and the UI-parity rule; it is linked from `docs/architecture/index.md` and `docs/architecture/runtime-config-control.md`.
- [x] The doc lists the planned task set (RUNTIME-287, PLATFORM-007, RUNTIME-288, PROC-035, CORE-010, RUNTIME-276, and the capability/UI pairs) as task links, not as implemented features.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Source, build or test changes; this task records the decision only.
- Describing planned tools as current capabilities.
