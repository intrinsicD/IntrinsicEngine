---
id: RUNTIME-130
theme: B
depends_on: [GRAPHICS-106]
maturity_target: Operational
completed_on: 2026-06-28
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
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Route render recipe activation through runtime`).
- Maturity: `Operational`.
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
- [x] Move ownership of the active recipe overlay out of `SandboxEditorUi` onto
      `Engine`/runtime; on `ActivatePreview`, call a runtime command that invokes
      the `GRAPHICS-106` renderer override seam.
- [x] In `Engine::Initialize()`, attempt `LoadRenderRecipeConfigFile` on a
      configured/default path and feed the validated result through the same
      runtime apply path so a default recipe reaches the seam at init; fail-closed
      to the derived default recipe on a missing/invalid file with a diagnostic.
- [x] Ensure config-file, UI, and the (future `RUNTIME-131`) agent path all share
      this single validated apply path.

## Tests
- [x] Headless test: an editor activation command reaches the seam and changes
      the `FrameRecipeIntrospection` pass set.
- [x] Headless test: a startup config selecting a non-default optional-slot set
      is reflected in the first frame's introspection.
- [x] Headless test: an invalid/missing startup config falls back to the derived
      default with a recorded diagnostic.
- [x] Default CPU gate stays green.

## Docs
- [x] Update `src/runtime/README.md` to record runtime ownership of the active
      recipe overlay and the init-time default-recipe load.
- [x] Record that `docs/architecture/frame-graph.md` remains deferred to
      `DOCS-004`; this slice updates runtime/config docs and renderer README
      only because `frame-graph.md` is still a stub owned by that task.

## Acceptance criteria
- [x] UI activation, a config file, and a programmatic call all drive frame
      composition through one runtime-owned validated apply path.
- [x] A default recipe config is loaded at init; invalid input is fail-closed.
- [x] `runtime -> graphics` direction preserved; editor never calls the renderer
      directly.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCoreTests IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'CoreEngineConfigLoad|RenderRecipeConfig|RuntimeRenderRecipeActivation|SandboxEditorUi\.RenderRecipeEditor' --timeout 120
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Introducing a command-bus abstraction or generic stage framework.
- Letting the editor call the renderer seam directly (must route through runtime).
- Re-implementing `RenderRecipeConfig` preview/validate/diagnostics.

## Maturity
- Target: `Operational` — the recipe lane drives the live frame through
  `Engine::Run` with the reference config, proven by
  `RuntimeRenderRecipeActivation.*` headless contract coverage and the default
  CPU-supported gate cited in `Verification`.
- `GRAPHICS-106` is retired, so the renderer seam dependency is satisfied.
