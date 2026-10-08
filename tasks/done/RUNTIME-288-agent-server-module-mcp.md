---
id: RUNTIME-288
theme: H
depends_on: [ARCH-019, RUNTIME-287, PLATFORM-007]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Retired interactively without a completion report; re-profiled to micro after retirement under BUG-237 (operator decision 2026-10-08). The task body and its commits are the record."
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery, runtime.editor-prepared-frame-locality]
---
# RUNTIME-288 — `AgentServerModule`: MCP JSON-RPC over the local socket


## Completion — 2026-09-27
Commit: the enclosing `claude/agent-lane` commit records this retirement.
Operational on the operator's host (live Sandbox + Claude Code bridge, 2026-09-27).
`Extrinsic.Runtime.AgentServer`: socket-free `AgentProtocol` (initialize, ping,
tools/list, tools/call; -32700/-32600/-32601/-32602 errors; -32000 busy when 64
messages are queued) and `AgentServerModule` (socket thread, mutex-protected
queues, UiBuild hook with at most 4 calls per frame, status service, Disconnect).
View > Agent Connection shows socket, client, mode, roots and calls.

Deviations from the planned text, all deliberate:
- The editor attachment attaches on the first frame hook, not in `OnResolve`, so
  every service it resolves (history, config, scene documents) exists.
- A second client is not refused; it waits in the listen backlog and is served
  after the first disconnects.
- The per-frame bound is a call count, not a time budget.
- Long operations report their status (`Pending` for queued imports and Vulkan
  smoothing); job identities come with RUNTIME-279.
- Tests live in `Test.AgentOperations.cpp` (protocol, read-only, labels, roots) and
  `Test.SandboxAgentServer.cpp` (socket session with config apply recorded as
  `AgentCli`, smoothing, show, undo; UI disconnect). The busy reply is not forced
  in a test.

Architecture review (threading, lifetime, failure states): the socket thread never
touches engine state; all tools run on the main thread. The thread and the frame
hook capture the module's `Impl`, and `Shutdown` stops and joins the thread and
detaches the attachment before the module is destroyed. A listen failure is logged
and shown in the status; the engine keeps running without the lane. Responses for
a dropped client are discarded by connection generation.

## Goal
- Let an MCP client attach to a running, user-launched Sandbox: an opt-in runtime
  module accepts one bridge connection on the PLATFORM-007 socket, speaks MCP
  JSON-RPC (`initialize`, `notifications/initialized`, `ping`, `tools/list`,
  `tools/call`) and executes `Runtime.AgentOperations` entries on the main thread.

## Non-goals
- No client-launched stdio mode, no HTTP transport, no new third-party dependency (ARCH-019).
- No new capabilities: tools are exactly the RUNTIME-287 registry entries.
- No MCP resources/prompts/sampling; tools only.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Architecture decision and binding exclusions: ARCH-019. Registry and label prefix: RUNTIME-287. Socket: PLATFORM-007. Bridge and `.mcp.json`: PROC-035.
- Marshalling primitives to reuse: `EngineSetup::RegisterFrameHook(FramePhase::UiBuild, hook)` with `RuntimeFrameHookContext`; `EditorWorkspaceAttachment::Attach(worlds, services)` and `PrepareEditorProcessingCommands(attachment)` are usable without ImGui (as `tests/contract/runtime/Test.RuntimeConfigControl.cpp` and the panel tests do). `src/app/Sandbox/main.cpp` already parses CLI flags (`--frame-pacing-report`) and composes an app-local `IRuntimeModule` (`FramePacingCaptureModule`) on the `UiBuild` hook — same composition shape.
- JSON: nlohmann with `allow_exceptions=false` as in `ConfigDetail::ParseConfigJson` (`src/runtime/Config/internal/Runtime.PointConfigJson.hpp`); JSON stays in implementation units.
- Framing: newline-delimited JSON-RPC 2.0 (MCP stdio framing), passed through unchanged by the bridge.

## Control surfaces
- Config: N/A for the server itself; launch flags `--agent-socket <path>` (Unix-domain socket path), `--agent-readonly`, `--agent-root <dir>` (repeatable; default current directory and `assets/`). Endpoint and roots are deliberately not engine config fields (an agent must not be able to widen its own roots through `config_apply`).
- UI: Sandbox shell shows server state (off / listening endpoint / client connected / read-only) and the last tool call, so the user always sees that an agent is attached; a "Disconnect agent" button closes the connection.
- Agent/CLI: MCP `tools/list` / `tools/call` over the bridge.

## Required changes
- [x] `Runtime.AgentServer.cppm` + implementation: `AgentServerModule : IRuntimeModule`; transport thread owns the PLATFORM-007 listener/connection and only enqueues parsed requests into a mutex-protected bounded queue with per-request response slots; the `UiBuild` hook drains at most `MaxCallsPerFrame` (default 4) within a per-frame time budget and executes tools against the module's own `EditorWorkspaceAttachment` (attach in `OnResolve`, detach on shutdown).
- [x] MCP methods: `initialize` (protocol version negotiation, `capabilities {tools:{}}`, `serverInfo`), `notifications/initialized`, `ping`, `tools/list` (from the registry: `name`, `title`, `description`, `inputSchema`, `annotations {readOnlyHint, destructiveHint:false, idempotentHint}`), `tools/call` → `{content:[{type:"text"}…, {type:"image"}…], isError}`; JSON-RPC errors for malformed JSON (-32700), invalid request (-32600), unknown method (-32601), invalid params (-32602), busy queue (-32000).
- [x] Every `tools/call` runs under `EditorCommandHistory::LabelPrefixScope("Agent: ")` and `RuntimeConfigControlSource::AgentCli`; long operations return their pending job identity immediately.
- [x] `--agent-readonly` rejects mutating tools with a JSON-RPC error before execution; `AllowedRoots` passed into `AgentOperationContext`.
- [x] Bounds: request ≤ 8 MiB, queue depth cap, one client at a time (second connection refused with a diagnostic), clean shutdown joins the transport thread before engine teardown.
- [x] `main.cpp` composes the module only when `--agent-socket` is given; shell status line/button in `src/app/Sandbox/Editor/Sandbox.EditorShell.cpp` reads a runtime-owned status snapshot.

## Tests
- [x] `tests/contract/runtime/Test.AgentServerProtocol.cpp` with an in-memory transport seam: initialize → tools/list → tools/call round trips, malformed JSON, unknown method, invalid params, busy queue, read-only mode refusal, `Agent: ` label observed in `EditorDocumentModel`.
- [x] `tests/integration/runtime/Test.SandboxAgentSocket.cpp` (headless Null composition, Unix socket in a temp dir): connect, initialize, list, call a read-only and a config tool; frame loop continues while a request is pending.
- [x] Default CPU gate green.

## Docs
- [x] `docs/architecture/agent-control-lane.md` (ARCH-019) updated from planned to current state for the server; `src/app/Sandbox/README.md` documents the launch flags; module inventory regenerated.

## Acceptance criteria
- [x] A Sandbox started with `--agent-socket` serves `tools/list` generated from the registry and executes `tools/call` on the main thread without stalling frames.
- [x] Read-only mode, allowed roots, bounds and the ARCH-019 exclusions are enforced structurally and covered by tests.
- [x] Without the flag, no listener, thread or module exists.
- [x] Architecture review (`intrinsicengine-review` architecture checklist: threading, lifetime, failure states) recorded in the PR.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AgentServer|AgentOperations|SandboxAgentSocket' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- ARCH-019 exclusions: no generic ECS/property write or script tool, no RHI/scheduler/memory/debug-layer controls, file access only inside allowed roots, no delete tool, no overwrite of session-imported source assets, no engine-originated network traffic, no credentials, no layout/settings persistence, no process-exit tool in attach mode.
- Executing tools on the transport thread or blocking the frame loop on a client.
- Exposing roots/endpoint as agent-writable config.
