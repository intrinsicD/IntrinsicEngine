# UI-006 — Sandbox EditorUI render graph diagnostics panel

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-08.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: `SandboxEditorUi` now exposes a promoted `Frame Graph` diagnostics panel backed by renderer-owned `RenderGraphFrameStats`, copying compile/execute counters, queue/timeline stats, command pass statuses, diagnostics, and the compiler debug dump into a runtime-owned UI model.

## Goal
- Reimplement the legacy Frame Graph editor diagnostics panel as a promoted `SandboxEditorUi` panel that displays renderer-owned render-graph compile, execution, command-recording, and debug-dump data through runtime-owned UI models.

## Non-goals
- No changes to render-graph compilation, queue scheduling, barriers, or pass execution semantics.
- No renderer/RHI ownership moves into UI; graphics remains the owner of `RenderGraphFrameStats` and debug dump generation.
- No new GPU/Vulkan operational behavior or shader changes.
- No legacy `Interface::GUI`, legacy `Graphics.RenderGraph`, or legacy editor imports.

## Context
- Owner/layer: `src/runtime/Editor/Runtime.SandboxEditorUi.*`; runtime may read graphics renderer diagnostics through `Engine::GetRenderer()` and expose data-only UI models.
- The legacy GUI included a `Frame Graph` panel. The promoted renderer already publishes `Graphics::RenderGraphFrameStats`, command pass statuses, and `BuildRenderGraphDebugDump(...)` output after `Graphics.RenderGraph.Compiler.cpp` compiles a frame graph.
- This slice wires that existing promoted data into the sandbox editor without changing compiler behavior.

## Required changes
- [x] Add a data-only render graph diagnostics model to `Extrinsic.Runtime.SandboxEditorUi`.
- [x] Populate the model from optional `Graphics::RenderGraphFrameStats` in `BuildSandboxEditorPanelFrame(...)` and from `Engine::GetRenderer().GetLastRenderGraphStats()` in the attached editor path.
- [x] Draw a `Frame Graph` ImGui panel that reports compile/execute status, counts, command pass statuses, diagnostics, and the compiler debug dump.

## Tests
- [x] Add `contract;runtime` coverage for the render graph diagnostics model copying compile/execute/resource/command/debug-dump data and fail-closed missing-stats diagnostics.
- [x] Add or update callback/UI drawing coverage so the new panel renders through the existing ImGui adapter path.

## Docs
- [x] Update `src/runtime/README.md` with the promoted render graph diagnostics panel and ownership boundary.
- [x] Update `tasks/backlog/ui/README.md` and `tasks/backlog/README.md` with UI-006 status after retirement.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` to record promoted Frame Graph panel coverage.
- [x] Regenerate `docs/api/generated/module_inventory.md` because the public module surface changes.

## Acceptance criteria
- [x] `SandboxEditorUi` exposes a deterministic render graph panel model without importing legacy UI or render graph code.
- [x] Missing renderer stats are reported as a disabled/fail-closed panel diagnostic.
- [x] Attached sandbox UI reads current renderer stats through the runtime `Engine` composition root.
- [x] Contract tests cover model fields and the ImGui draw path.

## Verification
```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

### Completion session record (2026-06-08)

- `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`
  completed; the generated inventory had no content diff.
- `cmake --build --preset ci --target IntrinsicRuntimeContractTests` passed
  after the `SandboxEditorUi` public surface and contract tests were updated.
- `ctest --test-dir build/ci --output-on-failure -R 'SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
  passed 24/24 tests.
- Structural checks passed:
  `python3 tools/repo/check_layering.py --root src --strict`,
  `python3 tools/repo/check_test_layout.py --root . --strict`,
  `python3 tools/agents/validate_tasks.py --root tasks --strict`,
  `python3 tools/agents/check_task_policy.py --root . --strict`,
  `python3 tools/agents/check_task_state_links.py --root . --strict`,
  `python3 tools/docs/check_doc_links.py --root .`,
  `python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main`,
  and `git diff --check`.
- `tools/ci/run_clean_workshop_review.sh . --strict` passed its automated
  checks. Manual rows 3-8 were reviewed with no findings: no lower layer
  public API exposes runtime/editor types, no renderer member/subsystem/pass or
  recipe dependency was added, no string-routed frame-graph pass was introduced,
  and the `CPUContracted` task records that no `Operational` follow-up is owed
  for this UI-only diagnostics panel.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding `src/graphics/*` imports of live ECS/runtime/editor state.
- Mutating `Graphics.RenderGraph.Compiler.cpp` behavior under this UI task.

## Maturity
- Target: `CPUContracted`.
- This slice closes the promoted editor diagnostics contract only. No `Operational` follow-up is owed for this UI-only diagnostics panel because it observes existing renderer stats rather than adding backend behavior.
