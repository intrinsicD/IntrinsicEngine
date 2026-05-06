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
- [GRAPHICS-003 — Frame recipe and default pipeline](../../done/GRAPHICS-003-frame-recipe-pipeline.md):
  completed reusable default frame recipe and canonical graph-construction gate.
- [GRAPHICS-004 — GPU-world allocation and lifetime](../../done/GRAPHICS-004-gpu-world-allocation-lifetime.md):
  completed retained-pool allocation/lifetime gate; depends on GRAPHICS-002.
- [GRAPHICS-005 — GPU-world compaction](../../done/GRAPHICS-005-gpu-world-compaction.md):
  completed managed-buffer fragmentation, opt-in compaction planning, and
  relocation reporting gate; depends on GRAPHICS-004.
- [GRAPHICS-006 — Material/shader/pipeline registry](../../done/GRAPHICS-006-material-shader-pipeline-registry.md):
  depends on GRAPHICS-002.
- [GRAPHICS-006Q — Material registry clarification backlog](../../done/GRAPHICS-006Q-material-registry-clarifications.md):
  completed clarification-only follow-up for GRAPHICS-006 material-slot,
  material-layout, and shader-asset identity decisions.
- [GRAPHICS-007 — Culling and draw buckets](../../done/GRAPHICS-007-culling-and-draw-buckets.md):
  depends on GRAPHICS-002 and GRAPHICS-004. Owns `CullingPass`
  (source module `Extrinsic.Graphics.Pass.Culling`) command contracts and the
  GPU draw-bucket contracts consumed by the surface, line, point, shadow, and
  selection passes.
- [GRAPHICS-007Q — Culling bucket clarification follow-ups](GRAPHICS-007Q-culling-bucket-clarifications.md):
  clarification-only backlog for selection bucket specialization, layer-mask
  policy, culling diagnostics ownership, and unsupported bucket combinations.
- [GRAPHICS-008 — Depth/surface/G-buffer passes](../../done/GRAPHICS-008-depth-surface-gbuffer-passes.md):
  depends on GRAPHICS-003, GRAPHICS-006, and GRAPHICS-007.
- [GRAPHICS-008Q — Surface pass clarification follow-ups](GRAPHICS-008Q-surface-pass-clarifications.md):
  clarification-only backlog for alpha-mask depth/G-buffer policy, descriptor
  bind seams, renderpass attachment ownership, and empty-bucket diagnostics.
- [GRAPHICS-009 — Deferred lighting and shadows](../../done/GRAPHICS-009-deferred-lighting-and-shadows.md):
  depends on GRAPHICS-008.
- [GRAPHICS-009Q — Lighting and shadow clarification follow-ups](GRAPHICS-009Q-lighting-shadow-clarifications.md):
  nonblocking docs-only follow-up for shadow atlas sizing input, backend sampler binding, cascade extraction ownership, and deferred-lighting debug mode seams. It must not be mixed with C++ behavior work.
- [GRAPHICS-010 — Lines, points, and debug primitives](../../done/GRAPHICS-010-lines-points-debug-primitives.md):
  depends on GRAPHICS-002, GRAPHICS-003, and GRAPHICS-007.
- [GRAPHICS-010Q — Transient debug backend clarification follow-ups](GRAPHICS-010Q-transient-debug-backend-clarifications.md):
  nonblocking docs-only follow-up for transient debug packet upload/routing, debug triangle backend path, overlay bucket policy, and excessive-count diagnostics.
- [GRAPHICS-011 — Spatial debug visualizers](../../done/GRAPHICS-011-spatial-debug-visualizers.md):
  depends on GRAPHICS-010.
- [GRAPHICS-011Q — Spatial debug adapter clarification follow-ups](GRAPHICS-011Q-spatial-debug-adapter-clarifications.md):
  nonblocking docs-only follow-up for concrete BVH/KD-tree/octree/convex-hull adapter ownership and diagnostics handoff.
- [GRAPHICS-012 — Picking, selection, and outline](../../done/GRAPHICS-012-picking-selection-outline.md):
  depends on GRAPHICS-002, GRAPHICS-007, GRAPHICS-008, and GRAPHICS-010. Owns
  the logical `PickingPass` stage (split source modules
  `Pass.Selection.EntityId`/`FaceId`/`EdgeId`/`PointId`) and the
  `SelectionOutlinePass` (`Pass.Selection.Outline`); see the Pass module
  naming map in `docs/architecture/rendering-three-pass.md`.
- [GRAPHICS-012Q — Picking backend/runtime clarification follow-ups](GRAPHICS-012Q-picking-backend-runtime-clarifications.md):
  nonblocking docs-only follow-up for shader-side ID encoding, backend readback, runtime ECS selection resolution, and transparent/special forward picking eligibility.
- [GRAPHICS-013 — Postprocess/debug-view/ImGui/present umbrella index](GRAPHICS-013-postprocess-debugview-imgui-present.md):
  planning-only umbrella; execute through GRAPHICS-013A/B/C.
- [GRAPHICS-013A — Postprocess chain](../../done/GRAPHICS-013A-postprocess-chain.md):
  depends on GRAPHICS-003 and on GRAPHICS-008/GRAPHICS-009 wherever
  scene-color or HDR inputs are required.
- [GRAPHICS-013AQ — Postprocess backend clarification follow-ups](GRAPHICS-013AQ-postprocess-backend-clarifications.md):
  nonblocking docs-only follow-up for bloom backend shape, histogram/exposure history, AA shader/LUT policy, and postprocess descriptor ownership.
- [GRAPHICS-013B — Debug view and render-target inspection](../../done/GRAPHICS-013B-debug-view-and-render-target-inspection.md):
  depends on GRAPHICS-013A.
- [GRAPHICS-013BQ — Debug-view backend clarification follow-ups](GRAPHICS-013BQ-debug-view-backend-clarifications.md):
  nonblocking docs-only follow-up for shader visualization modes, selected-resource descriptors, UI resource-name mapping, and buffer inspection policy.
- [GRAPHICS-013C — ImGui overlay and present/finalization](../../done/GRAPHICS-013C-imgui-overlay-and-present.md):
  depends on GRAPHICS-013B.
- [GRAPHICS-013CQ — ImGui/present backend clarification follow-ups](GRAPHICS-013CQ-imgui-present-backend-clarifications.md):
  nonblocking docs-only follow-up for ImGui draw-data translation, overlay upload/descriptor policy, present finalization strategy, and platform/backend ownership boundaries.
- [GRAPHICS-014 — Visualization attributes and overlays](../../done/GRAPHICS-014-visualization-attributes-overlays.md):
  depends on GRAPHICS-002 and GRAPHICS-010, and on GRAPHICS-015 wherever
  texture/atlas resources are required.
- [GRAPHICS-014Q — Visualization runtime/backend clarification follow-ups](GRAPHICS-014Q-visualization-runtime-backend-clarifications.md):
  nonblocking docs-only follow-up for runtime/geometry visualization packet producers, invalid-packet handling, overlay upload strategy, and UV/Htex bake selection policy.
- [GRAPHICS-015 — GPU assets, textures, and residency](../../done/GRAPHICS-015-gpu-assets-textures-residency.md):
  depends on GRAPHICS-006 wherever material texture references are involved.
- [GRAPHICS-015Q — Texture residency backend clarification follow-ups](GRAPHICS-015Q-texture-residency-backend-clarifications.md):
  nonblocking docs-only follow-up for cache capacity/eviction, streaming mips, fallback texture content, bindless descriptor flush cadence, and runtime upload scheduling policy.
- [GRAPHICS-017 — Camera, interaction, and gizmo boundaries](../../done/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md):
  depends on GRAPHICS-012 for picking handoff. Camera packet contracts may be
  defined earlier without blocking.
- [GRAPHICS-017Q — Camera/gizmo runtime clarification follow-ups](GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md):
  nonblocking docs-only follow-up for runtime camera controllers, pick-request scheduling, transform-gizmo hit testing, interaction state, and transform application ownership.
- [GRAPHICS-018 — Vulkan renderer integration](../../done/GRAPHICS-018-vulkan-renderer-integration.md):
  depends on stable CPU/null contracts from GRAPHICS-002, GRAPHICS-003,
  GRAPHICS-004, GRAPHICS-006, GRAPHICS-007, GRAPHICS-008, GRAPHICS-009, and
  GRAPHICS-013C.
- [GRAPHICS-018Q — Vulkan integration clarification follow-ups](GRAPHICS-018Q-vulkan-integration-clarifications.md):
  nonblocking docs-only follow-up for platform/window fixtures in opt-in
  swapchain smoke tests, shader/pipeline asset packaging, and resize/present
  failure diagnostic taxonomy. It must not be mixed with C++ behavior work.
- [GRAPHICS-019 — Legacy graphics IO boundaries](GRAPHICS-019-legacy-graphics-io-boundaries.md):
  may run as planning in parallel with the implementation tasks above, but
  must not put IO ownership into graphics.
- [GRAPHICS-020 — Legacy graphics retirement gates](GRAPHICS-020-legacy-graphics-retirement-gates.md):
  final retirement gating after parity tasks complete.
- [GRAPHICS-022 — Rendergraph diagnostics and validation](../../done/GRAPHICS-022-rendergraph-diagnostics-validation.md):
  completed follow-on infrastructure hardening task; canonical ordering matches
  GRAPHICS-001 and it depends on GRAPHICS-003 for frame-recipe context while
  remaining CPU/null testable (no Vulkan requirement).
- [GRAPHICS-023 — Shader, material, and texture hot reload](GRAPHICS-023-shader-material-texture-hot-reload.md):
  depends on GRAPHICS-006 for registry/material/pipeline contracts and
  GRAPHICS-015 for GPU asset/texture residency.
- [GRAPHICS-024 — Overlays, presentation adjacency, and editor handoff](../../done/GRAPHICS-024-overlays-presentation-editor-handoff.md):
  completed planning task; recorded the per-row owner matrix for legacy
  overlay/presentation/editor-adjacent behaviors and cross-linked
  GRAPHICS-010/011/014/017 done tasks plus this backlog index. Retirement
  gating in GRAPHICS-020 resolves overlay/presentation modules through
  the matrix in `../../../docs/migration/nonlegacy-parity-matrix.md`.
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
