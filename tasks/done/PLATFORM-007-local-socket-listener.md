---
id: PLATFORM-007
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive agent-lane slice; evidence is the diff, platform unit tests, review and CI.
contract_schema: 1
contracts: [repo.source-documentation]
---
# PLATFORM-007 — `Platform.LocalSocket` Unix-domain / loopback listener


## Completion — 2026-09-27
Commit: the enclosing `claude/agent-lane` commit records this retirement.
CPUContracted. `Extrinsic.Platform.LocalSocket`: owner-only (0600, via umask at
bind) Unix-domain listener and connections; stale socket files replaced, live
listeners (`AddressInUse`) and regular files (`InvalidPath`) refused, own file
removed on close. `Test.LocalSocket.cpp` covers round trip, accept timeout, peer
close, permissions, stale/live/regular-file paths, path limits and missing peers,
and skips where the platform reports `Unsupported`. Partial writes are handled by
the `SendAll` loop but not forced in a test. Permission errors surface as
`SystemError`.

## Goal
Provide a small platform-owned local socket listener (Unix-domain socket; a
loopback-only TCP endpoint only if a target platform lacks `AF_UNIX`) that the runtime agent server (RUNTIME-288)
uses to accept one MCP bridge connection and exchange newline-delimited bytes.

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums. Architecture decision: ARCH-019.
- `platform -> core` only; current platform modules: `src/platform/Platform.IWindow.cppm`, `Platform.Input.cppm`, `backends/`. No socket code exists in `src/`.
- Module `Extrinsic.Platform.LocalSocket` in `src/platform/Platform.LocalSocket.cppm/.cpp` (implementation started in the operator session on `claude/agent-lane`). Shape: listener open on a Unix path with a `LocalSocketStatus` result; `Accept(timeout)`, `LocalSocketConnection::{ReadSome(timeout), WriteAll, Close}`; no exceptions; `EINTR`/`EAGAIN` handled; POSIX implementation now, Windows reports unsupported with a diagnostic until needed.
- Unix path created with owner-only permissions (0600) and a stale-socket check (refuse to unlink a path that is not a socket). Loopback TCP is deferred: add it only for a platform without `AF_UNIX`, binding `127.0.0.1` only with no configurable bind address.
- Bytes only: framing (newline-delimited JSON-RPC) and JSON belong to RUNTIME-288.

## Control surfaces
- Config: N/A (endpoint comes from the `--agent-socket` launch flag owned by RUNTIME-288).
- UI: N/A (the Sandbox status for the server is owned by RUNTIME-288).
- Agent/CLI: transport substrate for the MCP bridge (PROC-035).

## Acceptance criteria
- [x] `Platform.LocalSocket` module (interface + implementation) with the shape above, purpose synopsis and failure diagnostics (path too long, in use, permission, not a socket, unsupported platform).
- [x] Unix-domain socket created 0600; existing non-socket path refused; own socket removed on close.
- [x] No network-reachable endpoint is expressible (Unix-domain only; any later loopback TCP option binds `127.0.0.1` only).
- [x] Unit tests in `tests/unit/platform` (`Test.LocalSocket.cpp`): accept/echo round trip, read timeout, peer close, partial writes, stale path refusal; tests skip cleanly where the socket type is unavailable.
- [x] `src/platform/README.md` lists the module; module inventory regenerated; layering check passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'LocalSocket' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Binding a non-loopback address or adding any outbound connection capability.
- JSON/runtime/graphics imports in platform; new third-party dependencies.
