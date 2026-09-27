# ADR 0029: Agent control lane over a local socket

- **Status:** Accepted
- **Date:** 2026-09-27
- **Owners:** runtime / sandbox
- **Related tasks:** ARCH-019, RUNTIME-287, RUNTIME-288, PLATFORM-007, PROC-035

## Context

AI agents working on the engine could only reach it through tests and scripted
ImGui drivers. They could not load a model into the Sandbox the operator is
using, run an operation, read its diagnostics or undo it. The engine already has
one validated control lane shared by files, the CLI and the UI: config sections
with preview/validate/apply (`EngineConfigControl`, `RuntimeConfigControlSource::AgentCli`)
and editor commands with undo history and stale-input guards.

## Decision

- The Sandbox exposes an MCP (Model Context Protocol, JSON-RPC 2.0) server only
  when started with `--agent-socket [path]`. It listens on an owner-only (0600)
  Unix-domain socket (`Platform.LocalSocket`); nothing is network-reachable.
- The MCP client launches a stdlib-only bridge (`tools/agents/mcp_bridge.py`,
  registered in `.mcp.json`) that connects to the running Sandbox. The operator
  starts the Sandbox; the agent attaches to it.
- `Runtime.AgentServer` owns the socket thread and newline-delimited framing.
  Every request runs on the main thread in the `UiBuild` frame phase, a bounded
  number per frame, against its own editor workspace attachment.
- Tools come from `Runtime.AgentOperations`: named, schema-described wrappers
  over existing editor queries, commands and config calls. There is no generic
  scene or property write. Mutating tools run with the history label prefix
  `Agent: ` so the operator sees and can undo every change.
- `--agent-readonly` hides and refuses mutating tools; `--agent-root <dir>`
  (default: working directory) bounds every file argument. Both are launch
  flags, never config fields, so an agent cannot widen them.
- Every capability an agent gets that a user could use is first built as a
  Sandbox feature with UI; the tool calls the same runtime function.

## Consequences

- Agents reproduce operator workflows on real models and verify results in the
  running editor; the operator watches and can undo or disconnect
  (View > Agent Connection).
- The tool surface grows only through operation-family registrations that call
  existing validated functions; tests cover protocol, policy and an end-to-end
  socket session.
- Unix-domain sockets only: Windows builds report `Unsupported` until a
  loopback-only fallback is needed.
- Follow-ups: schemas from declarative config field tables (CORE-010, RUNTIME-276),
  structured readiness reasons (RUNTIME-277) and the capability/UI pairs listed in
  `docs/architecture/agent-control-lane.md`.

## Alternatives Considered

- **Client-launched stdio server** (`--mcp-stdio`): simplest, but the agent then
  owns a separate Sandbox instead of working in the operator's session.
  Rejected by the operator on 2026-09-27.
- **In-process HTTP** (streamable HTTP via a new dependency): adds HTTP parsing,
  a TCP port and an exception-mode review for no capability the socket lacks.
- **A generic scene/property write tool**: bypasses validation, history and
  stale-input guards; excluded.

## Validation

- `tests/unit/platform/Test.LocalSocket.cpp`: permissions, stale files, live-owner refusal.
- `tests/contract/runtime/Test.AgentOperations.cpp`: protocol, read-only policy, path roots, `Agent:` labels.
- `tests/integration/runtime/Test.SandboxAgentServer.cpp`: a socket client applies
  config, runs smoothing, sees the labeled undo entry, undoes it; the Agent
  Connection window disconnects a client.
- `tests/regression/tooling/Test.McpBridge.py`: bridge startup without a Sandbox,
  forwarding, reconnection notifications.
