---
id: CORE-011
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive core slice; evidence is the diff, core unit tests, review and CI.
contract_schema: 1
contracts: [repo.source-documentation]
---
# CORE-011 — `LogEntry` sequence, timestamp and category with `TakeSnapshotSince`

## Goal
Make the core log ring readable as a cursor stream so the Diagnostics window
(UI-065) and the `diagnostics_read` agent operation (RUNTIME-285) can read only
new entries, filtered by level and category.

## Context
- Operator direction 2026-10-03: finish the larger MCP inspection and diagnostics gaps, establish tasks first, delegate with appropriate effort, then obtain Claude review. This direction takes precedence over automatic Framework24 work selection.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- `src/core/Core.Logging.cppm`: ring buffer of 2048 `LogEntry{Level, Message}`, `GetSequenceNumber()`, `TakeSnapshot()`, `ClearEntries()`; nothing in `src/app`/`src/runtime` reads it. Vulkan validation messages already flow through it (`Backends.Vulkan.Device.cpp` `[VulkanDevice::Bootstrap] validation: …`).
- Category = leading `[Tag]` of the message when present (existing call-site convention).
- `Core.Logging` is imported by ~28 units: the `LogEntry` change is a BMI-wide rebuild but mechanical.
- A console-stream switch (stderr) is not needed for the attach-to-running transport; add it only if a stdio consumer appears.

## Control surfaces
- Config: N/A.
- UI: consumed by UI-065 through RUNTIME-285.
- Agent/CLI: consumed by `diagnostics_read` (RUNTIME-285).

## Acceptance criteria
- [ ] `LogEntry` gains `std::uint64_t Sequence`, `std::uint64_t TimestampNs`, `std::string Category`; `TakeSnapshot()` unchanged in behavior.
- [ ] `TakeSnapshotSince(std::uint64_t sequence, std::size_t maxEntries, level mask)` returns entries after the cursor, the next cursor and a `Dropped` count when the ring wrapped past the cursor; `ClearEntries()` does not reset the sequence.
- [ ] `tests/unit/core` test covers cursor semantics, wraparound/dropped count, level filter, category parsing and clear.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'CoreLogging|Logging' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Rewriting call sites to a new logging API; changing log output formatting.

## Execution plan (2026-10-03)
- Interactive micro lane; one writing agent per isolated worktree. Root integrates and owns the shared `build/ci` verification tree.
- Geometry statistics and cursor/diagnostics ownership use `xhigh` effort (numerics and concurrency); runtime inspection uses `high` (domain resolution and bounded queries). UI and MCP integration are coordinated by the root agent.
- Reuse existing property/domain resolution, logger ring, operation registry, EditorShell and panel widgets; plain records and free functions. No new telemetry framework or generic property-write API.
- Dependencies determine integration order. UI/runtime paths land together before their MCP counterparts are reported complete.
- Completion requires the named behavior tests, combined verification and a read-only Claude review of a fixed diff.

The final combined CPU gate is justified by the widely imported `Core.Logging` interface; run focused logging tests first, then the full CPU selector once after integration. Sanitizer and live Vulkan evidence remain separate.
