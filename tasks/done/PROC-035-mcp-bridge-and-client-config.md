---
id: PROC-035
theme: H
depends_on: [RUNTIME-288]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive agent-lane slice; evidence is the bridge diff, its tooling test, review and CI.
contract_schema: 1
contracts: []
contract_review: Reviewed the catalog; a stdlib-only tooling script and client config entry touch no engine module, geometry, IO-format or task-schema contract.
---
# PROC-035 — MCP stdio↔socket bridge and `.mcp.json` entry


## Completion — 2026-09-27
Commit: the enclosing `claude/agent-lane` commit records this retirement.
Operational: Claude Code launched the bridge from `.mcp.json`, connected to a live
Sandbox via `sandbox_status` and received the Sandbox's tools through
`notifications/tools/list_changed`. The bridge answers `initialize` itself, so the
client starts without a Sandbox and connects later. It writes only protocol
messages to stdout and exits on stdin EOF. `Test.McpBridge.py` covers startup without
a Sandbox, forwarding with id restoration, list-changed on connect and on loss,
unknown methods, and that a host:port string is only ever a socket path.

## Goal
Ship `tools/agents/mcp_bridge.py`, the stdlib-only process an MCP client launches,
which forwards newline-delimited JSON-RPC between its stdio and the running
Sandbox's agent socket (RUNTIME-288), plus the `.mcp.json` entry and usage docs.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums. Architecture decision: ARCH-019.
- `.mcp.json` already has one stdio server (`knowledge-graph` → `graphify-mcp`); the new entry follows that shape (`command: python3`, `args: [tools/agents/mcp_bridge.py, --socket, <path>]`).
- The bridge is a byte pump: no JSON rewriting and no tool logic. When the Sandbox is not running it answers `initialize` with a clear JSON-RPC error ("Sandbox agent socket not available at <path>; start ExtrinsicSandbox --agent-socket <path>") instead of hanging.

## Control surfaces
- Config: `.mcp.json` entry; socket path via `--socket` or `INTRINSIC_AGENT_SOCKET`.
- UI: N/A (server status is shown in the Sandbox by RUNTIME-288).
- Agent/CLI: the MCP client launch command.

## Acceptance criteria
- [x] `tools/agents/mcp_bridge.py`: Python stdlib only; Unix-domain socket path endpoint (a loopback TCP endpoint only if PLATFORM-007 adds one); clean exit on either side closing; no logging to stdout (stderr only).
- [x] `.mcp.json` gains an `intrinsic-sandbox` entry; `tools/agents/README.md` and `docs/architecture/agent-control-lane.md` document the attach workflow (start Sandbox with `--agent-socket`, client launches the bridge).
- [x] `tests/regression/tooling/Test.McpBridge.py` uses a fake socket server: round trip of several framed messages, unavailable-socket error reply, refusal of any non-local endpoint.

## Verification
```bash
python3 tests/regression/tooling/Test.McpBridge.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Third-party Python packages; non-loopback endpoints; tool logic or payload rewriting in the bridge.
