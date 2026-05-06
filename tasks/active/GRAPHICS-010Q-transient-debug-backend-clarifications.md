# GRAPHICS-010Q — Transient debug backend clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-009Q` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-Q4y6S`.
- Implementation commit: pending local agent workflow handoff.
- Task-state commit: pending local agent workflow handoff.

## Decisions
- **Transient packet expansion path.** Keep transient debug line/point/triangle
  packets out of `GpuWorld` and out of the canonical `CullingPass` buckets.
  Concrete backend implementations expand the renderer-owned, sanitized
  `DebugPrimitiveSnapshot` spans (`Graphics.RenderWorld.cppm` `DebugLinePacket`/
  `DebugPointPacket`/`DebugTrianglePacket` and the `Lines`/`Points`/`Triangles`
  spans copied in `Graphics.Renderer::SubmitRuntimeSnapshots`/`PrepareFrame`)
  through **dedicated per-frame host-visible (transient) GPU buffers** owned
  by a future backend upload helper. This matches the existing performance
  contract that "Transient paths use per-frame host-visible buffers with
  bounded growth and telemetry overflow counters" in
  `docs/architecture/rendering-three-pass.md` (`## Performance Intent`) and
  preserves the canonical instance-slot invariant: retained-pool generation
  checks (`GpuWorld::Diagnostics`, GRAPHICS-007Q stance) must not be polluted
  by ephemeral primitives, and `GpuRender_Surface`/`GpuRender_Line`/
  `GpuRender_Point` flags remain reserved for retained renderable extraction.
  The existing `Cull.Lines.IndexedArgs`/`Cull.Lines.Count`/
  `Cull.Points.NonIndexedArgs`/`Cull.Points.Count` cull-bucket resources stay
  reserved for retained `GpuRender_Line`/`GpuRender_Point` renderables and are
  **not** reused for transient debug expansion.
- **Debug-triangle backend path.** Route `DebugTrianglePacket` expansion
  through a **dedicated debug-surface overlay** that draws into
  `SceneColorHDR`/`SceneDepth` after the lit composition (alongside the
  existing `LinePass`/`PointPass` overlays); do **not** reuse
  `Pass.Forward.Surface` or `Pass.Deferred.GBuffers`. The surface and
  G-buffer paths consume only the canonical `SurfaceOpaque`/`SurfaceAlphaMask`
  cull buckets per the GRAPHICS-008Q policy ("Empty/invalid bucket diagnostics"
  and the alpha-mask reserved-infrastructure paragraph in
  `rendering-three-pass.md` `### Depth, surface, and G-buffer command
  contract`); the alpha-mask bucket is reserved infrastructure until material
  alpha evaluation lands and transient debug triangles are not retained
  renderables. The debug-surface overlay's source module lands next to
  `Extrinsic.Graphics.Pass.Forward.Line` / `Extrinsic.Graphics.Pass.Forward.Point`
  in `src/graphics/renderer/Passes/`; the logical pass slots into the pipeline
  order between `LinePass`/`PointPass` and `PostProcessPass`. The
  numbered pipeline-order step is intentionally not added to docs in this
  task — it lands when the implementation does, under GRAPHICS-018, to keep
  the architecture doc factual rather than aspirational. Future hybrid or
  forward-overlay merging with `Pass.Forward.Surface` is out of scope and
  remains owned by GRAPHICS-025.
- **Depth-tested vs overlay routing.** Express the per-packet `DepthTested`
  boolean (`DebugLinePacket::DepthTested`, `DebugPointPacket::DepthTested`,
  `DebugTrianglePacket::DepthTested`) as **two pipeline variants per
  primitive lane** and **not** as separate cull buckets or separate frame-graph
  resources. The depth-tested variant attaches via `LOAD` with
  `CompareOp = Less`; the overlay variant attaches via `LOAD` with
  depth-test disabled and depth-write disabled. The future backend upload
  helper sorts per-frame packets into the two variants on the CPU side
  before recording draws, and the existing `LinePass`/`PointPass` (and the
  new debug-surface overlay) bind the appropriate variant. This mirrors the
  GRAPHICS-008Q precedent that pipeline state owns alpha-mask vs opaque and
  depth-equal vs depth-less behavior rather than introducing new buckets;
  no new `FrameRecipe` resource is introduced and both variants attach via
  `LOAD` to `SceneColorHDR`/`SceneDepth` exactly as the existing line/point
  command contract specifies.
- **Diagnostics and limits.** `RenderWorld::InvalidSnapshotRecordCount`
  remains the **single CPU diagnostic surface** for malformed transient
  records (incremented today by `Graphics.Renderer::PrepareFrame` for each
  rejected `DebugLinePacket`/`DebugPointPacket`/`DebugTrianglePacket`); it
  is not mirrored into `GpuWorld::Diagnostics`, which stays reserved for
  retained-pool lifetime per GRAPHICS-007Q. The future backend upload
  helper owns a deterministic per-frame upload budget (line/point/triangle
  counts and total transient-buffer byte size); counts that exceed the
  budget are clamped at the helper boundary and reported through a future
  `TransientDebugUploadDiagnostics` field associated with the transient
  upload helper — that field's introduction is implementation work tracked
  under GRAPHICS-018 Vulkan integration scope and is **not** added in this
  docs-only task. Backend allocation failures (transient buffer
  suballocation exhausted, descriptor ring pressure) report through that
  same diagnostics surface and cause the helper to skip command recording
  for the affected lane deterministically, matching the silent-no-op policy
  for empty/invalid culling buckets in `Pass.DepthPrepass`/
  `Pass.Forward.Surface`/`Pass.Deferred.GBuffers` documented under the
  GRAPHICS-008Q decision. No GPU diagnostics counter buffer is introduced
  by default, mirroring the GRAPHICS-007Q stance.

## Resolution
- Decisions recorded above and consequential notes synced into the
  "Line and point command contract" section of
  `docs/architecture/rendering-three-pass.md` and into the transient-debug
  ownership bullet in `src/graphics/renderer/README.md`.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify backend/runtime handoff decisions for expanding `GRAPHICS-010` transient debug line/point/triangle packets into concrete GPU buffers and draw calls.

## Non-goals
- No C++ behavior changes.
- No Vulkan-only implementation.
- No persistent editor overlay entity factory work.

## Context
- Owner: graphics renderer architecture and future backend integration notes.
- `GRAPHICS-010` established renderer-owned transient debug packet spans, sanitization rules, line/point pass command contracts, and line/point frame-recipe cull resources.
- Remaining questions affect backend upload strategy and should not block spatial debug visualizer packet generation in `GRAPHICS-011`.

## Required changes
- Decide whether transient debug packets are expanded into dedicated per-frame GPU buffers or folded into canonical `GpuWorld`/culling buckets by a staging upload system.
- Clarify whether debug triangles reuse the surface/G-buffer path, a dedicated debug-surface pass, or a future hybrid/overlay path.
- Clarify per-packet depth-tested versus overlay lane routing and whether that becomes separate buckets or pipeline variants.
- Clarify diagnostics/limits for excessive transient primitive counts and backend allocation failures.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md` and `src/graphics/renderer/README.md` with the chosen upload and backend routing policy.

## Acceptance criteria
- Later Vulkan/backend work can implement transient debug packet upload without changing graphics/runtime ownership boundaries.
- Spatial visualizer work can generate packet spans without depending on backend upload internals.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS/editor ownership into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

