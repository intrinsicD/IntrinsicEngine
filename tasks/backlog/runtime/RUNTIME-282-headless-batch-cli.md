---
id: RUNTIME-282
theme: F
depends_on: [RUNTIME-287, UI-046]
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation]
---
# RUNTIME-282 — Headless batch CLI

## Goal
- Run the Sandbox without a window as a scriptable batch tool: load a file, apply a
  config, run named operations, export and report, with non-zero exit codes on failure.

## Non-goals
- Not an MCP transport (attach-to-running is the agent transport, ARCH-019).
- No new operations: `--run` dispatches `Runtime.AgentOperations` entries by name.
- No headless GPU capture on Null (reports unavailable).

## Context
- Operator direction 2026-09-27: agent control lane and user-facing inspection capabilities, planned with Fable 5.1; attach-to-running transport; declarative schemas; integer enums.
- Exists: `Core::Config::WindowBackend::Null` + Null RHI drive every Sandbox integration test (`tests/integration/runtime/Test.RuntimeSandboxAcceptance.cpp` `HeadlessConfig()`, `Test.SandboxAppComposition.cpp`); `src/app/Sandbox/main.cpp` parses `--frame-pacing-report`/`--frame-pacing-frames`/`--engine-config` (`ResolveEngineConfigForBoot`) and composes `FramePacingCaptureModule` on the `UiBuild` hook that calls `engine.RequestExit()`; async import via `AssetWorkflowModule` and `EditorAssetImportQueueModel`; `LoadAndApplyEngineConfigHotSubsetFile(path, AgentCli)`.
- Export uses UI-046's runtime export command; property export uses RUNTIME-283 once available.
- REVIEW-007 C03 (2026-10-06): `Core.Process` is test-only today (`tests/integration/runtime/Test.CoreProcess.cpp`) and is kept for this task's structured process launch. If RUNTIME-282 drops that need, delete `Core.Process`.

## Control surfaces
- Config: `--apply <config.json>` uses `LoadAndApplyEngineConfigHotSubsetFile(..., AgentCli)`.
- UI: N/A by design (CLI mode); every step is an operation also reachable from the Sandbox UI.
- Agent/CLI: `--headless`, `--load <file>`, `--apply <config.json>`, `--run <op>[=<args.json>]` (repeatable), `--export <out>`, `--capture <out.png>`, `--script <steps.json>`; JSON report `intrinsic.batch_report.v1` on stdout.

## Required changes
- [ ] `src/app/Sandbox/Sandbox.Batch.cpp`: `SandboxBatchModule : IRuntimeModule` with a `UiBuild` step machine (load → wait for import queue → apply → run → wait for `PendingJob` via `EditorJobCommandSurface` → export/capture → report → `RequestExit`), per-step timeout and total frame cap.
- [ ] `main.cpp` parsing; `--headless` forces `Window.Backend=Null` and disables the reference scene.
- [ ] Mutations run under `EditorCommandHistory::LabelPrefixScope("Agent: ")` and the `AgentCli` source (batch is an agent/CLI lane); file paths restricted to the working directory unless `--agent-root` is given.

## Tests
- [ ] `tests/integration/runtime/Test.SandboxHeadlessBatch.cpp` (labels `integration;runtime;headless`): OBJ fixture from `tests/data`, apply a smoothing config, run the smoothing operation, export PLY, assert file and report.
- [ ] Process-level test via `Core.Process` (`Test.CoreProcess.cpp` pattern) running `ExtrinsicSandbox --headless …` (label `slow` if needed); failure exit codes for a missing file and an unknown operation.

## Docs
- [ ] `src/app/Sandbox/README.md` CLI section; `docs/architecture/agent-control-lane.md` notes batch mode as a separate CLI capability.

## Acceptance criteria
- [ ] A scripted run loads, processes, exports and exits with a machine-readable report and correct exit code.
- [ ] Steps use the same runtime functions as the UI and agent lanes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests ExtrinsicSandbox
ctest --test-dir build/ci --output-on-failure -R 'SandboxHeadlessBatch' --timeout 300
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- A second operation dispatch table; app-side reimplementation of runtime operations.
- ARCH-019 exclusions for batch-invoked operations.
