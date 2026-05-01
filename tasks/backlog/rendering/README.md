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

- [GRAPHICS-021 — Rendering backlog workflow cleanup](GRAPHICS-021-rendering-backlog-workflow-cleanup.md):
  must complete before further rendering task churn (task-format/validator/doc
  hygiene precondition for the rest of the queue).
- [GRAPHICS-016 — Runtime extraction and graphics handoff](GRAPHICS-016-runtime-extraction-handoff.md):
  first implementation gate. Must land before any graphics pass implementation
  work begins, because promoted graphics must consume snapshots/views and must
  not depend on live ECS ownership.
- [GRAPHICS-002 — Render-world / frame-input snapshot contract](GRAPHICS-002-render-world-contract.md):
  depends on GRAPHICS-016, or must explicitly avoid touching runtime
  extraction.
- [GRAPHICS-003 — Frame recipe and default pipeline](GRAPHICS-003-frame-recipe-pipeline.md):
  depends on GRAPHICS-002.
- [GRAPHICS-004 — GPU-world allocation and lifetime](GRAPHICS-004-gpu-world-allocation-lifetime.md):
  depends on GRAPHICS-002.
- [GRAPHICS-005 — GPU-world compaction](GRAPHICS-005-gpu-world-compaction.md):
  depends on GRAPHICS-004.
- [GRAPHICS-006 — Material/shader/pipeline registry](GRAPHICS-006-material-shader-pipeline-registry.md):
  depends on GRAPHICS-002.
- [GRAPHICS-007 — Culling and draw buckets](GRAPHICS-007-culling-and-draw-buckets.md):
  depends on GRAPHICS-002 and GRAPHICS-004.
- [GRAPHICS-008 — Depth/surface/G-buffer passes](GRAPHICS-008-depth-surface-gbuffer-passes.md):
  depends on GRAPHICS-003, GRAPHICS-006, and GRAPHICS-007.
- [GRAPHICS-009 — Deferred lighting and shadows](GRAPHICS-009-deferred-lighting-and-shadows.md):
  depends on GRAPHICS-008.
- [GRAPHICS-010 — Lines, points, and debug primitives](GRAPHICS-010-lines-points-debug-primitives.md):
  depends on GRAPHICS-002, GRAPHICS-003, and GRAPHICS-007.
- [GRAPHICS-011 — Spatial debug visualizers](GRAPHICS-011-spatial-debug-visualizers.md):
  depends on GRAPHICS-010.
- [GRAPHICS-012 — Picking, selection, and outline](GRAPHICS-012-picking-selection-outline.md):
  depends on GRAPHICS-002, GRAPHICS-007, GRAPHICS-008, and GRAPHICS-010.
- [GRAPHICS-013 — Postprocess, debug view, ImGui, and present](GRAPHICS-013-postprocess-debugview-imgui-present.md)
  (or its split replacements once Task 9 lands): depends on GRAPHICS-003 and on
  GRAPHICS-008/GRAPHICS-009 wherever scene-color or HDR inputs are required.
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
  GRAPHICS-013.
- [GRAPHICS-019 — Legacy graphics IO boundaries](GRAPHICS-019-legacy-graphics-io-boundaries.md):
  may run as planning in parallel with the implementation tasks above, but
  must not put IO ownership into graphics.
- [GRAPHICS-020 — Legacy graphics retirement gates](GRAPHICS-020-legacy-graphics-retirement-gates.md):
  final retirement gating after parity tasks complete.

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
