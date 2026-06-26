---
id: RUNTIME-131
theme: F
depends_on: [CORE-003, GRAPHICS-106]
maturity_target: CPUContracted
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
- [ ] Add a runtime-owned facade on `Engine` that drives the recipe
      draft/preview/activate lifecycle and an apply-`EngineConfig` (hot-subset)
      path, backed by `Graphics::PreviewRenderRecipeConfig` /
      `LoadRenderRecipeConfigFile` and the `CORE-003` engine-config loader.
- [ ] Make `SandboxEditorUi` call the same facade; the UI keeps only widget /
      draft-buffer state (no private subsystem poking).
- [ ] Keep the facade ImGui-independent so an agent/CLI caller can build the
      context once and drive activation with zero ImGui frames.

## Tests
- [ ] Contract test: a non-ImGui caller drives recipe activation + an
      `EngineConfig` hot-subset change through the facade with zero ImGui frames,
      sharing one validated path with the UI.
- [ ] Contract test: the editor command path and the agent path produce identical
      validated results for the same input.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Document the agent/CLI control facade (entry points, validated apply path)
      under `docs/architecture/` and link it from the runtime docs.

## Acceptance criteria
- [ ] An agent/CLI caller controls recipe activation and an engine-config hot
      subset without the UI, through the same validated path the UI uses.
- [ ] No command-bus abstraction introduced; facade is plain structs + functions.
- [ ] No GPU dependency; CPU gate green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
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
