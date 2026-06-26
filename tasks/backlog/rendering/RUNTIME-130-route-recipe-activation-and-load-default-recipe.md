---
id: RUNTIME-130
theme: B
depends_on: [GRAPHICS-106]
maturity_target: Operational
---
# RUNTIME-130 — Route recipe activation through runtime + load default recipe at init

## Goal
- Close the config-lane loop so UI, config files, and agents converge on the
  `GRAPHICS-106` renderer override seam through one runtime-owned apply path,
  and so a default recipe config is loaded at `Engine::Initialize()` (P3 + P5:
  "default recipes are loaded initially").

## Non-goals
- Adding the renderer override seam itself (owned by `GRAPHICS-106`).
- The `EngineConfig` file lane (owned by `CORE-003`) — this task wires the
  *render recipe* lane only.
- A general command-bus framework or new abstraction (P1: keep it a thin
  runtime-owned apply path reusing the existing recipe kernel).

## Context
- Today `ApplySandboxEditorRenderRecipeCommand` with `ActivatePreview`
  (`src/runtime/Editor/Runtime.SandboxEditorUi.cpp`, ~lines 9143/9200/9227)
  uses `Graphics::PreviewRenderRecipeConfig` and writes only editor-local
  `m_RenderRecipeState`. `Engine::Initialize()` loads no recipe config.
- After `GRAPHICS-106` exists, the activation write has a destination. This task
  moves ownership of the *active* recipe overlay onto the runtime (`Engine`) and
  has the editor command call a runtime command that invokes the renderer seam,
  preserving the `runtime -> graphics` direction (the editor never calls the
  renderer directly).
- Owner/layer: `runtime` (composition root). Runtime owns IO and the apply path;
  graphics stays IO-free.

## Required changes
- [ ] Move ownership of the active recipe overlay out of `SandboxEditorUi` onto
      `Engine`/runtime; on `ActivatePreview`, call a runtime command that invokes
      the `GRAPHICS-106` renderer override seam.
- [ ] In `Engine::Initialize()`, attempt `LoadRenderRecipeConfigFile` on a
      configured/default path and feed the validated result through the same
      runtime apply path so a default recipe reaches the seam at init; fail-closed
      to the derived default recipe on a missing/invalid file with a diagnostic.
- [ ] Ensure config-file, UI, and the (future `RUNTIME-131`) agent path all share
      this single validated apply path.

## Tests
- [ ] Headless test: an editor activation command reaches the seam and changes
      the `FrameRecipeIntrospection` pass set.
- [ ] Headless test: a startup config selecting a non-default optional-slot set
      is reflected in the first frame's introspection.
- [ ] Headless test: an invalid/missing startup config falls back to the derived
      default with a recorded diagnostic.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update `src/runtime/README.md` to record runtime ownership of the active
      recipe overlay and the init-time default-recipe load.
- [ ] Update `docs/architecture/frame-graph.md` (after `DOCS-004`) to state the
      lane is now wired (no longer preview-only).

## Acceptance criteria
- [ ] UI activation, a config file, and a programmatic call all drive frame
      composition through one runtime-owned validated apply path.
- [ ] A default recipe config is loaded at init; invalid input is fail-closed.
- [ ] `runtime -> graphics` direction preserved; editor never calls the renderer
      directly.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Introducing a command-bus abstraction or generic stage framework.
- Letting the editor call the renderer seam directly (must route through runtime).
- Re-implementing `RenderRecipeConfig` preview/validate/diagnostics.

## Maturity
- Target: `Operational` — the recipe lane drives the live frame through
  `Engine::Run` with the reference config, proven by a headless integration test
  cited in `Verification`.
- Depends on `GRAPHICS-106` (`Operational` is not reachable until the seam exists).
