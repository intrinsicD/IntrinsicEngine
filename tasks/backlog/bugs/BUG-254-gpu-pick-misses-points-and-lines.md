---
id: BUG-254
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and the Vulkan smoke run
contract_schema: 1
contracts: [geometry.element-domain-sources]
---
# BUG-254 — A click on a point or edge line with no surface behind it misses on Vulkan

## Goal
- On Vulkan, a click on any drawn primitive (surface, edge line, point), including one with
  no surface behind it, hits its entity and resolves to the clicked element in Entity mode and
  in the element modes that apply to it (Vertex for points, Edge for edge lines), so points,
  edges, faces and entities can all be selected; this includes primitives drawn from bound
  positions (UI-083 slice 3a).

## Context
- Found 2026-10-25 while writing the UI-083 slice 3a smoke (host `hochvogel`, NVIDIA GeForce
  RTX 4090, driver 580.159.04, `build/ci-vulkan`): the ReferenceTriangle's points lane drawn at
  positions shifted by +1 in x, Vertex mode, click on the shifted apex point (background
  around it). Selection diagnostics: `clicks=1 drained=1 readbacks=1 hits=0 noHits=1`; no
  refined primitive. The same smoke shows the points drawn at the shifted pixels, so the click
  pixel was on a visible point.
- Code facts (blockers for a point or edge line with no surface behind it):
  - The entity, face, edge and point ID pipelines test `DepthOp::Equal` without depth writes
    (`src/graphics/renderer/Graphics.Renderer.cpp`, the selection pipeline descriptors), against
    the depth prepass, which draws only the `SurfaceOpaque` bucket
    (`src/graphics/renderer/Passes/Pass.DepthPrepass.cpp`). A point or line fragment over
    background therefore fails the depth test, although `selection/point_id.frag` and
    `selection/edge_id.frag` write the entity id as well as the primitive id.
  - `PointIdPass` draws the `SelectionPoints` bucket, which `assets/shaders/instance_cull.comp`
    fills only for instances with `GpuRender_Selectable`; no runtime code sets that flag
    (`BuildRenderFlags` in `Runtime.RenderExtraction.cpp`, the primitive-view flags in
    `Runtime.RenderExtraction.Geometry.cpp`). `EdgeIdPass` draws the ordinary `Lines` bucket.
  - A probe that set `GpuRender_Selectable` on the mesh primitive views still read back no hit
    (reverted, not committed): the flag alone is not enough, consistent with the depth test.
  - Edge or point fragments that lie on a surface can pass the depth-equal test, and a surface
    hit already refines to a vertex or an edge; those clicks are not reported broken.
  - GRAPHICS-158 records that `selection/edge_id.vert` draws native lines instead of the visible
    line quads and `selection/point_id.vert` uses one uniform size instead of per-point sizes.
- No GPU smoke clicks a point or an edge line; `RuntimeSandboxAcceptanceGpuSmoke` covers surface
  face picks only (BUG-026B). Not checked: graph-only and point-cloud-only entities (the same
  cause predicts that their points and lines do not hit; not run).
- The CPU side already maps hits back: refinement measures against the positions of the hit
  lane and reports canonical element ids (UI-083 slice 3a).
- Relation: GRAPHICS-158 slice 4 asks for the same click evidence for rebound positions; this
  bug owns making the click hit. UI-083 then highlights a selection on every drawn lane that
  shows it (operator decision 2026-10-25).

## Operator questions (verbatim, 2026-10-25)
1. "Ja also das Ziel ist es schon alle Punkte, Kanten und Flächen sowie entities auswählen zu
   können. Das ist glaube ich noch nicht komplett implementiert."
2. (Approval of the proposed order) "Ja bitte." — commit the drawing smoke and open this bug,
   then fix picking for every lane, then highlight a selection on every drawn lane (UI-083
   slice 3c).
3. (After plan review round 1) "Ok, setze den Plan so um. Ja die Modus Regel passt"

Goal item served: Framework24 W5 "Inspect rendering and properties" (every primitive
reachable); this is the shortest path because the CPU refinement already maps a hit on any lane
back to canonical ids, so only the GPU ID passes and the selectable flag stand in the way.

## Slice plan
- **Slice A — the nearest drawn primitive of the selection mode is picked (~400 lines with
  tests and smokes).** Serves question 1; revised after plan review round 1 (overlap order,
  selection-mode precedence, hit depth, existing tests).
  1. Pick-private depth. While a primitive pick is pending, the `PickingPass` renders into its
     own D32 target (`Picking.Depth`, cleared to 1) instead of loading `SceneDepth`: the
     pick-variant entity ID pipeline draws surfaces with `LessEqual` and depth writes, face ID
     keeps `Equal` against that depth (invariant positions as today), edge and point ID use
     `LessEqual` with depth writes. Of primitives at different depths the nearest wins every
     pixel; at equal depth the sub-pass order face, edge, point decides, as today. `SceneDepth` is
     never written, so the later surface pass is untouched. `Picking.Depth` is a transient
     created like `PrimitiveId` (`DepthTarget | TransferSrc`, ID-target extent, cleared to 1,
     stored); the readback copy looks it up instead of `SceneDepth` and moves it from the depth
     attachment layout to `TransferSrc` and back. The outline-only entity ID pipeline keeps
     `Equal` without writes explicitly (today it derives from the pick descriptor).
     The pick readback copies the depth from `Picking.Depth`, so the hit position belongs to the
     picked primitive (a point in front of a surface no longer reports the surface behind it).
     Outline-only frames (no pending pick) keep today's single-target `SceneDepth` route.
  2. Lanes per selection mode. The rendering pick request (`PickPixelRequest` →
     `RenderFrameInput::Pick` → the render-world `PickRequest` snapshot → the picking route) carries
     which primitive ID sub-passes to record, all by default for direct frame-input callers, set by
     `SceneInteractionModule` from the issuing target: Entity and Vertex draw
     surfaces, edges and points; Edge draws surfaces and edges; Face draws surfaces only. An
     Edge- or Face-mode click therefore never gets a point hint (which refines to a vertex only)
     where it used to get an edge or face hint. Confirmed by the operator (question 3): an
     Edge- or Face-mode click on a point passes through it to the edge or face behind.
  3. `GpuRender_Selectable` from `SelectableTag` on every instance (main instance through
     `BuildRenderFlags`, the graph point-lane instance, the mesh edge/vertex views), so the
     culling fills `SelectionPoints`; the shared-recipe selection candidate counts follow it.
  4. Tests (each fails without its change): update the existing descriptor assertions in
     `RendererFrameLifecycle` (`Test.RendererFrameLifecycle.cpp`, the edge/point ID
     `Equal` checks and the pick-variant entity ID); a frame-recipe test that a pending pick
     declares and writes `Picking.Depth` and an outline-only frame does not; a
     `SceneInteractionModule` test that each target's mask reaches the render-world snapshot and
     a renderer test that only the masked sub-passes record (`SelectionPrimitiveIdPassCount`
     adjusted to masked execution); extraction tests in
     the owning suites (`MeshGeometryExtraction` main instance and views, `GraphGeometryExtraction`
     split point lane) that a selectable entity's instances carry the flag and a
     non-selectable one's do not (mutation: an unconditional flag fails the negative case).
  5. `gpu;vulkan` smokes, each mechanism mutated independently (edge depth op, point depth op,
     pick-private depth, lane mask): on the shifted-points ReferenceTriangle a Vertex-mode click
     on a shifted point (vertex 2, hit near the shifted position) and an Entity-mode click on
     it; a selectable point-cloud-only and graph-only entity clicked on a point (Entity, Vertex)
     and the graph on an edge line (Entity, Edge); a mesh edge view with edges bound away from
     the surface clicked in Edge and Entity mode; a surface-backed edge near a vertex clicked in
     Vertex, Edge, Face and Entity mode (no regression of edge/face hints); a point in front of a
     surface and one behind it (front picked with its own depth, behind not picked); two
     overlapping points and two overlapping edges at different depths, each with the near one
     drawn first and drawn last (the near one picked with its depth; the comparisons and the depth
     writes mutated independently); BUG-026B's face click and the background clear keep passing.
- **Slice B — the pick footprint matches what is drawn.** Edge ID draws the visible line quads
  (`LineQuads` with the forward line expansion); point ID matches the visible point: per-point
  size, circular/oriented coverage and sphere-impostor depth (`forward/point.vert`,
  `forward/point.frag`). Planned after slice A lands; the session stops and reports before it.

Slice A alone does not complete question 1: native one-pixel edge lines stay hard to hit and
point footprints are squares of the uniform size until slice B; the selection outline stays
surface-only (UI-083 slice 3c highlights selected elements).

## Acceptance criteria
Slice A:
- [ ] A click on a point with no surface beneath (mesh vertex view, graph node, point cloud
      point) hits its entity and resolves to that element in Vertex mode and to the entity in
      Entity mode.
- [ ] A click on an edge line with no surface beneath (mesh edge view, graph edge) hits its
      entity and resolves to that edge in Edge mode.
- [ ] Of overlapping drawn primitives the nearest is picked, and the reported hit position lies
      on it; Edge- and Face-mode clicks keep their edge and face hints.
- [ ] A point drawn from a bound Position (UI-083) is picked where it is drawn and resolves to
      its canonical vertex id (extends `RuntimeSandboxAcceptanceGpuSmoke.BoundPointPositionsDrawWhileTheSurfaceStaysCanonical`).
- [ ] Surface face picks (BUG-026B) keep passing; each case above has a `gpu;vulkan` smoke cited
      as run, with host GPU and driver.

Slice B:
- [ ] The pick footprint matches what is drawn: line width, point size (uniform or per point),
      point shape and sphere depth.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 6
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120 \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ClickPick|BoundPoint|.*Pick)'
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j 6
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(RendererFrameLifecycle|FrameRecipeContract|SelectionSystemContracts|PrimitiveSelectionRefinementWiring|SceneInteractionModule|MeshGeometryExtraction|GraphGeometryExtraction)\.'
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-25: Plan review (Claude author → Codex reviewer, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a173a0-dfdd-7652-b543-4d73aff845eb`;
  recursion protection instruction-only). Round 1 on the first plan: "revise" (overlap order,
  selection-mode precedence, hit depth, existing tests, smoke matrix). Round 2 on the revised
  plan: "revise" (the lane mask must travel through `RenderFrameInput::Pick`, an overlap smoke
  for points and edges, integration notes); folded into the plan as review input before
  implementing, as interactive pair work allows.
- 2026-10-25: Slice A implemented.
  - `Picking.Depth` (`FrameRecipeResourceKind::PickingDepth`, appended): created with
    `PrimitiveId` while a pick is pending, `DepthTarget | TransferSrc`, cleared to 1; the
    `PickingPass` writes it instead of reading `SceneDepth`, and the readback copies the pick
    pixel from it (depth attachment ↔ transfer-source barriers). `SceneDepth` lost the
    picking-only `TransferSrc` usage.
  - Pick pipelines: entity `LessEqual` with writes, face `Equal`, edge and point `LessEqual`
    with writes; the outline-only entity pipeline keeps `Equal` without writes explicitly.
  - `PickPrimitiveLanes {Edges, Points}` on `PickPixelRequest` and the render-world
    `PickRequestSnapshot` (all by default); `SceneInteractionModule` sets it from the issuing
    target (Entity/Vertex both, Edge edges, Face neither); the picking route records only those
    sub-passes, and `SelectionPrimitiveIdPassCount` counts a masked frame once.
  - `GpuRender_Selectable` from `SelectableTag` on the main instance, the graph point-lane
    instance and the mesh edge/vertex views.
  - CPU tests: `RendererFrameLifecycle` descriptor checks updated (entity pick, edge, point),
    the readback test asserts the depth comes from the pick pass's own depth, a new test that a
    pick records only its lanes' sub-passes (4/3/2); `FrameRecipeContract` resource/depth checks
    updated and a new test for `Picking.Depth` (cleared, transfer source, absent from outline-only
    frames); `SceneInteractionModule` per-target lanes reach the render world; mesh and graph
    extraction tests for the flag with a non-selectable control. Each fails with its mechanism
    removed (lanes ignored in the renderer, lanes not set by the runtime, unconditional flag, the
    pass reading `SceneDepth`).
  - Vulkan smoke `RuntimeSandboxAcceptanceGpuSmoke.ClicksPickTheNearestDrawnPrimitiveOfTheSelectionMode`
    (21 clicks, geometry placed on exact pixel rows by inverse projection): point cloud point
    (Entity, Vertex), graph node (Entity, Vertex) and edge band (Entity, Edge), a mesh drawn from
    bound edge positions (Entity, Edge) and bound point positions (Vertex, Entity), a
    surface-backed vertex in Vertex/Edge/Face/Entity mode, a point in front of (picked, cursor
    z 0.5) and behind the surface (surface picked), overlapping points and edge bands at depth
    0.5 and -0.5 in both creation orders (near picked, cursor z 0.5), background (no hit).
    Passed three times (host `hochvogel`, NVIDIA GeForce RTX 4090, driver 580.159.04, X11 `:1`,
    1600x900 backbuffer, VSync off). Mutations on Vulkan, each failing exactly its clicks: edge
    `Equal` (all edge clicks), no selectable flag on main instances (cloud and graph points),
    point `Equal` (all points without a surface behind), lanes ignored (Edge/Face clicks on the
    surface vertex get a vertex), edge depth writes off (near band loses or reports the wrong
    depth), point depth writes off (near point loses, wrong cursor depth), mesh views not
    selectable (bound point misses, surface vertex falls back to a face), pick pass on
    `SceneDepth` (cursor depth wrong, Edge click loses its edge).
  - Vulkan suites after the change: `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` 44/44,
    `IntrinsicGraphicsVulkanSmokeTests` 36/36, `IntrinsicPointLBVHGpuTests` 55 passed, 1 skipped.
  - After restoring the formatter's churn to the original style: the default CPU gate
    (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) passes 5892/5892 (2 native-window skips),
    and on the final sources `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` 44/44 and
    `IntrinsicGraphicsVulkanSmokeTests` 36/36. Task policy, doc links, layering, test layout and
    docs sync pass. jscpd: no new clone in production lines beyond the existing pipeline
    descriptor pattern; the mesh and graph flag tests share about 20 test lines.
  - Not covered by slice A: one-pixel native edge lines and square, uniform-size point footprints
    (slice B); overlapping primitives at exactly equal depth resolve by sub-pass order.
- 2026-10-25: Slice A code review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a1748c-8bc5-78f0-b017-37624fbd1c8d`;
  recursion protection instruction-only), on diff sha256 `44ccaa82…`. Verdict verbatim "approve"
  ("No blocking production defect found"; "Approval is for slice A, not completion of the
  operator's full selection goal"). Optional findings, addressed after the verdict (they go into
  the next block's review): an unused variable and a stale depth comment removed; the CPU readback
  test now also asserts the depth copy itself (`MockCommandContext` records
  `CopyTextureToBuffer`; removing the copy fails the test); the smoke pins the graph band's
  middle edge and checks that the strip's picked edge passes within one pixel of the click (its
  diagonals lie between two rows, so edge 11 rather than the horizontal edge can be the one
  drawn under the pixel). Recorded as is: picking still requires the depth prepass in the recipe,
  hover picks keep the Entity lanes, and the picking pass's buffer read list predates this slice
  (declares `LineQuads`/`Points` rather than what the ID passes draw).
  - After these fixes the click smoke passed three more times on the RTX 4090.
