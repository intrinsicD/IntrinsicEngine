# GRAPHICS-001 — Rendering parity inventory and task index

## Goal
- Create the canonical backlog entry that inventories legacy-inspired rendering features and indexes the reimplementation tasks for `src/graphics`.

## Non-goals
- No C++ implementation, shader implementation, file moves, or behavior changes.
- No copy/paste from `src/legacy` into promoted graphics layers.

## Context
- This task is the canonical rendering backlog index. The earlier seed
  `RORG-031B — Rendering pipeline backlog seed` is a superseded historical
  parent seed and has been retired to
  `tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md`; it is not an
  active implementation plan and must not be selected as next-active work.
- Owner: `graphics` planning across `graphics/rhi`, `graphics/vulkan`, `graphics/framegraph`, `graphics/renderer`, and `graphics/assets`.
- `src/legacy/Graphics` contains render orchestration, passes, materials, shader registries, visualization, selection, debug drawing, and lifecycle systems that are behavioral references only.
- Final graphics code must consume snapshots/views and must not depend on live ECS ownership.
- Accepted GPU-driven direction: runtime composes the CPU render scene from ECS/assets/geometry, stores any extraction-side mappings or GPU handle sidecars, and submits immutable graphics snapshots; graphics owns the GPU scene/buffer contracts and must not store graphics GPU handles in canonical `src/ecs` components.

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
- Assets/geometry coverage includes file IO, model loading, PropertySet authority, spatial structures, and geometry algorithms.

## Required changes
- Audit `src/legacy/Graphics`, `src/legacy/RHI`, and legacy runtime render-extraction modules for feature coverage.
- Audit existing `src/graphics` modules and identify gaps against the canonical pass/resource contract.
- Keep this task updated as the index for `GRAPHICS-002` and later reimplementation tasks.

Task index and intended order:

1. `GRAPHICS-002` render-world/frame-input snapshot contract.
2. `GRAPHICS-016` runtime extraction and graphics handoff, because CPU composition and sidecar mapping ownership must be fixed before GPU-scene implementation.
3. `GRAPHICS-003` frame recipe and default pipeline.
4. `GRAPHICS-004` GPU-world allocation/lifetime before retained-geometry-heavy work.
5. `GRAPHICS-005` optional GPU-world compaction after allocation metadata exists.
6. `GRAPHICS-006` material/shader/pipeline registry before pass shading integration.
7. `GRAPHICS-007` culling and draw buckets before primitive, shadow, and selection passes.
8. `GRAPHICS-008` depth/surface/G-buffer passes.
9. `GRAPHICS-009` deferred lighting and shadows after G-buffer and bucket contracts.
10. `GRAPHICS-010` line/point/transient debug primitive passes.
11. `GRAPHICS-011` spatial debug visualizers on top of debug primitive packets.
12. `GRAPHICS-012` picking and selection outline after primitive pass contracts.
13. `GRAPHICS-013A` postprocess chain (bloom, FXAA, SMAA, tone map, histogram, HDR→LDR, and postprocess lifetimes).
14. `GRAPHICS-013B` debug-view and render-target inspection contracts.
15. `GRAPHICS-013C` ImGui overlay and present/finalization contracts.
16. `GRAPHICS-014` visualization attributes and overlays.
17. `GRAPHICS-015` GPU assets/textures/residency.
18. `GRAPHICS-017` camera, interaction, and gizmo ownership boundaries.
19. `GRAPHICS-018` Vulkan renderer integration after CPU/null contracts stabilize.
20. `GRAPHICS-019` legacy graphics IO boundary split to assets/geometry owners.
21. `GRAPHICS-020` legacy graphics retirement gates.
22. `GRAPHICS-022` rendergraph diagnostics and validation (infrastructure hardening for deterministic contract failures).

## Tests
- Run task policy validation after adding or updating backlog files.
- Run documentation link validation when adding markdown links.

## Docs
- Keep this task aligned with `docs/architecture/graphics.md`, `docs/architecture/rendering-three-pass.md`, and `docs/migration/nonlegacy-parity-matrix.md`.

## Acceptance criteria
- Legacy-inspired graphics feature areas are represented by scoped follow-up tasks.
- Each follow-up task identifies owner layer, non-goals, tests, docs, and forbidden changes.
- Work ordering and dependency gates are clear enough for future active-task selection.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Treating `src/legacy` as a copy source instead of a behavioral reference.
