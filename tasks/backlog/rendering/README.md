# Rendering Backlog Index

This README is the agent-facing entry point into the `tasks/backlog/rendering/`
queue. It complements the canonical inventory and points agents at the next
valid task without requiring them to read every file.

## Canonical sources

- [GRAPHICS-001 — Rendering parity inventory and task index](GRAPHICS-001-rendering-parity-inventory.md)
  is the canonical rendering backlog index. The prose ordering inside
  GRAPHICS-001 remains authoritative whenever this README and GRAPHICS-001
  disagree on intent; this README expresses the same ordering as a
  machine-readable DAG.
- `RORG-031B`, if encountered in older notes, is **historical/superseded
  planning** that has been retired to
  [`tasks/done/RORG-031B-rendering-pipeline-backlog-seed.md`](../../done/RORG-031B-rendering-pipeline-backlog-seed.md).
  It is not the active implementation path and must not be selected as
  next-active work.

## Dependency DAG

The following list is the minimum dependency order for the rendering backlog.
Each entry lists the upstream tasks that must be complete (or explicitly
out-of-scope) before the entry is eligible for "in-progress" selection.

- [GRAPHICS-021 — Rendering backlog workflow cleanup](../../done/GRAPHICS-021-rendering-backlog-workflow-cleanup.md):
  completed cleanup precondition for further rendering task churn
  (task-format/validator/doc hygiene for the rest of the queue).
- [GRAPHICS-016 — Runtime extraction and graphics handoff](../../done/GRAPHICS-016-runtime-extraction-handoff.md):
  completed first implementation gate for runtime-owned extraction and graphics
  snapshot handoff. Downstream graphics pass work can proceed through
  GRAPHICS-002+ without introducing live ECS ownership into graphics.
- [GRAPHICS-002 — Render-world / frame-input snapshot contract](../../done/GRAPHICS-002-render-world-contract.md):
  completed immutable render-world/frame-input snapshot contract gate for
  downstream pass work.
- [GRAPHICS-003 — Frame recipe and default pipeline](GRAPHICS-003-frame-recipe-pipeline.md):
  depends on GRAPHICS-002.
- [GRAPHICS-004 — GPU-world allocation and lifetime](GRAPHICS-004-gpu-world-allocation-lifetime.md):
  depends on GRAPHICS-002.
- [GRAPHICS-005 — GPU-world compaction](GRAPHICS-005-gpu-world-compaction.md):
  depends on GRAPHICS-004.
- [GRAPHICS-006 — Material/shader/pipeline registry](GRAPHICS-006-material-shader-pipeline-registry.md):
  depends on GRAPHICS-002.
- [GRAPHICS-007 — Culling and draw buckets](GRAPHICS-007-culling-and-draw-buckets.md):
  depends on GRAPHICS-002 and GRAPHICS-004. Owns `CullingPass`
  (source module `Extrinsic.Graphics.Pass.Culling`) command contracts and the
  GPU draw-bucket contracts consumed by the surface, line, point, shadow, and
  selection passes.
- [GRAPHICS-008 — Depth/surface/G-buffer passes](GRAPHICS-008-depth-surface-gbuffer-passes.md):
  depends on GRAPHICS-003, GRAPHICS-006, and GRAPHICS-007.
- [GRAPHICS-009 — Deferred lighting and shadows](GRAPHICS-009-deferred-lighting-and-shadows.md):
  depends on GRAPHICS-008.
- [GRAPHICS-010 — Lines, points, and debug primitives](GRAPHICS-010-lines-points-debug-primitives.md):
  depends on GRAPHICS-002, GRAPHICS-003, and GRAPHICS-007.
- [GRAPHICS-011 — Spatial debug visualizers](GRAPHICS-011-spatial-debug-visualizers.md):
  depends on GRAPHICS-010.
- [GRAPHICS-012 — Picking, selection, and outline](GRAPHICS-012-picking-selection-outline.md):
  depends on GRAPHICS-002, GRAPHICS-007, GRAPHICS-008, and GRAPHICS-010. Owns
  the logical `PickingPass` stage (split source modules
  `Pass.Selection.EntityId`/`FaceId`/`EdgeId`/`PointId`) and the
  `SelectionOutlinePass` (`Pass.Selection.Outline`); see the Pass module
  naming map in `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-013 — Postprocess/debug-view/ImGui/present umbrella index](GRAPHICS-013-postprocess-debugview-imgui-present.md):
  planning-only umbrella; execute through GRAPHICS-013A/B/C.
- [GRAPHICS-013A — Postprocess chain](GRAPHICS-013A-postprocess-chain.md):
  depends on GRAPHICS-003 and on GRAPHICS-008/GRAPHICS-009 wherever
  scene-color or HDR inputs are required.
- [GRAPHICS-013B — Debug view and render-target inspection](GRAPHICS-013B-debug-view-and-render-target-inspection.md):
  depends on GRAPHICS-013A.
- [GRAPHICS-013C — ImGui overlay and present/finalization](GRAPHICS-013C-imgui-overlay-and-present.md):
  depends on GRAPHICS-013B.
- [GRAPHICS-014 — Visualization attributes and overlays](GRAPHICS-014-visualization-attributes-overlays.md):
  depends on GRAPHICS-002 and GRAPHICS-010, and on GRAPHICS-015 wherever
  texture/atlas resources are required.
- [GRAPHICS-015 — GPU assets, textures, and residency](GRAPHICS-015-gpu-assets-textures-residency.md):
  depends on GRAPHICS-006 wherever material texture references are involved.
- [GRAPHICS-017 — Camera, interaction, and gizmo boundaries](GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md):
  depends on GRAPHICS-012 for picking handoff. Camera packet contracts may be
  defined earlier without blocking.
- [GRAPHICS-018 — Vulkan renderer integration](GRAPHICS-018-vulkan-renderer-integration.md):
  depends on stable CPU/null contracts from GRAPHICS-002, GRAPHICS-003,
  GRAPHICS-004, GRAPHICS-006, GRAPHICS-007, GRAPHICS-008, GRAPHICS-009, and
  GRAPHICS-013C.
- [GRAPHICS-019 — Legacy graphics IO boundaries](GRAPHICS-019-legacy-graphics-io-boundaries.md):
  may run as planning in parallel with the implementation tasks above, but
  must not put IO ownership into graphics.
- [GRAPHICS-020 — Legacy graphics retirement gates](GRAPHICS-020-legacy-graphics-retirement-gates.md):
  final retirement gating after parity tasks complete.
- [GRAPHICS-022 — Rendergraph diagnostics and validation](GRAPHICS-022-rendergraph-diagnostics-validation.md):
  queued follow-on infrastructure hardening task; canonical ordering matches
  GRAPHICS-001 and it depends on GRAPHICS-003 for frame-recipe context while
  remaining CPU/null testable (no Vulkan requirement).
- [GRAPHICS-023 — Shader, material, and texture hot reload](GRAPHICS-023-shader-material-texture-hot-reload.md):
  depends on GRAPHICS-006 for registry/material/pipeline contracts and
  GRAPHICS-015 for GPU asset/texture residency.
- [GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff](GRAPHICS-024-overlays-presentation-editor-handoff.md):
  depends on GRAPHICS-010/011/014/017 ownership decisions where overlay,
  visualization, picking, camera, and editor handoff overlap; informs
  GRAPHICS-020 retirement gating.
- [GRAPHICS-025 — Hybrid, transparent, and special-material forward path](GRAPHICS-025-hybrid-transparent-special-material-path.md):
  future-facing follow-up after GRAPHICS-006/007/008/009 and GRAPHICS-013A
  establish material, bucket, opaque, lighting, and postprocess contracts.

## Agent selection rules

When picking the next rendering task to promote to active:

- **Prefer the earliest unblocked task** in the DAG above. "Unblocked" means
  every upstream dependency is either marked done in `tasks/done/` or has been
  explicitly recorded as not-in-scope for the candidate task.
- **Do not start pass implementation before GRAPHICS-016 extraction ownership
  is resolved.** Until live-ECS ownership is removed from promoted graphics
  APIs, downstream pass and pipeline tasks must not be promoted to active.
- **Do not mix docs-only cleanup with C++ behavior changes** in a single PR.
  Split the work into separate tasks/PRs rather than batching.
- **Do not copy legacy code into promoted graphics layers.** `src/legacy` is a
  behavioral reference only; promoted code must be reimplemented against the
  snapshot/view contracts.

## Related docs

- [`AGENTS.md`](../../../AGENTS.md) — authoritative repository agent contract.
- [`docs/architecture/graphics.md`](../../../docs/architecture/graphics.md) —
  graphics layer architecture and ownership boundaries.
- [`docs/architecture/rendering-three-pass.md`](../../../docs/architecture/rendering-three-pass.md) —
  canonical rendering pass architecture reference.
- [`docs/migration/nonlegacy-parity-matrix.md`](../../../docs/migration/nonlegacy-parity-matrix.md) —
  legacy-to-promoted parity tracking.
