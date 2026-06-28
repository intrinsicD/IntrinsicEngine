---
id: GRAPHICS-106
theme: B
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---
# GRAPHICS-106 — Fail-closed IRenderer frame-recipe override seam

## Goal
- Add a fail-closed renderer seam so a *validated* frame-recipe override can
  disable optional declared passes at the `BuildDefaultFrameRecipe` call site,
  making config/UI/agent recipe activation actually reach the live frame
  (P3 + P5 root cause).

## Non-goals
- Widening `RenderRecipeDescriptor` recipe vocabulary (owned by retired
  `GRAPHICS-099`; no new slot kinds here).
- Injecting arbitrary pass-graph nodes or mutating the fixed core
  (preserve the `GRAPHICS-101` fixed-core invariant).
- UI/runtime activation wiring and default-recipe-at-init (owned by `RUNTIME-130`).
- The engine-level `EngineConfig` file lane (owned by `CORE-003`).

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Add renderer frame recipe override seam`).
- Maturity: `CPUContracted`.
- `src/graphics/renderer/Graphics.Renderer.cpp` derives features via
  `DeriveDefaultFrameRecipeFeatures(renderWorld)` (~line 2120) then calls
  `BuildDefaultFrameRecipe(...)` (~line 2153) unconditionally. `IRenderer`
  (`Graphics.Renderer.cppm`) exposes **no** recipe install/override seam, so an
  `Activated` `RenderRecipeConfig` preview never changes the live frame — the
  config lane is a dead end for frame composition.
- `Extrinsic.Graphics.RenderRecipeConfig` already proves the validated-edit
  shape: `kRenderRecipeConfigSchemaId` + version, side-effect-free
  `PreviewRenderRecipeConfig`, `LoadRenderRecipeConfigFile`, and a typed
  diagnostic vector. This task consumes that surface; it does not re-create it.
- Owner/layer: `graphics` (renderer). The seam takes RHI/graphics types only —
  no ECS, runtime, or live asset-service knowledge.

## Required changes
- [x] Add an `IRenderer` seam (`SetActiveFrameRecipeOverride(std::optional<...>)`
      / `ClearActiveFrameRecipeOverride()` + a getter) that stores the override
      beside the existing recipe/lighting state on the concrete renderer.
- [x] Add a pure, side-effect-free projection from a validated
      `RenderRecipeConfigPreview` / `RenderRecipeDescriptor` to
      `FrameRecipeFeatures` enable flags, restricted to optional declared slots
      (e.g. `LightingPath`, `EnablePostProcess`, AA mode).
- [x] Apply the override at the `BuildDefaultFrameRecipe` call site **after**
      `DeriveDefaultFrameRecipeFeatures`, keeping the existing availability gates
      (`EnableAntiAliasing` / `EnableHZBBuild` / cluster flags) failing closed so
      an override can only **disable** an optional pass, never enable one whose
      real resources are unavailable.
- [x] Record a diagnostic when an override references an unknown/unavailable
      slot and leave the derived defaults unchanged in that case.

## Tests
- [x] CPU/null contract test: activate an override that disables PostProcess;
      assert `PostProcessPass` is absent from `FrameRecipeIntrospection` /
      `RenderGraphFrameStats`.
- [x] CPU/null contract test: an invalid / unknown-slot override leaves the
      derived defaults untouched and records a diagnostic (fail-closed).
- [x] Unit test the projection function in isolation (no device).
- [x] Default CPU gate stays green.

## Docs
- [x] Note the override seam and its fixed-core/optional-slot constraints in
      `src/graphics/renderer/README.md`.

`docs/architecture/frame-graph.md` cross-linking is deferred to `DOCS-004`,
which owns promoting that stub into the canonical frame-graph document.

## Acceptance criteria
- [x] `IRenderer` exposes the override seam and the projection is unit-tested in
      isolation.
- [x] An activated override changes the introspected pass set on a null device;
      an invalid override is a fail-closed no-op with a diagnostic.
- [x] Fixed core is never mutated; no `RenderRecipeDescriptor` vocabulary widened.
- [x] No GPU dependency; the default CPU gate is green.

## Verification
```bash
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RendererFrameLifecycle' --timeout 120
cmake --build --preset ci --target IntrinsicTests
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
```

## Forbidden changes
- Widening `RenderRecipeDescriptor` vocabulary or adding new slot kinds.
- Injecting arbitrary pass-graph nodes or mutating the fixed core.
- Enabling an optional pass whose real resources are unavailable.
- UI/runtime activation wiring (that is `RUNTIME-130`).
- Mixing mechanical file moves with semantic refactors.

## Maturity
- Target: `Operational` on the live frame eventually.
- This task closes `Scaffolded → CPUContracted` (seam + pure projection +
  null/CPU contract tests). `Operational` owned by `RUNTIME-130` (the override
  reaching the live frame through `Engine::Run` via UI/config/agent).
