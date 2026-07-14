---
id: RUNTIME-131
theme: F
depends_on: [CORE-003, GRAPHICS-106]
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# RUNTIME-131 — Agent/CLI config-control facade on the Engine

## Goal
- Make the agent/CLI lane a first-class control surface equal to the UI by
  hoisting the existing recipe + engine-config apply lifecycle onto a thin
  runtime-owned facade on `Engine`, reachable without any ImGui frame (P3).

## Non-goals
- A new command-bus / message-dispatch framework (P1: a thin typed facade over
  the existing backend-neutral kernels, nothing more).
- New config schema (consumes `CORE-003`'s `EngineConfig` lane and the existing
  `RenderRecipeConfig` surface).
- The renderer override seam (`GRAPHICS-106`) or its runtime routing (`RUNTIME-130`).

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Add runtime config-control facade`).
- Maturity: `CPUContracted`.
- The render-recipe draft/preview/activate lifecycle lives inside
  `ApplySandboxEditorRenderRecipeCommand` (`Runtime.SandboxEditorUi.cpp`,
  ~line 9143) and is reachable only through ImGui. P3 requires config files,
  UI, and agents to drive the engine through one lane with the agent lane
  first-class.
- `CORE-003` provides the `EngineConfig` loader; `GRAPHICS-106` +`RUNTIME-130`
  provide the recipe apply path. This task exposes both through one runtime
  facade and makes the editor call the *same* facade.
- Owner/layer: `runtime`. Plain structs + free functions; no new abstraction layer.

## Required changes
- [x] Add a runtime-owned facade on `Engine` that drives the recipe
      draft/preview/activate lifecycle and an apply-`EngineConfig` (hot-subset)
      path, backed by `Graphics::PreviewRenderRecipeConfig` /
      `LoadRenderRecipeConfigFile` and the `CORE-003` engine-config loader.
- [x] Make `SandboxEditorUi` call the same facade; the UI keeps only widget /
      draft-buffer state (no private subsystem poking).
- [x] Keep the facade ImGui-independent so an agent/CLI caller can build the
      context once and drive activation with zero ImGui frames.

## Tests
- [x] Contract test: a non-ImGui caller drives recipe activation + an
      `EngineConfig` hot-subset change through the facade with zero ImGui frames,
      sharing one validated path with the UI.
- [x] Contract test: the editor command path and the agent path produce identical
      validated results for the same input.
- [x] Default CPU gate stays green.

## Docs
- [x] Document the agent/CLI control facade (entry points, validated apply path)
      under `docs/architecture/` and link it from the runtime docs.

## Acceptance criteria
- [x] An agent/CLI caller controls recipe activation and an engine-config hot
      subset without the UI, through the same validated path the UI uses.
- [x] No command-bus abstraction introduced; facade is plain structs + functions.
- [x] No GPU dependency; CPU gate green.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeConfigControlFacade|RuntimeRenderRecipeActivation|SandboxEditorUi\.RenderRecipeEditor' --timeout 120
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Introducing a command-bus / generic dispatch framework.
- Duplicating the `RenderRecipeConfig` or `EngineConfig` validation kernels.
- Letting the UI retain a private apply path the facade cannot reproduce.

## Maturity
- Target: `CPUContracted` — the facade is contract-tested via a non-ImGui caller.
- Depends on `CORE-003` (engine-config loader) and `GRAPHICS-106` (override seam);
  `RUNTIME-130` provides the recipe routing this facade reuses.
- `CPUContracted` is the intended endpoint; no `Operational` follow-up is owed.
  `RUNTIME-134` consumes the facade for a concrete UI/method playground rather
  than being a maturity follow-up for this facade.
