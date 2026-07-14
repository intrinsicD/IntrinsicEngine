# GRAPHICS-001 — Rendering parity inventory and task index

- Status: completed / retired.
- Completion date: 2026-06-06.
- Maturity: `Retired` as a planning and inventory task.
- Current selection source: [`tasks/backlog/rendering/README.md`](../backlog/rendering/README.md).

## Goal
- Create the original backlog entry that inventories legacy-inspired rendering features and indexes the reimplementation tasks for `src/graphics`.

## Non-goals
- No C++ implementation, shader implementation, source file moves, or behavior changes.
- No copy/paste from `src/legacy` into promoted graphics layers.
- No final legacy source deletion; deletion readiness is governed by [`GRAPHICS-020`](GRAPHICS-020-legacy-graphics-retirement-gates.md) and later `LEGACY-*` tasks.

## Context
- This task was the initial rendering parity index. It is now retired because its child task chain has been opened and either retired or moved into newer DAG records.
- The current rendering task selection source is [`tasks/backlog/rendering/README.md`](../backlog/rendering/README.md), not this retired task.
- The earlier seed `RORG-031B — Rendering pipeline backlog seed` is a superseded historical parent seed retired to [`RORG-031B-rendering-pipeline-backlog-seed.md`](RORG-031B-rendering-pipeline-backlog-seed.md); it is not an active implementation plan.
- Owner: `graphics` planning across `graphics/rhi`, `graphics/vulkan`, `graphics/framegraph`, `graphics/renderer`, and `graphics/assets`.
- `src/legacy/Graphics` remains a behavioral reference only. Final graphics code consumes snapshots/views and must not depend on live ECS ownership.
- Accepted GPU-driven direction: runtime composes the CPU render scene from ECS/assets/geometry, stores extraction-side mappings or GPU handle sidecars, and submits immutable graphics snapshots; graphics owns GPU scene/buffer contracts and must not store graphics GPU handles in canonical `src/ecs` components.

Initial legacy-inspired feature inventory:

- Render orchestration: render driver, render pipeline/path, render graph, global resources, presentation.
- Primitive passes: surface, line, point, shadow, picking, selection outline, debug view, ImGui, composition.
- Post-process passes: bloom, FXAA, SMAA, histogram, tone map, shared post-process settings.
- Retained GPU scene/world: mesh, graph, point-cloud, mesh-view, dirty-property sync, primitive BVH build, allocation lifetime.
- Materials/shaders/assets: material registry, shader registry/compiler/hot reload, pipeline library, texture loading, GPU asset residency.
- Visualization/debug: colormaps, color mapping, property enumeration, isolines, vector fields, overlays, Htex patch preview, transient debug draw, spatial debug visualizers.
- Interaction/presentation adjacency: camera/view packets, picking requests, transform gizmo render packets, runtime-owned mutation.
- IO adjacency: legacy model/import/export features must be routed to `assets`/`geometry`, not promoted into graphics.

Coverage decision for the accepted GPU-driven direction:

- Canonical instance-slot buffers cover retained renderable identity/state, culling, material/light references, picking IDs, and draw buckets.
- Auxiliary GPU resources cover per-domain visualization attributes, debug primitive streams, post-process/LUT/readback resources, texture residency, and Htex/visualization atlases.
- Runtime-only coverage includes extraction sidecars, dirty-domain decisions, selection refinement, camera/input/gizmo mutation, ImGui draw-data production, and async visualization baking.
- Assets/geometry coverage includes file IO, model loading, `PropertySet` authority, spatial structures, and geometry algorithms.

## Required changes
- [x] Audit `src/legacy/Graphics`, `src/legacy/RHI`, and legacy runtime render-extraction modules for feature coverage.
- [x] Audit existing `src/graphics` modules and identify gaps against the canonical pass/resource contract.
- [x] Keep this task updated as the index for `GRAPHICS-002` and later reimplementation tasks until the rendering DAG supersedes it.

Task index and final disposition:

- [x] [`GRAPHICS-002`](GRAPHICS-002-render-world-contract.md) render-world/frame-input snapshot contract.
- [x] [`GRAPHICS-016`](GRAPHICS-016-runtime-extraction-handoff.md) runtime extraction and graphics handoff.
- [x] [`GRAPHICS-003`](GRAPHICS-003-frame-recipe-pipeline.md) frame recipe and default pipeline.
- [x] [`GRAPHICS-004`](GRAPHICS-004-gpu-world-allocation-lifetime.md) GPU-world allocation/lifetime.
- [x] [`GRAPHICS-005`](GRAPHICS-005-gpu-world-compaction.md) optional GPU-world compaction.
- [x] [`GRAPHICS-006`](GRAPHICS-006-material-shader-pipeline-registry.md) material/shader/pipeline registry.
- [x] [`GRAPHICS-007`](GRAPHICS-007-culling-and-draw-buckets.md) culling and draw buckets.
- [x] [`GRAPHICS-008`](GRAPHICS-008-depth-surface-gbuffer-passes.md) depth/surface/G-buffer passes.
- [x] [`GRAPHICS-009`](GRAPHICS-009-deferred-lighting-and-shadows.md) deferred lighting and shadows.
- [x] [`GRAPHICS-010`](GRAPHICS-010-lines-points-debug-primitives.md) line/point/transient debug primitive passes.
- [x] [`GRAPHICS-011`](GRAPHICS-011-spatial-debug-visualizers.md) spatial debug visualizers.
- [x] [`GRAPHICS-012`](GRAPHICS-012-picking-selection-outline.md) picking and selection outline.
- [x] [`GRAPHICS-013A`](GRAPHICS-013A-postprocess-chain.md) postprocess chain.
- [x] [`GRAPHICS-013B`](GRAPHICS-013B-debug-view-and-render-target-inspection.md) debug-view and render-target inspection contracts.
- [x] [`GRAPHICS-013C`](GRAPHICS-013C-imgui-overlay-and-present.md) ImGui overlay and present/finalization contracts.
- [x] [`GRAPHICS-014`](GRAPHICS-014-visualization-attributes-overlays.md) visualization attributes and overlays.
- [x] [`GRAPHICS-015`](GRAPHICS-015-gpu-assets-textures-residency.md) GPU assets/textures/residency.
- [x] [`GRAPHICS-017`](GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md) camera, interaction, and gizmo ownership boundaries.
- [x] [`GRAPHICS-018`](GRAPHICS-018-vulkan-renderer-integration.md) Vulkan renderer integration.
- [x] [`GRAPHICS-019`](GRAPHICS-019-legacy-graphics-io-boundaries.md) legacy graphics IO boundary split to assets/geometry owners.
- [x] [`GRAPHICS-020`](GRAPHICS-020-legacy-graphics-retirement-gates.md) legacy graphics retirement gates.
- [x] [`GRAPHICS-021`](GRAPHICS-021-rendering-backlog-workflow-cleanup.md) rendering backlog workflow cleanup.
- [x] [`GRAPHICS-022`](GRAPHICS-022-rendergraph-diagnostics-validation.md) rendergraph diagnostics and validation.
- [x] [`GRAPHICS-023`](GRAPHICS-023-shader-material-texture-hot-reload.md) shader/material/texture hot reload.
- [x] [`GRAPHICS-024`](GRAPHICS-024-overlays-presentation-editor-handoff.md) overlays, presentation adjacency, and editor handoff.
- [x] [`GRAPHICS-025`](GRAPHICS-025-hybrid-transparent-special-material-path.md) hybrid/transparent/special-material forward path follow-up.

## What remains
- Nothing remains inside `GRAPHICS-001`; it is a retired planning/inventory task.
- Current rendering task selection lives in [`tasks/backlog/rendering/README.md`](../backlog/rendering/README.md).
- Legacy graphics deletion readiness lives in [`GRAPHICS-020`](GRAPHICS-020-legacy-graphics-retirement-gates.md), [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md), and the `LEGACY-*` architecture backlog tasks.
- Vulkan-specific operational work already has opt-in `gpu;vulkan` evidence in retired descendants through `GRAPHICS-037D`, `GRAPHICS-038E`, and the default-recipe smoke/readback chain. Future vendor reconstructor backends remain unopened until the relevant SDK is integrated.
- The legacy CUDA graphics/RHI side path remains a keep/remove decision for the final legacy deletion program; no promoted default graphics path owns CUDA today.

## Tests
- [x] Run task policy validation after adding or updating backlog files.
- [x] Run documentation link validation when adding markdown links.

## Docs
- [x] Keep this task aligned with [`docs/architecture/graphics.md`](../../docs/architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../../docs/architecture/rendering-three-pass.md), and [`docs/migration/nonlegacy-parity-matrix.md`](../../docs/migration/nonlegacy-parity-matrix.md).

## Acceptance criteria
- [x] Legacy-inspired graphics feature areas are represented by scoped follow-up tasks.
- [x] Each follow-up task identifies owner layer, non-goals, tests, docs, and forbidden changes.
- [x] Work ordering and dependency gates are clear enough for future active-task selection.
- [x] This task no longer appears as active backlog work.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Completion
- Completed: 2026-06-06.
- Commit reference: this task-retirement commit.
- Notes: `GRAPHICS-001` is now a historical parity seed. Follow-up selection proceeds through the rendering DAG and the legacy retirement matrix.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Treating `src/legacy` as a copy source instead of a behavioral reference.
