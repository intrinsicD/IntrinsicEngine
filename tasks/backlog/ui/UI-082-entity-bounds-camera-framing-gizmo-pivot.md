---
id: UI-082
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
contract_schema: 1
contracts: [runtime.gizmo-transform-session]
---
# UI-082 — Entity bounding boxes, large-model camera framing and bounds-center gizmo pivot

## Goal
- Any entity can show its world bounding box (per entity, all on, all off); the overlay costs
  nothing while disabled.
- Imported models keep their size; the orbit camera frames large or off-origin models, and **F**
  frames the selected entity around its bounds center.
- The gizmo sits at, and scales about, the selection's bounds center by default.

## Operator questions (verbatim, 2026-10-20)
1. "wenn ich lucy.obj lade dann ist das modell sehr gross skaliert"
2. "wenn ich es mit dem guizmo kleiner oder grösser skalieren will dann rotiert die kamera danach nicht mehr um lucy's center of gravity."
3. "das guizmo wird an der seite von lucy gerendert wenn es enabled wird, ich musste es erst suchen. es sollte aber im center of gravity der entity gerendert werden."
4. "zusätzlich sollte auf tasten druck der taste f die kamera zum center of gravity der ausgewählten entity gedreht werden"
5. "es gibt momentan keine möglichkeit das bounding volumen von entities rendern zu lassen (ui controlle). das sollte effizient passieren und nur wenn es enabled ist (pro entity individuell, für alle an, für alle aus) und ordentlich in der ui integriert sein"
6. "das soll natürlich für alle anderen entities auch funktionieren und nicht nur für lucy"

## Decisions (implementing session, 2026-10-20)
- "Center of gravity" is the center of the displayed local bounds: the bound Position source when
  one is displayed, else the canonical local bounds. Not a mass or area centroid.
- Imports keep their authored size. Instead of rescaling, the orbit range grows to four times the
  framed distance and the far plane follows the orbit radius; portrait viewports widen the vertical
  FOV so the focus sphere fits the shorter side.
- `Graphics::Components::RenderBounds` is a scene-owned tag beside `RenderSurface/Edges/Points`:
  toggled through `EditorRenderHintCommand` (undoable), saved as `render.bounds`, copied on clone,
  and set by agents with `set_visibility` `lane: "bounds"`.
- Render extraction emits 12 debug lines per enabled entity from its current world bounds after the
  transform flush; no packets without enabled entities and no line budget cap.
- The gizmo pivot defaults to `BoundsCenters`; each selected entity's local center is cached by
  position owner, data pointer, property revision and name, so an idle gizmo does not rescan
  vertices every frame.

## Acceptance criteria
- [x] Inspector **Bounding box** checkbox and **View → Bounding boxes → All on / All off**; undo and
      redo; saved, loaded and cloned with the scene; agent `set_visibility` with `lane: "bounds"`.
- [x] Disabled entities emit no box packets; enabled boxes follow current world bounds.
- [x] Orbit focus frames large off-origin geometry, also in portrait viewports; zoom stays
      continuous; poses beyond the former 500-unit orbit limit are kept, not clamped.
- [x] **F** frames the selected entity by the bounds of its displayed positions.
- [x] The default gizmo pivot is the displayed bounds center and updates when those positions change.
- [x] CPU contract and integration tests pass on `main` at `aa70ecb28` with these changes (496/496,
      2026-10-23).
- [ ] Live Sandbox check on Vulkan: boxes visible and following a gizmo drag, **F** framing
      `lucy.obj`, the gizmo at the model's center.

## Maturity
- Target: `Operational` on Vulkan-capable hosts. The CPU slice closes `CPUContracted`;
  `Operational` owned by `UI-082` (the open live-check criterion).

## Verification
```bash
cmake --build --preset ci -j 6 --target IntrinsicRuntimeContractTests IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 -R '^(RuntimeCameraControllers|RuntimeCameraFocusCommand|GizmoInteraction|SandboxEditorGizmo|SandboxEditorUi|SandboxEditorPresentation|RuntimeRenderExtraction|RuntimeSceneSerialization|SceneDocumentModule|VertexChannelBindings|SandboxAgentServer|LocalSocket)\.'
```

## Log
- 2026-10-20: implemented in a Codex Desktop session; its independent code review approved after
  one revision (far plane for large orbit distances, cached gizmo pivot). That session reported
  413 focused and 39 socket tests passing and mechanism-removal checks; the work stayed uncommitted.
- 2026-10-23: the focused tests above pass on `main` (496/496). The full CPU suite and the
  sanitizer builds were not rerun. The operator has not yet confirmed the bounds-center reading of
  "center of gravity" or keeping imports at their authored size.
- 2026-10-30: Moved from `tasks/active/` to the backlog by the operator's decision (ARCH-022, question 38): nobody is working on it now. Its stale claim fields were cleared; its scope, state and open points are unchanged.
