---
id: GRAPHICS-116
theme: B
depends_on: []
---
# GRAPHICS-116 — Frame-recipe pass contribution seam and typed record-path resolution

## Goal
- Open the closed default frame recipe behind a validated contribution seam:
  runtime/editor-owned overlay passes (ImGui, SelectionOutline, DebugView,
  VisualizationOverlay) are contributed as registered pass descriptors keyed
  by typed IDs instead of being baked into
  `BuildDefaultFrameRecipe(...)` — and the renderer's record path resolves
  resources by typed `FrameResourceId` instead of per-frame name string
  scans.

## Non-goals
- No arbitrary pass-injection config surface: the fixed-core guard from
  `docs/architecture/frame-graph.md` stays — contributions are code-level,
  typed, and validated, not document-driven.
- No change to the framegraph module (`src/graphics/framegraph/*`) — the
  generic graph is already clean; this is a renderer-layer seam.
- No relocation of the passes' *implementations* to app (`ARCH-006`
  sequencing); this task makes them contributions, wherever they live.
- No multi-threaded recording (`GRAPHICS-119`) or compile caching
  (`GRAPHICS-117`).

## Context
- Owner/layer: `graphics/renderer` (`Graphics.FrameRecipe`,
  `Graphics.Renderer`); `runtime` becomes a registrant.
- Today the render graph has exactly one producer: the hardcoded default
  recipe (`src/graphics/renderer/Graphics.FrameRecipe.cpp:469-637`,
  `addRecipePass` at `:1332`) whose pass list includes editor/app-flavored
  passes gated only by feature booleans; runtime's only hook is the
  post-graph frame-command hook. The per-pass record lambda special-cases
  features against the compiled graph with O(resources) string scans:
  SelectionOutline scans `compiled->TextureNames` for `"EntityId"`
  (`Graphics.Renderer.cpp:2592-2594`), DebugView scans by
  `selection.SelectedResourceName` (`:2612-2614`), ImGui and Present get
  identity special cases (`:2650, 2657`).
- `FramePassId`/`FrameResourceId` and `FrameRecipeIntrospection` already
  exist as the typed vocabulary; the seam builds on them.
- Origin: `docs/reviews/2026-07-03-mainloop-taskgraph-rendergraph-review.md`
  finding R7.

## Control surfaces
- Config: unchanged — `RenderRecipeConfig` can still only disable declared
  optional extension slots; contributed passes declare whether they expose
  such a slot.
- UI/Agent/CLI: recipe introspection (`DescribeDefaultFrameRecipe`) reports
  contributed passes so existing debug/validation surfaces stay truthful.

## Required changes
- [ ] Define a pass-contribution descriptor (typed `FramePassId`, queue
      affinity, declared reads/writes by `FrameResourceId`, feature gate,
      insertion anchor relative to core passes) and a renderer registration
      API with fail-closed validation (unknown resources, fixed-core
      conflicts, duplicate IDs → recipe validation failure, same
      `NoteRecipeGraphValidation` lane).
- [ ] Convert the editor overlay pass family (ImGui, SelectionOutline,
      DebugView, VisualizationOverlay) from hardcoded recipe entries to
      registered contributions with unchanged pass content and ordering;
      the default sandbox composition registers them, so frames are
      byte-identical.
- [ ] Replace record-path name scans with typed resolution: pass bodies
      receive their declared resources by `FrameResourceId` via recipe
      introspection; delete the `"EntityId"`/`SelectedResourceName` string
      scans and the ImGui/Present identity special cases from the generic
      dispatch (move per-pass needs into the routed pass bodies via the
      existing `RenderCommandRouter`).
- [ ] Keep `DescribeDefaultFrameRecipe`/validation/goldens covering the
      contributed shape.

## Tests
- [ ] Contract: the default contributed recipe compiles to the same pass/
      resource DAG as before (golden comparison of the compiled description).
- [ ] Contract: an invalid contribution (unknown resource, core conflict)
      fails recipe validation closed without altering the frame.
- [ ] Contract: with the overlay family unregistered, the core recipe still
      renders (headless null-backend frame completes; overlays absent).
- [ ] Existing recipe/introspection/config-lane suites stay green.

## Docs
- [ ] Update `docs/architecture/frame-graph.md` (contribution seam next to
      the fixed-core guard) and `src/graphics/renderer/README.md`.
- [ ] Regenerate `docs/api/generated/module_inventory.md` for changed
      surfaces.

## Acceptance criteria
- [ ] No editor/app-specific pass names hardcoded in
      `BuildDefaultFrameRecipe`'s required core; overlay passes arrive via
      registration.
- [ ] No per-frame resource-name string scans in the record path
      (grep-verified).
- [ ] Frames byte-identical for the default sandbox composition; CPU gate
      and recipe validation green.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Weakening the fixed-core/fail-closed recipe guard.
- Giving `graphics/*` knowledge of ECS/runtime types (contributions carry
  render-ready data only).
- Changing pass rendering behavior while converting registration.

## Maturity
- Target: `Operational` (the live sandbox frame is the subject); CPU/null
  contract tests prove the seam, a null-backend frame proves composition.
