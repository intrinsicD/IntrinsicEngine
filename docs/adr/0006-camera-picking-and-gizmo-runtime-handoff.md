# ADR 0006 — Camera, Picking-Request, and Gizmo Runtime Handoff

- **Status:** Accepted; amended 2026-10-06/07 by `UI-078` slices 1–3 (see the amendments below; slice 3 supersedes §§3–4 for the Sandbox editor).
- **Date:** 2026-05-17
- **Owners:** Runtime composition, Graphics (CameraSnapshots boundary)
- **Related tasks:** [`tasks/done/GRAPHICS-017`](../../tasks/archive/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md), [`GRAPHICS-017Q`](../../tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md), [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.CameraSnapshots` "Per `GRAPHICS-017Q`" follow-up paragraph in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0007](0007-picking-selection-and-outline.md) owns the `Extrinsic.Graphics.SelectionSystem` reporting seam this handoff produces selection inputs for.

## Context

`GRAPHICS-017` established the data-only graphics contracts for `CameraViewInput`, `PickPixelRequest`, `CameraViewSnapshot`, frustum planes, pick rays, and `TransformGizmoRenderPacket`. Those contracts deliberately leave the *producer* side open: graphics validates and consumes data snapshots but says nothing about who produces them or how editor/runtime interaction state flows into them.

The handoff question matters because three different domains touch this seam:

1. **Runtime** owns camera controllers, input translation, ECS/asset state, and undo.
2. **Editor/app** owns interaction policy: which controller is active, multi-select pivot, snap mode, orientation reference frame, modifier-key behavior, etc.
3. **Graphics** must stay free of input polling, gizmo hit testing, transform mutation, and editor state — `src/graphics/*` must not import `src/platform/`, must not touch live ECS, and must not own controller fan-out.

`GRAPHICS-017Q` answered the producer-side questions for camera-controller ownership, pick-request scheduling/coalescing, gizmo hit testing, interaction-state storage and lifetime, and transform-application/undo policy. This ADR captures those decisions as the canonical durable home. The handoff matrix that enumerates legacy `Graphics.TransformGizmo` / `Graphics.Interaction` features awaiting promoted-implementation tasks lives in [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md); this ADR cross-links it rather than duplicating the matrix.

`docs/architecture/graphics.md` keeps the canonical CameraSnapshots ownership bullet (validates matrices, extracts frustum planes, derives pick rays, runtime/platform own motion / input / hit-test / transform application) and retains a single pointer line to this ADR for the runtime handoff details.

## Decision

### 1. Runtime camera-controller ownership

Concrete camera controllers (orbit, fly, free-look, top-down) live as runtime modules under `Extrinsic.Runtime.CameraControllers`. The naming mirrors the runtime-adapter pattern already used by:

- `Extrinsic.Runtime.VisualizationRecipes` (`GRAPHICS-014Q`).
- `Extrinsic.Runtime.AssetBridges.Texture` (`GRAPHICS-015Q`).

Each frame:

1. The active controller reads platform input deltas through the existing platform input port and translates them into camera-state mutations (target, distance, yaw/pitch/roll, position, look-at) on runtime-owned camera state.
2. Runtime extraction reads the resulting view/projection from the active controller and fills `CameraViewInput` (eye, look-at/forward, up, fov, near/far, viewport pixel width/height, view/projection matrices).
3. Runtime submits the input through `IRenderer::SubmitRuntimeSnapshots()`.
4. Graphics validates the input through `Extrinsic.Graphics.CameraSnapshots` to produce the immutable `CameraViewSnapshot` (with extracted frustum planes) consumed by passes.

Graphics never imports `src/platform/`, never polls window events, and never owns active-controller selection or camera fan-out across multiple views. Editor/app code may surface controller-selection UI, but it funnels the choice through the runtime adapter as a pre-extraction input rather than calling graphics-side camera builders directly.

Multiple cameras (preview, top-down, editor secondary view) are runtime-owned: each emits its own `CameraViewInput` per frame; graphics consumes the resulting snapshot spans without owning controller fan-out logic.

Reimplementing the legacy `Graphics::Camera` motion helpers under `Extrinsic.Runtime.CameraControllers` is a future runtime-task follow-up tracked through the existing editor-handoff rows in [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md) that already cross-link `GRAPHICS-017Q`. Concrete task IDs are deliberately not allocated here because `GRAPHICS-020` (legacy graphics retirement gates) is the gating task that consumes the matrix.

### 2. Pick-request scheduling and coalescing

Runtime extraction owns input-to-`PickPixelRequest` translation. Each input frame's accepted picks — mouse-down, hover-pick, programmatic editor pick, async asset-pipeline pick — are collected into a per-frame queue on the runtime side. Runtime emits the queue as the immutable `PickPixelRequest` span on `RenderFrameInput` once per frame alongside `CameraViewInput` through `IRenderer::SubmitRuntimeSnapshots()`.

The span is **single-shot**: each entry maps to exactly one drained readback.

Coalescing is runtime-owned. When an editor produces two pick requests for the same `(viewport, pixel, request_kind)` key in the same input tick, runtime keeps only the latest one before submitting the snapshot — matching the "graphics never validates duplicates; runtime validates at extraction" stance from `GRAPHICS-014Q`.

The renderer-side drain mirrors `Picking.Readback` from `GRAPHICS-012Q`:

- The renderer copies requested pixels into the graphics-owned host-visible `Picking.Readback` buffer at frame-record time.
- The renderer drains them on the next `BeginFrame()` after the issuing frame's fences complete.
- Valid samples invoke `SelectionSystem::PublishPickResult(...)`.
- `EntityId == 0`, invalidated requests, and deterministic readback failures invoke `SelectionSystem::PublishNoHit()`.

There is **no** graphics-side persistent pending-pick queue across frames and **no** graphics-side request-kind taxonomy. Programmatic picks, mouse picks, and async picks share the same single-shot path; graphics does not distinguish kinds. Editor policy may attach a runtime-side request-kind tag (mouse vs. async vs. gizmo handle pick) inside its own queue, but that tag is consumed by the runtime selection-resolution sidecar and never crosses into `RenderFrameInput`.

### 3. Transform-gizmo hit testing ownership

> Superseded for the Sandbox editor by the slice 3 amendment: the app-private
> ImGuizmo frontend hit-tests and draws the gizmo; runtime keeps the session.
> The packet contract below stays for other producers.

Hit testing lives in the concrete runtime/editor-owned `Extrinsic.Runtime.GizmoInteraction` module, mirroring the same adapter pattern as the camera-controller umbrella.

The hit-test path reads:

- The active selection's authoring transforms from runtime ECS / editor state.
- The same `CameraViewSnapshot::ViewProjection` / `PickRay` derivation that graphics already produces from `CameraViewInput`.
- Raw pointer pixel coordinates from the platform input port.

Runtime computes axis / plane intersection in world or view space and produces the resulting interaction state. Graphics never receives raw pointer coordinates and never imports any gizmo hit-test code path.

The `TransformGizmoRenderPacket` spans on `RenderWorld` carry **only** render-relevant data:

- World-space gizmo origin.
- Gizmo scale (camera-relative for constant pixel size).
- Active gizmo mode (translate / rotate / scale).
- Highlighted axis or plane mask.
- Per-handle render flags.

Active drag state, axis lock, screen-space drag origin, snap thresholds, modifier-key state, multi-select pivot policy (centroid / individual / last-selected), and orientation reference frame (local / world / view) all stay runtime-side and never enter graphics.

**Reusing `PickPixelRequest` for gizmo handles is permitted but not required.** Editor policy may either:

- Use direct CPU ray-vs-gizmo intersection in runtime/editor — **preferred** for the dominant gizmo case because it avoids a frame of pick-readback latency; or
- Route handle picks through `PickPixelRequest` with a runtime-side request-kind tag that selects gizmo handles rather than scene entities.

Either choice is invisible to graphics.

### 4. Interaction-state storage and lifetime

> Superseded for the Sandbox editor by the slice 3 amendment: the frontend
> state (mode choice, pointer state, snap steps) lives in the Sandbox shell,
> the frozen session in `GizmoInteraction`.

Interaction state is runtime/editor-owned. Implementations may store it as either:

- An editor-side singleton (when gizmo interaction is editor-only); or
- An ECS component (when gizmo interaction is part of an in-game tool).

Graphics has no opinion on the storage choice and never imports either.

Each frame's gizmo interaction state has at minimum:

- Active mode (translate / rotate / scale, or none if idle).
- Affected axis or plane mask.
- Accumulated delta in the gizmo's reference frame.
- Drag origin in pointer pixels.
- Snap mode and snap step.
- Multi-select pivot policy (centroid / individual / last-selected).
- Orientation reference frame (local / world / view).

None of these fields enter graphics: the renderer sees only the resulting `TransformGizmoRenderPacket` span. Interaction-state lifetime is per-input-frame (or per drag-interaction span) and lives outside graphics; graphics carries the gizmo render packets only until the next `BeginFrame()` clears `RenderWorld`, mirroring the existing transient-debug-primitive lifetime from `GRAPHICS-002` / `GRAPHICS-010Q`.

### 5. Transform application and undo policy

Transform application is runtime/editor-owned:

- **At drag-tick time** the runtime/editor gizmo interaction module computes the next authoring transform and writes it into runtime ECS state (or asset / prefab override storage).
- **At drag-commit time** it pushes a single undoable command onto the editor undo stack capturing the `(entity, before, after)` tuple.

Undo / redo stays in the editor; graphics has no role and never mutates ECS, asset, or prefab state.

### 6. Legacy promotion path

Legacy `Graphics.TransformGizmo` and `Graphics.Interaction` features awaiting promoted-implementation tasks include:

- Gizmo orientation modes (local / world / view).
- Snap modes (angle / grid / value).
- Multi-select pivot policy.
- Modifier-key behavior.
- Numeric-input commit.
- Per-axis constraint locks.

These are already enumerated by the editor-handoff rows in [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md) that cross-link `GRAPHICS-017Q`. Each feature's promoted-implementation path lands as a future runtime/editor task tracked under `tasks/backlog/runtime/` (or an equivalent editor-bound queue), **not** as a graphics task. Concrete task IDs are deliberately not allocated by this ADR because the matrix already cross-links them and `GRAPHICS-020` (legacy graphics retirement gates) is the gating task that consumes the matrix.

Graphics-side commitments stay frozen by `GRAPHICS-017`: data-only `CameraViewInput` / `CameraViewSnapshot` / `PickPixelRequest` / `TransformGizmoRenderPacket` shapes, no input polling, no transform mutation, no editor selection mutation. This handoff adds no new graphics fields, no new graphics diagnostics, and no graphics acceptance criteria beyond the existing `GRAPHICS-017` contract.

## Consequences

Positive:

- Graphics stays clean of input polling, ECS mutation, transform application, and editor state — preserving the layering invariant from `AGENTS.md` §2.
- Camera controllers, gizmo interaction, undo, and snap policy can iterate freely in runtime/editor without changing the graphics surface, because graphics validates only the data-only `CameraViewInput` / `PickPixelRequest` / `TransformGizmoRenderPacket` shapes.
- Pick-request handling is single-shot and runtime-coalesced, so graphics never grows a persistent pending-pick queue or a request-kind taxonomy.
- Multiple cameras (preview, top-down, editor secondary view) are a runtime concern; graphics consumes the resulting snapshot spans without owning fan-out logic.
- The legacy promotion path is tracked exactly once — in the parity matrix — so this ADR does not duplicate it and cannot drift from it.

Trade-offs and risks:

- Editor policy has two valid options for gizmo handle picking (direct CPU ray-vs-gizmo, or routed through `PickPixelRequest`). The ADR records the preference but does not force the choice, so two editors could legitimately implement different behaviors. This is intentional but means reviewers must check the editor's choice against latency expectations.
- The runtime-coalescing rule for pick requests means a programmatic editor that fires picks faster than the input tick will silently drop earlier picks. This matches `GRAPHICS-014Q`'s "runtime validates at extraction" stance and is documented in `GRAPHICS-017Q`, but is worth re-stating here so consumers know graphics is not the deduplication seam.
- The runtime umbrella module names recorded by this ADR have since landed as `Extrinsic.Runtime.CameraControllers` and `Extrinsic.Runtime.GizmoInteraction`; future editor/runtime follow-ups must extend those modules or explicitly amend this ADR.

Follow-up tasks required: none from this ADR. The matrix-tracked legacy promotion tasks are gated by `GRAPHICS-020` and do not become urgent because of this extraction.

## Alternatives Considered

- **Graphics owns camera controllers.** Rejected per §1: would force `src/graphics/*` to import `src/platform/` for input polling, breaking `AGENTS.md` §2.
- **Persistent graphics-side pending-pick queue and request-kind taxonomy.** Rejected per §2: forces graphics to track request semantics (mouse vs. async vs. gizmo handle) and to retry across frames. Runtime already owns the queue and can coalesce by `(viewport, pixel, request_kind)`; graphics stays single-shot.
- **Gizmo hit testing inside graphics.** Rejected per §3: requires graphics to import raw pointer coordinates and to read live ECS / editor state, which `GRAPHICS-017` explicitly forbids.
- **`TransformGizmoRenderPacket` carries interaction state (drag origin, snap thresholds, modifier-key state).** Rejected per §§3–4: those fields are editor policy, not render data, and would couple graphics to editor iteration.
- **Graphics-owned undo of transform edits.** Rejected per §5: undo / redo is editor policy and must not be smeared across the layer boundary.
- **Allocating concrete promoted-implementation task IDs in this ADR for every legacy `Graphics.TransformGizmo` feature.** Rejected per §6: the parity matrix already tracks them and `GRAPHICS-020` is the gating task that consumes the matrix; duplicating the list here would create two sources of truth that drift.

## Validation

- [`tasks/done/GRAPHICS-017`](../../tasks/archive/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md) records the underlying data-only graphics contracts (`CameraViewInput`, `PickPixelRequest`, `CameraViewSnapshot`, frustum planes, pick rays, `TransformGizmoRenderPacket`).
- [`tasks/done/GRAPHICS-017Q`](../../tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md) records the five clarification decisions captured in §§1–5 plus the legacy-promotion-path note in §6.
- [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md) is the single source of truth for the legacy `Graphics.TransformGizmo` / `Graphics.Interaction` feature handoff inventory; this ADR cross-links it instead of duplicating the matrix.
- `src/graphics/renderer/README.md` carries the matching `Graphics.CameraSnapshots` ownership-contract bullet authored by `GRAPHICS-017Q`.
- Layering invariant validation: `python3 tools/repo/check_layering.py --root src --strict` continues to pass because graphics does not import `src/platform/`, ECS, or editor code; only data-only `CameraViewInput` / `PickPixelRequest` / `TransformGizmoRenderPacket` cross the seam.

## Amendment — 2026-10-06 (`UI-078` slice 1)

§§4–5 are made concrete for multi-selection by a matrix drag session in
`Extrinsic.Runtime.GizmoInteraction`; the graphics packet contract is
unchanged.

- **Frozen session.** `Begin` deduplicates and sorts the selection and freezes
  it with mode, orientation, pivot policy and the start state; it writes no ECS
  data, and a second session is refused. Start world matrices are composed from
  local TRS along the parent chain, not read from the `WorldMatrix` cache.
- **Pivot and basis.** Pivot is the mean of selected world origins (default) or
  of per-entity world-bounds centers with origin fallback. The basis is world,
  a single entity's world rotation, or for groups the chordal mean of the
  selected world rotations; an unavailable mean falls back to world with an
  explicit reason.
- **Group delta.** Each preview computes `D = Gt·G0⁻¹`, `Wi' = D·Wi0`,
  `Li' = Wparent⁻¹·Wi'` from the start state and writes only selected entities
  without a selected ancestor, so selected descendants move once.
- **Atomic rejection.** A preview whose local result is not storable as TRS
  (shear, perspective, non-finite, singular parent) writes nothing for any
  entity; the last accepted preview stays and the reason is returned.
- **Session check.** Preview, commit and cancel first run one write-free check:
  same registry, every write target still holds its last accepted TRS under an
  unchanged parent world matrix, and every selected entity and its ancestors
  (including selected descendants and intermediate nodes that are not write
  targets) is alive with an unchanged parent link and, for non-targets,
  unchanged local TRS. `Gt` itself must be affine. A foreign registry is
  refused without writes and the session stays.
- **Undo.** Commit records one before/after batch through the generation-
  validated history transaction (§5's single command). A no-op drag (equal
  local 3x3, so equivalent TRS such as mirrors count as unchanged, and
  translation equal within a few ulps) and
  cancel record nothing and restore the exact original TRS. A conflicting or
  unrecorded commit records nothing and rolls back every target that still
  holds its accepted preview; a foreign change to a target is kept.
- **Packets.** (Superseded by slice 3: runtime publishes no gizmo packets.)
  A multi-selection published one gizmo on the shared session frame.

## Amendment — 2026-10-07 (`UI-078` slice 2)

Viewport input ownership and the lifecycle cancel of a running session.

- **Ownership request.** A frame contribution claims the viewport through
  `EditorUiHost::RequestViewportInput`; contributions OR their requests into
  one per-frame claim. `EditorUiModule` resets it at the start of `UiBegin`,
  a hide or `SetOperational(false)` drops it, and a hidden or non-operational
  host ignores it. After the adapter has written the frame's capture snapshot,
  `UiEndCapture` merges the claim as mouse and keyboard capture, and
  `EditorUiDiagnostics::CapturesViewportInput` publishes the merged value. The
  existing consumers therefore block: camera controller updates, new viewport
  pick requests, and keyboard camera actions such as `F`.
- **Capture is not cancel.** A claim never cancels or commits the session the
  claiming frontend drives. (Slice 3 removed the ray-driven adapter this
  originally skipped while claimed.)
- **Cancel triggers.** `DragCancel` on the bound registry ends a running
  session without history; selection, mode, orientation, pivot and tuning
  stay. Triggers: the UI turning hidden (seen in `UiBegin`, again before the
  viewport driver, and on minimized frames), native window focus loss
  (`Platform::WindowFocusEvent`, republished on the kernel event bus and
  handled at delivery, so a loss and regain in one batch still cancels; an
  `Idle` hook keeps minimized frames pumping), and the existing world or
  document change reset. Every trigger fires before the single pre-render
  transform flush, so the restored transform reaches the same frame's
  extraction. The global `G` toggle therefore runs at `UiBegin`, independent of
  ImGui keyboard capture, instead of as a post-flush input action.

## Amendment — 2026-10-07 (`UI-078` slice 3)

The Sandbox editor's gizmo frontend is ImGuizmo, private to
`src/app/Sandbox` (`EditorShell`); runtime no longer hit-tests, drives or
draws the gizmo.

- **Removed.** `GizmoInteraction`'s ray adapter (`HitTest`, `BeginDrag`,
  `DragTick`, axis lock, modifier mask, ray tuning `GizmoConfig`),
  `TransformGizmoRenderPacketBuilder`, and `SceneInteractionModule`'s
  automatic mouse driver and gizmo packet production. The general
  `RuntimeSceneInteractionRenderSnapshot::GizmoDrawPackets` → `RenderWorld`
  packet contract stays for other producers; the Sandbox publishes none.
- **Frontend.** The shell reads `SceneInteractionModule::PrepareGizmo` and
  drives the token-checked `BeginGizmoDrag`/`PreviewGizmoDrag`/
  `CommitGizmoDrag`/`CancelGizmoDrag` from `ImGuizmo::IsUsing()` transitions:
  begin when it turns true, an absolute `Gt` preview every frame, exactly one
  commit on release. Escape, a disabled gizmo, or a session that ended
  elsewhere (hide, focus loss, world/document change: the token no longer
  matches) never commits; ImGuizmo is reset and no drag restarts until the
  mouse is released. Hover, drag, the release/cancel frame and a consumed
  W/E/R claim the viewport (slice 2). Snap steps come from the `sandbox.gizmo`
  config section (Shift snaps), frozen per drag; a snapped rotation is
  previewed as the exact snapped angle about the frozen pivot, since
  ImGuizmo's own snap drifts by up to ~5e-4 rad. A left-button release ends
  the drag even where ImGuizmo misses it (over a panel).
