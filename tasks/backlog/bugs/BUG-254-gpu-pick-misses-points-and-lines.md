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
4. (Slice B, 2026-10-25) "Weiter mit BUG-254 Slice B: Klickfläche = sichtbare Linienbreite und
   Punktgröße/-form. Erst Plan mit Review, dann umsetzen."

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
- **Slice B — the click footprint is the drawn footprint (question 4).** Slice A made every
  lane hittable; this slice makes it hittable exactly where it is drawn. Steps:
  1. Shared footprint code. The line-quad expansion of `forward/line.vert` moves unchanged into
     `common/line_quad.glsl`; the point-quad expansion and per-point size of `forward/point.vert`
     and the disc/surfel metric and sphere-impostor coverage and depth of `forward/point.frag` move
     unchanged into `common/point_quad.glsl`. The forward shaders call them (same math, same
     `invariant gl_Position`), and so do the ID shaders, so the click footprint follows any later
     change of the drawn one.
  2. Edge ID. `EdgeIdPass` draws the `LineQuads` bucket (non-indexed, six vertices per segment)
     instead of native `Lines`; `edge_id.vert` expands with the shared function (uniform or
     per-edge width) and passes the segment index flat to `edge_id.frag`, replacing
     `gl_PrimitiveID` (which now counts triangles). Pipeline `TriangleList`, no culling. The
     instance set is unchanged (both buckets come from `GpuRender_Line`, not gated by the
     selectable flag, as today). The picking pass declares the `LineQuads` buffers it now reads
     instead of `Lines`.
  3. Point ID. `instance_cull.comp` emits `SelectionPoints` as quads (`PointVertexCount * 6`,
     `PointFirstVertex * 6`, as `Points`; `PointIdPass` is its only consumer). `point_id.vert`
     expands with the shared function (uniform or per-point size) and passes point index, mode,
     disc UV and view centre/radius/normal; `point_id.frag` discards where the drawn point
     discards (metric > 1, sphere ray miss) and writes the drawn point's depth (sphere-impostor
     depth in Sphere mode, the quad depth otherwise). Pipeline `TriangleList`, no culling,
     `gl_PointSize` removed.
  4. CPU tests, red first: the `RendererFrameLifecycle` descriptor tests expect `TriangleList` for
     the edge and point ID pipelines; `GraphicsSelectionPassContracts` expects `EdgeIdPass` to draw
     `LineQuads` non-indexed; `FrameRecipeContract` expects the picking pass to read `LineQuads`.
     The existing forward point-size source checks in `RendererFrameLifecycle` follow the moved
     code into the include; no new source-text test (AGENTS.md §7): the shared footprint is
     proved by the smoke.
  5. `gpu;vulkan` smoke `RuntimeSandboxAcceptanceGpuSmoke.ClickFootprintIsTheDrawnLineWidthAndPointShape`
     (geometry by inverse projection; every click at least 2 px inside or outside a footprint
     boundary; the backbuffer pixel at each click is sampled too: drawn where a hit is expected,
     background where a miss is expected, so the forward refactor is checked as well):
     - a graph edge of uniform width 12 px: Edge-mode clicks 4 px off its centre line (that
       edge) and 9 px off (no hit);
     - a graph of two edges with uniform width 1 and a per-edge width property {12, 1}: clicks
       4 px off each (edge 0; no hit);
     - a Flat point of uniform size 16: clicks 6 px (hit) and 10 px (no hit) right of it;
     - a cloud of two Flat points of uniform size 4 with a per-point size property {24, 4}:
       clicks 8 px right of point 0 (point 0) and of point 1 (no hit), and at (+10, +10) from
       point 0, 2 px inside its square and 2.1 px outside its disc (no hit);
     - a Sphere-mode point of 32 px whose centre lies 0.4 R behind a selectable surface (R its view
       radius): a Vertex-mode click on its centre picks the point with the cursor at the drawn cap,
       z = 0.6 R (the quad depth would be -0.4 R, behind the surface).
     Mutations in the ID shaders only (the forward draw unchanged), with the clicks each must fail:
     edge width 1 px (uniform-width edge at 4 px, per-edge edge 0); per-edge widths ignored
     (per-edge edge 0 only); pick point size x1.5 (uniform point at 10 px and the disc corner
     hit); per-point sizes ignored (point 0 at 8 px); no disc discard (disc corner hits); quad
     depth instead of sphere depth (sphere click picks the surface); one vertex per selection
     point in the cull (every positive point click of both smokes on background misses: here the
     uniform point, point 0 and the sphere; in the slice A smoke the cloud, graph node, bound
     point and near-point clicks). The slice A click smoke and BUG-026B keep passing.
  6. Docs: the `PickingPass` row of `docs/architecture/rendering-three-pass.md` and the ID shader
     comments.
  Overlap order (plan review): forward lines draw `LessEqual` without depth writes and points
  after them with writes, so a farther line drawn later, or a point behind a line, can be drawn
  over a nearer line, while the pick keeps slice A's rule (the nearest primitive wins). The
  footprint change does not alter this; whether forward lines should write depth is a rendering
  decision for the operator, raised at the end of the slice.
  Surfel coverage equals the disc today (`forward/point.vert` emits the view normal +Z); it is
  shared, not separately tested. Unchanged: equal-depth ties resolve by sub-pass order, hover
  picks use the Entity lanes, and the picking pass declares `Points` while `PointIdPass` reads
  `SelectionPoints` (recorded in slice A).

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
      point shape and sphere depth, each with a `gpu;vulkan` click cited as run and its mechanism
      mutated.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests -j 6
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120 \
  -R '^RuntimeSandboxAcceptanceGpuSmoke\.(ClickPick|BoundPoint|.*Pick|ClickFootprint)'
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests IntrinsicRuntimeContractTests -j 6
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(RendererFrameLifecycle|FrameRecipeContract|GraphicsSelectionPassContracts|SelectionSystemContracts|PrimitiveSelectionRefinementWiring|SceneInteractionModule|MeshGeometryExtraction|GraphGeometryExtraction)\.'
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
- 2026-10-25: Slice B plan review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a1750a-0e74-78b2-840a-113241aeb053`;
  recursion protection instruction-only), on the plan with sha256 `4ee35ac8…`. Verdict verbatim
  "revise". Blocking: the overlap order of drawn lines and points differs from the nearest pick
  (recorded in the plan as an operator decision, not changed here); the smoke lacked per-edge
  width, a uniform point's boundary, the sphere hit depth and pixel samples of the drawn
  footprint (added); the disc-corner probe (9, 9) had a 0.73 px margin and the one-vertex mutation
  needed named predictions (fixed). Optional, followed: keep the shared seam with stage built-ins
  as parameters and the existing sphere helper; anchor structural checks to `SelectionPoints` or
  leave them to the smoke (no new source-text test, AGENTS.md §7).
- 2026-10-25: Slice B implemented.
  - `common/line_quad.glsl` (`GpuLineQuadCornerAt`) and `common/point_quad.glsl`
    (`GpuPointSizePx`, `GpuPointQuadCornerAt`, `GpuPointDiscMetric`, `GpuPointFragmentCoverage`,
    built on the existing `ShadeSphereImpostor`) hold the visible line and point footprint, moved
    from `forward/line.vert`, `forward/point.vert` and `forward/point.frag`, which now call them;
    `gl_FragCoord.z` enters the coverage helper as a parameter. The optimized SPIR-V of the forward
    shaders is not identical to before (control flow nesting; the sphere branch now loads only
    `CameraProj`), so the forward draw is checked by the smokes' pixel samples, not by a binary diff.
  - `selection/edge_id.vert` and `point_id.vert` call the same helpers and pass the segment/point
    index flat; `point_id.frag` discards and writes depth through `GpuPointFragmentCoverage`.
    `EdgeIdPass` draws `LineQuads` (non-indexed); `instance_cull.comp` emits `SelectionPoints` as
    six vertices per point; both ID pipelines are `TriangleList`; the picking pass declares the
    `LineQuads` buffers instead of `Lines`.
  - CPU tests: `RendererFrameLifecycle` (edge/point ID `TriangleList`), `GraphicsSelectionPassContracts`
    (edge pass draws `LineQuads` non-indexed), `FrameRecipeContract` (picking reads `LineQuads`, not
    `Lines`): 4 failures before the change, green after. The legacy source checks in
    `RendererFrameLifecycle` (camera view-projection, forward point size and sphere) follow the
    moved code into the includes.
  - Vulkan smoke `RuntimeSandboxAcceptanceGpuSmoke.ClickFootprintIsTheDrawnLineWidthAndPointShape`
    (11 clicks, each with a pixel check that the pick result matches what is drawn at the click:
    hit where red is drawn, miss on background); the slice A smoke now shares its click driver
    (`Bug254ClickDriver`). Against the slice A production code it fails exactly the four predicted
    clicks (wide edge, per-edge wide edge, per-point large point, sphere cap), plus the pixel checks
    of the three missed drawn clicks (prediction corrected: the pixel check compares the actual
    pick with the drawn pixel). Mutations, each failing as predicted: edge width 1 px (both wide-edge
    clicks), per-edge widths ignored (per-edge click), uniform pick size x1.5 (10 px click hits),
    per-point sizes ignored (8 px click on point 0), no disc discard (corner click hits), quad depth
    instead of sphere depth (sphere click picks the surface), one vertex per selection point
    (uniform, per-point and sphere clicks; the slice A smoke fails at `DeviceOperational` instead of
    named clicks: the first vertex no longer matches the shader's offset, geometries with a nonzero
    first point vertex read out of bounds and the device is lost at the first pick).
  - Runs on host `hochvogel`, NVIDIA GeForce RTX 4090, driver 580.159.04, X11 `:1`, 1600x900
    backbuffer, VSync off: the new smoke passed three times; `RuntimeSandboxAcceptanceGpuSmoke` 45/45
    and the graphics Vulkan smokes 36/36 (`IntrinsicGraphicsVulkanSmokeTests`). CPU (focused):
    `IntrinsicGraphicsContractCpuTests` 580/580, `IntrinsicGraphicsIntegrationCpuTests` 75/75,
    `IntrinsicGraphicsContractTests` 71/71.
  - jscpd: one new 10-line clone in the smoke (the backbuffer readback buffer block of the
    bound-point smoke); in the shaders `edge_id.vert` and `point_id.vert` repeat the mirrored push
    block and scene fetch (12 lines), as the other ID shaders do.
  - ponytail-review (author, staged diff): "Verdict: Ship." Findings: the `Lines` cull bucket has
    no drawer left (the edge pick was its last reader; culling still fills it each frame, like the
    already unused `SelectionLines`) - left for a separate cleanup, since removing a bucket kind
    touches the RHI enum, the cull table layout and the HZB smoke; a redundant smoke assertion -
    removed, and both click smokes passed again.
- 2026-10-25: Slice B code review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17525-2980-7e02-aaeb-9c72a2cf956f`;
  recursion protection instruction-only), on the staged diff sha256 `4afa1cf6…`. Verdict verbatim
  "approve" ("No blocking correctness defect found"; "Approval covers this footprint slice. The
  documented forward-line overlap ordering mismatch remains; complete visible-object/pick
  agreement is therefore not established."). Optional findings, both follow-ups outside this
  slice: retire the `Lines` bucket that nothing draws any more; declare the `SelectionPoints`
  buffers the point pass reads in the picking pass (pre-existing; every bucket's arguments are
  barriered, so no synchronization failure).
