---
id: RUNTIME-287
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive implementation in the operator session on claude/agent-lane; evidence is the diff, contract tests, review and CI.
contract_schema: 1
contracts: [repo.source-documentation, repo.task-contract-discovery, runtime.editor-prepared-frame-locality]
---
# RUNTIME-287 — Agent operations registry and `Agent:` history label prefix


## Completion — 2026-09-27
Commit: the enclosing `claude/agent-lane` commit records this retirement.
CPUContracted and verified live. `Extrinsic.Runtime.AgentOperations` holds the
registry, `InvokeAgentOperation`, `ResolveAgentPath` and 16 editor-backed operations
(scene/entity/property queries, selection, import, show property, history, undo/redo,
config sections/get/preview/apply with `AgentCli`, jobs, log, mesh-field preview/run).
The prefix scope is `ScopedEditorCommandLabelPrefix` (nesting test in
`Test.EditorCommandHistory.cpp`); the `Agent:` label is observed through the
`history` tool, which reads the same snapshot as `EditorDocumentModel`. A live
Sandbox run on 2026-09-27 imported `tests/data/sculpt.obj`, applied a smoothing
config, ran it (`cpu_sparse_cholesky`), saw `Agent: Smooth property` and undid it
through the Claude Code MCP bridge.

## Goal
Add the runtime-owned `Runtime.AgentOperations` table of named, schema-described
operations that the MCP server (RUNTIME-288), the headless batch CLI (RUNTIME-282)
and contract tests invoke, plus an `EditorCommandHistory` label-prefix scope so
every mutation performed for an agent is recorded as `Agent: <label>`.

## Context
- Status: in progress (operator session, branch `claude/agent-lane`). Next verification step: focused `AgentOperations|EditorCommandHistory` contract tests, then the default CPU gate.
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums. Architecture decision: ARCH-019.
- Files: `src/runtime/Agent/Runtime.AgentOperations.cppm/.cpp`; label prefix in `src/runtime/Editor/Runtime.EditorCommandHistory.cppm/.cpp`.
- Justified abstraction: three present callers (MCP server, batch CLI, contract tests) need one name → invoke dispatch; entries are thin wrappers over existing `Get/Preview/ApplyEditor*` functions and `EngineConfigControl`.
- Shape (plan): `AgentOperationSpec { Name; ReadOnly; Title; Description; InputSchemaJson; Invoke(AgentOperationContext&, std::string_view argumentsJson) -> AgentOperationOutcome }`; `AgentOperationOutcome { IsError; ResultJson; Images; optional PendingJob (EditorJobIdentity) }`; `AgentOperationContext { EditorWorkspaceAttachment&; EngineConfigControl*; JobService&; AllowedRoots; Source = RuntimeConfigControlSource::AgentCli }`. Families register entries from their own implementation units; JSON stays in implementation units.
- Label prefix: RAII `ScopedEditorCommandLabelPrefix`; `NonEmptyLabel` in `src/runtime/Editor/Runtime.EditorCommandHistory.cpp` prepends the active prefix.
- Initial operation set over existing surfaces (phase (a)): `config_sections` / `config_get` (read-only, `EngineConfigControl::SectionRegistry()` + `GetEngineConfigControlState()`), `config_preview` (read-only, `PreviewEngineConfigControlDocument`), `config_apply` (mutating, `ApplyEngineConfigHotSubset(..., AgentCli)`), and read-only document/scene state from existing snapshot models (`EditorDocumentModel`, workspace entity list). The implementing session may adjust this set; capability tasks add their own entries.

## Control surfaces
- Config: config operations go through `EngineConfigControl` with `RuntimeConfigControlSource::AgentCli`.
- UI: agent-made history entries are visible in the existing Undo/Redo labels (and the History window, UI-064) through the `Agent: ` prefix.
- Agent/CLI: this registry is the agent/CLI surface.

## Acceptance criteria
- [x] `Runtime.AgentOperations` module (interface + private implementation) with registration, lookup by name, deterministic listing order and invocation; no JSON types in the interface; source-documented purpose synopsis.
- [x] `ScopedEditorCommandLabelPrefix` prefixes every label executed while active and restores the previous prefix on scope exit (nesting-safe).
- [x] Initial config and read-only operations registered; mutating operations run under the prefix scope and `AgentCli` source.
- [x] `tests/contract/runtime/Test.AgentOperations.cpp`: every entry's `InputSchemaJson` parses; read-only flags match the naming convention; unknown names and malformed arguments fail closed without mutation; config apply records `AgentCli`; history label prefix observed through `EditorDocumentModel`; allowed-root checks refuse paths outside roots.
- [x] `Test.EditorCommandHistory.cpp` covers the prefix scope; module inventory regenerated; `docs/architecture/runtime-config-control.md` links the registry.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AgentOperations|EditorCommandHistory|RuntimeConfigControl' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- A generic ECS/component/property write, script or RHI operation (ARCH-019 exclusions).
- JSON types in `.cppm` surfaces; exceptions/RTTI.
- A second, parallel config apply path that bypasses `EngineConfigControl`.
