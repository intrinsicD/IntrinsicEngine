# GRAPHICS-017Q — Camera/gizmo runtime clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-07 after `GRAPHICS-015Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-07.
- Branch: `claude/agentic-workflow-session-undXM`.
- Promotion commit: `b41e622` (move file from `tasks/backlog/rendering/` to `tasks/active/`).
- Implementation commit: `a152674` (resolve decisions and sync graphics / rendering-three-pass / renderer-README docs; refresh rendering backlog README description).
- Task-state commit: this retirement commit (moves the file from `tasks/active/` to `tasks/done/` and redirects the rendering backlog README link).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/graphics.md` (extended `Extrinsic.Graphics.CameraSnapshots` ownership bullet with a new "Per `GRAPHICS-017Q`" paragraph), `docs/architecture/rendering-three-pass.md` (new "Camera, picking-request, and gizmo runtime contract" subsection alongside the existing `GRAPHICS-013AQ`/`013BQ`/`013CQ`/`014Q`/`015Q` clarification paragraphs), and `src/graphics/renderer/README.md` (extended `Graphics.CameraSnapshots` ownership-contract bullet with a matching "Per `GRAPHICS-017Q`" paragraph). The rendering backlog README entry for `GRAPHICS-017Q` is redirected from `tasks/backlog/rendering/` to `tasks/active/` by the promotion commit and from `tasks/active/` to `tasks/done/` by this retirement commit. No `docs/migration/nonlegacy-parity-matrix.md` rows change because the existing matrix rows already cross-link to `GRAPHICS-017Q` for editor interaction, pick-request scheduling, and camera/gizmo mutation policy. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .`.

## Decisions

- **Runtime camera controller ownership and platform-input translation.**
  Concrete camera controllers live as runtime modules under the planned
  umbrella module name `Extrinsic.Runtime.CameraControllers`, mirroring
  the `Extrinsic.Runtime.SpatialDebugAdapters` pattern from
  `GRAPHICS-011Q`, the `Extrinsic.Runtime.VisualizationAdapters` pattern
  from `GRAPHICS-014Q`, and the `Extrinsic.Runtime.AssetBridges.Texture`
  pattern from `GRAPHICS-015Q`. Concrete controllers (orbit, fly,
  free-look, top-down) read platform input deltas through the existing
  platform input port and translate them into camera state mutations
  (target, distance, yaw/pitch/roll, position, look-at) on
  runtime-owned camera state. Each frame, runtime extraction reads the
  resulting view/projection from the active controller and fills
  `CameraViewInput` (eye, look-at/forward, up, fov, near/far,
  viewport pixel width/height, view/projection matrices), submits it
  through `IRenderer::SubmitRuntimeSnapshots()`, and graphics validates
  it through `Extrinsic.Graphics.CameraSnapshots` to produce the
  immutable `CameraViewSnapshot` (with extracted frustum planes)
  consumed by passes. Graphics never imports `src/platform/`, never
  polls window events, and never owns active-controller selection.
  Editor/app code may surface controller-selection UI but funnels the
  choice through the runtime adapter as a pre-extraction input rather
  than calling graphics-side camera builders directly. Multiple cameras
  (preview, top-down, editor secondary view) are runtime-owned: each
  emits its own `CameraViewInput` per frame; graphics consumes the
  resulting snapshot spans without owning controller fan-out logic.
  Reimplementing the legacy `Graphics::Camera` motion helpers under
  `Extrinsic.Runtime.CameraControllers` is a future runtime-task
  follow-up tracked through the existing
  `docs/migration/nonlegacy-parity-matrix.md` editor-handoff rows that
  already cross-link `GRAPHICS-017Q`; concrete task IDs are deliberately
  not allocated here because `GRAPHICS-020` is the gating task that
  consumes the matrix.

- **Pick-request scheduling and coalescing.**
  Runtime extraction owns input-to-`PickPixelRequest` translation. Each
  input frame's accepted picks (mouse-down, hover-pick, programmatic
  editor pick, async asset-pipeline pick) are collected into a
  per-frame queue at the runtime side; runtime emits the queue as the
  immutable `PickPixelRequest` span on `RenderFrameInput` once per
  frame, alongside `CameraViewInput`, through
  `IRenderer::SubmitRuntimeSnapshots()`. The span is **single-shot**:
  each entry maps to exactly one drained readback. Coalescing policy is
  runtime-owned: when an editor produces two pick requests for the
  same `(viewport, pixel, request_kind)` key in the same input tick,
  runtime keeps only the latest one before submitting the snapshot,
  matching the established "graphics never validates duplicates;
  runtime validates at extraction" stance from `GRAPHICS-014Q`. The
  per-frame readback drain mirrors `Picking.Readback` from
  `GRAPHICS-012Q`: the renderer copies requested pixels into the
  graphics-owned host-visible `Picking.Readback` buffer at frame-record
  time and drains them on the next `BeginFrame()` after the issuing
  frame's fences complete; valid samples invoke
  `SelectionSystem::PublishPickResult(...)` and `EntityId == 0` /
  invalidated requests / deterministic readback failures invoke
  `SelectionSystem::PublishNoHit()`. There is no graphics-side
  persistent pending-pick queue across frames and no graphics-side
  request-kind taxonomy; programmatic and mouse picks share the same
  single-shot path and graphics does not distinguish kinds. Editor
  policy may attach a runtime-side request-kind tag (mouse vs. async
  vs. gizmo handle pick) inside its own queue, but that tag is consumed
  by the runtime selection-resolution sidecar and never crosses into
  `RenderFrameInput`.

- **Transform-gizmo hit testing ownership.**
  Hit testing is runtime/editor-owned, under the planned umbrella
  module name `Extrinsic.Runtime.GizmoInteraction`, mirroring the
  `Extrinsic.Runtime.SpatialDebugAdapters` pattern. The hit-test path
  reads (a) the active selection's authoring transforms from runtime
  ECS/editor state, (b) the same `CameraViewSnapshot::ViewProjection`
  / `PickRay` derivation that graphics already produces from
  `CameraViewInput`, and (c) raw pointer pixel coordinates from the
  platform input port. Runtime computes axis/plane intersection in
  world or view space and produces the resulting interaction state.
  Graphics never receives raw pointer coordinates and never imports
  any gizmo hit-test code path. The `TransformGizmoRenderPacket` spans
  on `RenderWorld` carry only render-relevant data: world-space gizmo
  origin, gizmo scale (camera-relative for constant pixel size),
  active gizmo mode (translate / rotate / scale), highlighted axis or
  plane mask, and per-handle render flags. Active drag state, axis
  lock, screen-space drag origin, snap thresholds, and modifier-key
  state stay runtime-side and never enter graphics. Reusing
  `PickPixelRequest` for gizmo handle picking is permitted but not
  required: editor policy may either (a) use direct CPU
  ray-vs-gizmo intersection in runtime/editor — preferred for the
  dominant gizmo case because it avoids a frame of pick-readback
  latency — or (b) route handle picks through `PickPixelRequest`
  with a runtime-side request-kind tag that selects gizmo handles
  rather than scene entities. Either choice is invisible to graphics.

- **Interaction state storage and lifetime.**
  Interaction state is runtime/editor-owned. Implementations may store
  it as either an editor-side singleton (when gizmo interaction is
  editor-only) or as an ECS component (when gizmo interaction is part
  of an in-game tool). Graphics has no opinion on the storage choice
  and never imports either. Each frame's gizmo interaction state has at
  minimum: active mode (translate / rotate / scale, none if idle),
  affected axis or plane mask, accumulated delta in the gizmo's
  reference frame, drag origin in pointer pixels, snap mode and snap
  step, multi-select pivot policy (centroid / individual /
  last-selected), and orientation reference frame (local / world /
  view). None of these fields enter graphics: the renderer sees only
  the resulting `TransformGizmoRenderPacket` span. Interaction-state
  lifetime is per-input-frame (or per drag-interaction span) and lives
  outside graphics; graphics carries the gizmo render packets only
  until the next `BeginFrame()` clears `RenderWorld`, mirroring the
  existing transient debug primitive lifetime from
  `GRAPHICS-002`/`GRAPHICS-010Q`.

- **Transform application, undo policy, and legacy promotion path.**
  Transform application is runtime/editor-owned. At drag-tick time the
  runtime/editor gizmo interaction module computes the next authoring
  transform and writes it into runtime ECS state (or asset / prefab
  override storage); at drag-commit time it pushes a single undoable
  command onto the editor undo stack capturing the
  `(entity, before, after)` tuple. Undo / redo stays in the editor;
  graphics has no role and never mutates ECS, asset, or prefab state.
  Legacy `Graphics.TransformGizmo` and `Graphics.Interaction` features
  (gizmo orientation modes — local / world / view; snap modes —
  angle / grid / value; multi-select pivot policy; modifier-key
  behavior; numeric-input commit; per-axis constraint locks) are
  already enumerated by the editor-handoff rows in
  `docs/migration/nonlegacy-parity-matrix.md` that cross-link
  `GRAPHICS-017Q`. Each feature's promoted-implementation path lands
  as a future runtime/editor task tracked under
  `tasks/backlog/runtime/` (or an equivalent editor-bound queue), not
  as a graphics task. Concrete task IDs are deliberately not allocated
  by this clarification because the matrix already cross-links them
  and `GRAPHICS-020` (legacy graphics retirement gates) is the gating
  task that consumes the matrix. Graphics-side commitments stay
  frozen by `GRAPHICS-017`: data-only `CameraViewInput` /
  `CameraViewSnapshot` / `PickPixelRequest` /
  `TransformGizmoRenderPacket` shapes, no input polling, no transform
  mutation, no editor selection mutation. This clarification adds no
  new graphics fields, no new graphics diagnostics, and no graphics
  acceptance criteria beyond the existing `GRAPHICS-017` contract.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/graphics.md` (new "Per `GRAPHICS-017Q`"
  ownership-contract bullet next to the existing
  `Extrinsic.Graphics.CameraSnapshots` bullet),
  `docs/architecture/rendering-three-pass.md` (new
  "### Camera, picking-request, and gizmo runtime contract"
  subsection alongside the existing `GRAPHICS-013AQ`/`013BQ`/`013CQ`/
  `014Q`/`015Q` clarification paragraphs), and
  `src/graphics/renderer/README.md` (matching ownership-contract
  bullet near the `Graphics.CameraSnapshots` description). The
  rendering backlog README entry for `GRAPHICS-017Q` is redirected
  from the `tasks/backlog/rendering/` path to the `tasks/active/`
  path by the promotion commit and will be redirected to
  `tasks/done/` by the retirement commit; the entry's bullet
  description is also expanded by this commit to summarize the five
  resolved decisions. No `docs/migration/nonlegacy-parity-matrix.md`
  rows change: the existing editor-handoff matrix rows already
  cross-link to `GRAPHICS-017Q` for editor interaction producers,
  pick-request scheduling, and camera/gizmo mutation policy.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify runtime/editor integration details that remain after the CPU/null `GRAPHICS-017` camera, pick-ray, and transform-gizmo render snapshot contracts.

## Non-goals
- No C++ behavior changes.
- No editor UI feature expansion.
- No graphics-side input polling, gizmo hit testing, or transform mutation.

## Context
- `GRAPHICS-017` established data-only graphics contracts for `CameraViewInput`, `PickPixelRequest`, `CameraViewSnapshot`, frustum planes, pick rays, and transform-gizmo render packets.
- Runtime/platform/editor layers still need ownership policy for producing these snapshots and applying interaction results.

## Required changes
- [x] Clarify runtime camera controller ownership and how platform input is translated into camera motion without graphics dependencies.
- [x] Clarify input-to-pick-request scheduling and whether pick requests are single-shot, queued, or coalesced per frame.
- [x] Clarify transform-gizmo hit testing ownership, interaction state storage, and transform application/undo policy.
- [x] Clarify how editor/runtime producers convert gizmo interaction state into data-only `TransformGizmoRenderPacket` spans.
- [x] Clarify legacy interaction behavior that must become promoted implementation tasks before legacy retirement.

## Tests
- [x] Documentation/checker only; no C++ tests required unless policy docs introduce checked manifests.

## Docs
- [x] Update runtime, platform, graphics, and migration docs with selected ownership policies.

## Acceptance criteria
- [x] Future runtime/editor implementation work can produce camera, pick, and gizmo snapshots without changing graphics ownership boundaries.
- [x] Graphics remains free of input polling, live ECS/editor mutation, and transform application.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Moving input polling, gizmo hit testing, or transform mutation into `src/graphics`.

