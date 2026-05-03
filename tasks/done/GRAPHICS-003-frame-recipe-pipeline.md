# GRAPHICS-003 — Frame recipe and default pipeline

- Status: completed
- Completion date: 2026-05-03
- Commit / PR: local split commit 754e23b; remote PR reference TBD.

## Goal
- Add a reusable default frame recipe that declares canonical resources and schedules renderer passes without hard-coded one-off graph construction.
## Non-goals
- No shader implementation.
- No Vulkan-only behavior.
- No new visual effects beyond the documented canonical pass chain.
## Context
- Owner: `src/graphics/renderer` and `src/graphics/framegraph`.
- `docs/architecture/rendering-three-pass.md` defines canonical resources, pass order, and backbuffer ownership.
- Current renderer entry points need a reusable recipe path before individual passes can be filled in safely.
## Implementation note (2026-05-03)
- Added `Extrinsic.Graphics.FrameRecipe` with typed lighting-path/features, canonical resource declarations, pass/resource introspection, and `BuildDefaultFrameRecipe()` graph construction.
- `NullRenderer::ExecuteFrame()` now builds its render graph through the default frame recipe instead of hard-coded one-off pass/resource declarations.
- Optional picking, shadow, selection-outline, debug-view, postprocess, ImGui, and depth-prepass resources are gated by `FrameRecipeFeatures`; disabled optional resources are omitted from graph construction.
- The imported `Backbuffer` is accepted only through `FrameRecipeImports` and is finalized by the `Present` declaration.
## Required changes
- Define a typed `FrameRecipe` or equivalent policy object for forward, deferred, hybrid, debug, selection, and post-process feature gates.
- Build the default render graph from pass modules and canonical resource names.
- Add introspection hooks for pass order, resource producers/consumers, and imported-resource write policy.
## Tests
- Add contract tests for default pass order, optional-resource gating, imported backbuffer policy, and missing-resource diagnostics.
- Use null/mock backend seams for CPU-supported tests.
- Label CPU contract tests `contract;graphics` so they run in the default CPU gate.
- Added `Test.FrameRecipeContract.cpp` to `IntrinsicGraphicsContractCpuTests`.
## Docs
- Update the frame recipe and pass table in `docs/architecture/rendering-three-pass.md`.
## Acceptance criteria
- The null renderer can report or execute the canonical graph through reusable pipeline code.
- Optional resources are omitted when their features are disabled.
- Backbuffer writes are restricted to the present/finalization step.
## Verification
Completed checks:

```bash
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md --check
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
grep -R "entt\|Extrinsic.ECS\|Runtime\." -n src/graphics/renderer --include='*.cpp' --include='*.cppm'
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeGraphicsCpuTests -j1
ctest --test-dir build/ci --output-on-failure -R 'FrameRecipeContract|RenderWorldContract|RuntimeRenderExtraction' --timeout 60
cmake --build --preset ci --target IntrinsicTests -j1
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet
```

Note: `external/cache/draco-*` and `external/cache/imgui-*` contained ignored
corrupt partial FetchContent clones from earlier configure attempts. Removing
those ignored generated directories allowed `cmake --preset ci` to complete.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```
## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Creating hidden pass dependencies outside the frame graph.
