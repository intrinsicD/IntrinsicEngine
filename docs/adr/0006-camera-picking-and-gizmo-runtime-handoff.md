# ADR 0006 — Camera, Picking-Request, and Gizmo Runtime Handoff

- **Status:** Accepted; §§3–5 rewritten 2026-10-07 for the `UI-078` gizmo (see [Change history](#change-history)).
- **Date:** 2026-05-17
- **Owners:** Runtime composition, Graphics (CameraSnapshots boundary)
- **Related tasks:** [`tasks/done/GRAPHICS-017`](../../tasks/archive/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md), [`GRAPHICS-017Q`](../../tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md), `UI-078`
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
2. Runtime extraction reads the resulting view/projection from the active controller and fills `CameraViewInput` (eye, look-at/forward, up, fov, near/far, viewport pixel width/height, view/projection matrices). Projections follow the [clip-space convention](../architecture/graphics.md#gpu-scene-ownership) (right-handed, depth [0, 1]).
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

### 3. Gizmo frontend: hit testing and drawing

The editor frontend hit-tests and draws the gizmo; runtime never sees pointer
coordinates for it. In the Sandbox this is ImGuizmo, private to
`src/app/Sandbox` (`EditorShell`, `imguizmo_lib` linked PRIVATE to
`ExtrinsicSandboxEditor`). Each `UiBuild` the shell reads
`SceneInteractionModule::PrepareGizmo(orientation, pivot)`, a copied
`GizmoUiFrame`:

- an availability reason (`NoBinding`, `NoHistory`, `NoEntitySelection`,
  `InvalidFrame`, `NoCamera`);
- the gizmo matrix: `ComputeFrame` before a drag, the frozen session frame,
  mode and last accepted `Gt` during one;
- the Main controller's unjittered view/projection read with `GetView` and
  without `Update`, i.e. its state before this frame's camera update; a claimed
  frame (§5) does not update the controller, so it equals the rendered camera;
- the scene rectangle: the current `SceneViewport()` claim or the full client
  area, resolved in framebuffer pixels like the engine and mapped back to
  logical window (ImGui) coordinates; the shell adds only the display origin
  (no second scale, no Y flip);
- a `{world, epoch, session}` token.

The shell drives `BeginGizmoDrag`/`PreviewGizmoDrag(Gt)`/`CommitGizmoDrag`/
`CancelGizmoDrag` from `ImGuizmo::IsUsing()` transitions: begin when it turns
true (right after Prepare, since Begin recomputes `G0`), an absolute `Gt`
preview every frame, exactly one commit on release, also for a left-button
release ImGuizmo misses (over a panel). ImGuizmo's working matrix stays
separate from the accepted one. Escape, disabling the gizmo, or a session that
ended elsewhere never commits; ImGuizmo is reset and no drag restarts until the
mouse is released. Mode (W/E/R, only when enabled and not typing), pivot,
orientation and snap steps are frontend state, frozen into the next drag.

Snap is held Shift. The steps are the `sandbox.gizmo` config section owned by
`SceneInteractionModule` (translate 0.25, rotate 15°, scale 0.1; positive
normal floats, rotation at least 0.001°), edited through the config lane's
preview/validate/apply (`PreviewGizmoSnapConfig`/`ApplyGizmoSnapConfig`).
ImGuizmo snaps `Gt`; a snapped rotation is replaced by the exact snapped angle
about the frozen pivot (`SnapGizmoRotation`), because ImGuizmo's own snap
drifts by up to ~5e-4 rad and would be rejected as shear under a non-uniformly
scaled parent.

The Sandbox publishes no `TransformGizmoRenderPacket`. The generic packet
contract (`RuntimeSceneInteractionRenderSnapshot::GizmoDrawPackets` →
`RenderWorld`; origin, scale, mode, highlight mask, handle flags; no
interaction state) stays for other producers.

Known limitation: in an exactly top-down orthographic view ImGuizmo cannot pick
the X/Z scale handles, because the pick ray is parallel to the plane it
intersects for those axes. Accepted; tracked by
[`BUG-236`](../../tasks/backlog/bugs/BUG-236-topdown-ortho-gizmo-scale-handles.md).

### 4. Runtime matrix session

`Extrinsic.Runtime.GizmoInteraction`, owned by `SceneInteractionModule`, holds
one session:

- **Frozen start.** `Begin` deduplicates and sorts the selection and freezes it
  with mode, orientation, pivot policy and the start state; it writes nothing,
  and a second session is refused. Start world matrices `Wi0` are composed from
  local TRS along the parent chain, not read from the `WorldMatrix` cache.
- **Pivot and basis.** Pivot is the mean of selected world origins (default) or
  of world-bounds centers with origin fallback. The basis is world, a single
  entity's world rotation, or for groups the chordal mean of the selected world
  rotations (`Geometry::Rotation::ChordalMean`); an unavailable mean falls back
  to world with an explicit reason.
- **Group delta.** Each preview computes `D = Gt·G0⁻¹`, `Wi' = D·Wi0`,
  `Li' = Wparent⁻¹·Wi'` from the start state and writes only selected entities
  without a selected ancestor, so selected descendants move once and unselected
  children follow their parent.
- **Atomic rejection.** A result not storable as TRS (shear, perspective,
  non-finite, singular parent) writes nothing for any entity; the last accepted
  preview stays and the reason is returned for the UI.
- **Session check.** Preview, commit and cancel first run one write-free check:
  same registry; every write target still holds its last accepted TRS under an
  unchanged parent world matrix; every selected entity and its ancestors is
  alive with an unchanged parent link and, for non-targets, unchanged local
  TRS; `Gt` is affine. A foreign registry is refused without writes and the
  session stays.
- **Tokens.** The `SceneInteractionModule` calls resolve selection, registry and
  history internally. A stale token (world switch, document replacement, any
  session start or end including the lifecycle cancels of §5) writes and starts
  nothing; `Begin` accepts only the current idle token.
  `GizmoInteraction::SessionGeneration()` advances on every start and end, and
  the interaction epoch survives `Shutdown`/`Initialize`.

### 5. Viewport ownership, flush and undo

- **Capture.** While the gizmo is hovered or dragged, on the release/cancel
  frame and while a consumed W/E/R is held, the frontend claims the viewport
  through `EditorUiHost::RequestViewportInput` (OR of contributions, reset in
  `UiBegin`, dropped on hide/non-operation, ignored while hidden).
  `EditorUiModule` merges it as mouse and keyboard capture after the adapter's
  capture snapshot, so camera updates, viewport picks and keyboard camera
  actions such as `F` are blocked. A claim never cancels or commits the
  claiming frontend's session.
- **Cancel.** `DragCancel` ends a running session without history and restores
  the exact start TRS when the UI turns hidden (`G` toggles at `UiBegin`,
  independent of ImGui keyboard capture), on native window focus loss
  (`Platform::WindowFocusEvent` from the GLFW focus callback or Null
  `QueueEvent`, republished on the kernel bus and handled at delivery; an
  `Idle` hook keeps minimized frames pumping), and on world or document change.
- **Flush.** Previews (`UiBuild`) and every cancel run before the single
  pre-render transform flush, so the changed or restored transform reaches the
  same frame's extraction.
- **Undo.** Release records one before/after batch through the
  generation-validated `EditorCommandHistory` transaction; without that service
  dragging is unavailable. A no-op (equal local 3x3, so mirrors count as
  unchanged, and translation within a few ulps) and a cancel record nothing.
  Release after a rejected preview commits the last accepted state; Escape
  discards. A conflicting or unrecorded commit records nothing and rolls back
  every target still holding its accepted preview; a foreign change is kept.
  Undo/redo stays in the editor; graphics never mutates ECS, asset or prefab
  state.

### 6. Legacy promotion path

> Historical (2026-05-17). Orientation (local/world), snap, multi-select pivot
> and modifier behavior have since landed in §§3–5; view orientation,
> numeric-input commit and per-axis locks were not carried over.

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

- Gizmo handle picking is frontend behavior: the Sandbox inherits ImGuizmo's screen-space hit test, including its top-down orthographic scale-handle gap (§3).
- The runtime-coalescing rule for pick requests means a programmatic editor that fires picks faster than the input tick will silently drop earlier picks. This matches `GRAPHICS-014Q`'s "runtime validates at extraction" stance and is documented in `GRAPHICS-017Q`, but is worth re-stating here so consumers know graphics is not the deduplication seam.
- The runtime umbrella module names recorded by this ADR have since landed as `Extrinsic.Runtime.CameraControllers` and `Extrinsic.Runtime.GizmoInteraction`; future editor/runtime follow-ups must extend those modules or explicitly amend this ADR.

Follow-up tasks required: none from this ADR. The matrix-tracked legacy promotion tasks are gated by `GRAPHICS-020` and do not become urgent because of this extraction.

## Alternatives Considered

- **Graphics owns camera controllers.** Rejected per §1: would force `src/graphics/*` to import `src/platform/` for input polling, breaking `AGENTS.md` §2.
- **Persistent graphics-side pending-pick queue and request-kind taxonomy.** Rejected per §2: forces graphics to track request semantics (mouse vs. async vs. gizmo handle) and to retry across frames. Runtime already owns the queue and can coalesce by `(viewport, pixel, request_kind)`; graphics stays single-shot.
- **Gizmo hit testing inside graphics.** Rejected per §3: requires graphics to import raw pointer coordinates and to read live ECS / editor state, which `GRAPHICS-017` explicitly forbids.
- **`TransformGizmoRenderPacket` carries interaction state (drag origin, snap thresholds, modifier-key state).** Rejected per §§3–4: those fields are editor policy, not render data, and would couple graphics to editor iteration.
- **Keep the runtime ray adapter (`HitTest`/`BeginDrag`/`DragTick`) and `TransformGizmoRenderPacketBuilder` as the Sandbox frontend.** Replaced by `UI-078`: ImGuizmo already provides handles, drawing and snapping, while runtime keeps the session, validation and history, which other frontends can reuse.
- **Graphics-owned undo of transform edits.** Rejected per §5: undo / redo is editor policy and must not be smeared across the layer boundary.
- **Allocating concrete promoted-implementation task IDs in this ADR for every legacy `Graphics.TransformGizmo` feature.** Rejected per §6: the parity matrix already tracks them and `GRAPHICS-020` is the gating task that consumes the matrix; duplicating the list here would create two sources of truth that drift.

## Validation

- [`tasks/done/GRAPHICS-017`](../../tasks/archive/GRAPHICS-017-camera-interaction-and-gizmo-boundaries.md) records the underlying data-only graphics contracts (`CameraViewInput`, `PickPixelRequest`, `CameraViewSnapshot`, frustum planes, pick rays, `TransformGizmoRenderPacket`); [`tasks/done/GRAPHICS-017Q`](../../tasks/archive/GRAPHICS-017Q-camera-gizmo-runtime-clarifications.md) the original §§1–6 decisions.
- [`docs/migration/nonlegacy-parity-matrix.md`](../migration/nonlegacy-parity-matrix.md) is the single source of truth for the legacy `Graphics.TransformGizmo` / `Graphics.Interaction` feature handoff inventory.
- Layering: `python3 tools/repo/check_layering.py --root src --strict` passes; ImGuizmo types appear only in `src/app/Sandbox`.
- **CPU contract** (§§4–5): `GizmoInteraction.*`, `GizmoInteractionEngineWiring.*`, `SceneInteractionModule.*` (including `GizmoUiSceneRectIsTheCurrentClaimMappedBackFromFramebufferPixels`, the only evidence for a framebuffer/window ratio other than 1), `EditorUiHost.*`, `EditorUiModule.*`, `ImGuiAdapterEngineWiring.*`, `NullPlatform.DeliversWindowFocusEventsInOrder`, `SandboxConfigSections.GizmoSnapStepsRegisterValidateAndApplyThroughTheConfigLane`, `RuntimeEngineLayering.*`, `RuntimeEnginePrivateGlue.*`.
- **Shell** (§3, Null window events through the production shell): `SandboxEditorGizmo.*` in `tests/integration/runtime/Test.SandboxEditorPresentation.cpp`.
- **Vulkan**: `RuntimeSandboxAcceptanceGpuSmoke.{ImGuizmoGroupDragAndUndoReachSameFramePixels, ImGuizmoOrthographicSplitViewportDragAndUndo}` in `tests/integration/runtime/Test.RuntimeSandboxAcceptanceGpuSmoke.cpp` drive the production Sandbox through the GLFW callbacks the window registered (no OS-generated input): a menu click enables the gizmo, a claimed group drag moves both triangles in the first preview frame's own readback (fails without the pre-render flush), and a click on Undo restores transforms and pixels. Executed on an NVIDIA RTX 3050 (driver 590.48.01, X11, pixel ratio 1) on the `95d093bb1` tree plus the then-uncommitted slice-4a diff, whose smoke code was committed unchanged as `49a609334`; claim C118 in `ara/logic/claims.md`. The orthographic case needs the [clip-space convention](../architecture/graphics.md#gpu-scene-ownership) fixed by `BUG-235` (`95d093bb1`). A ratio other than 1 is not yet run ([`UI-079`](../../tasks/backlog/ui/UI-079-imguizmo-hidpi-operational-run.md)).
- **Native focus**: `GlfwPlatformSmoke.NativeFocusChangesEmitWindowFocusEvents` in `tests/integration/platform/Test.GlfwPlatformSmoke.cpp` requests real focus changes and checks the production callback's events; it passed on an unlocked GNOME/X11 session and skips where the window manager refuses programmatic focus.

## Change history

- 2026-05-17: accepted (`GRAPHICS-017Q`).
- 2026-10-06, `UI-078` slice 1 (`5ba6f9b97`): matrix session with frozen group pivot/basis, atomic rejection and one undo batch (§§4–5).
- 2026-10-07, slice 2 (`6441f256e`): viewport claim and lifecycle cancel (§5).
- 2026-10-07, slice 3 (`a6fc6f4fc`, `0ae5e4538`, `b513f2787`): ImGuizmo frontend, snap config, tokens (§§3–4). Superseded: runtime gizmo hit testing and its ray adapter, the CPU ray-vs-gizmo or `PickPixelRequest` handle-pick options, the per-frame interaction-state field list (axis mask, pointer drag origin, view orientation, individual/last-selected pivot) and runtime gizmo packet production.
- 2026-10-07, slice 4 (`49a609334` and the consolidation of the slice amendments into §§3–5): Vulkan and native-focus validation.
