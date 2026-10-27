---
id: UI-083
theme: J
depends_on: [RUNTIME-315, UI-075, UI-081]
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests and CI
maturity_target: Operational
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality, repo.source-documentation]
---
# UI-083 — Grouped processing menus and per-primitive Appearance attributes

## Operator questions (verbatim, 2026-10-23)
1. "please move UI/Mesh/Geodesics to Processing/Geodesics."
2. "Also Group the processing methods into meaningful groups. This should be done for Point Cloud, Graph and Mesh and View"
3. "Additionally in UI/View/Appearance: There are three Collapsing Headers for Vertices, Edges and Faces."
4. "Vertices should let me control the attributes for rendering of the vertex primitive point cloud, so Position, Normal, Color (uniform color edit, varying property), PointSize (uniform edit, varying property)"
5. "Edges should let me control the "attributes" for rendering of the edge primitive graph, so Position, Color (uniform color edit, varying property), Line Width (uniform edit, varying property)"
6. "Faces should let me control the "attributes" for rendering of the Face primitive surface, so Position, Normal, Texcoords, Color (uniform color edit, varying property)"
7. "The uniform color edits should only be active if the respective color combo box is set to Default. It should remember the last set color. Default is always grey white."
8. "Vector field rendering should also let me choose : length (uniform edit, varying property), color (uniform color edit, varying property)"

## Goal
- Serves Framework24 convergence (theme J, P0): W5 "Inspect rendering and properties" (every
  render attribute of every primitive reachable and controllable) and W1 discoverability (method
  menus readable instead of one flat list of 25 entries).
- Each Appearance section controls exactly the attributes of one render primitive: Vertices =
  point lane, Edges = line lane, Faces = surface lane.
- Shortest path: the binding table, overlay targets, uniform fields and vector-field layers
  already exist (RUNTIME-315, UI-075, UI-050/UI-081). This task re-keys the existing table by
  lane, splits the one structural Position/Normal stream per lane, and adds two optional
  per-arrow inputs. No new binding model.

## Operator decisions (2026-10-23)
- Menu groups (nine, same names under every domain menu and under View > Processing; a domain
  menu shows only the methods it registers):
  Filtering & Smoothing, Normals, Features & Descriptors, Segmentation & Clustering,
  Fields & Spectral, Geodesics, Sampling & Density, Remeshing & UV,
  Reconstruction & Registration. View keeps its tool windows at the top level.
- Position and Normal are **independent per primitive**: binding `smoothed_pos` as the point
  lane's Position does not move the surface or the edges. This reverses RUNTIME-315 slice 6
  ("meshes viewed as points/edges use the entity's one binding set, no per-lane copies").
  Why (operator, 2026-10-23): to compare original and processed positions on one entity
  without creating another, for example to show the original point cloud and the mesh at the
  new positions.
- Saved scenes keep loading after slice 3a (operator, 2026-10-23). Slice 3a keeps the existing
  binding key names; see its Scene keys.
- Color Default: the imported color (material, texture, `v:color`) when present, else
  grey-white. The starting uniform color is grey-white instead of orange.
- Arrow length = uniform Length × per-arrow property value. Per-arrow color works like the
  Color rows: a scalar goes through the colormap, vec3/vec4 is RGB(A).
- Selection with per-lane positions (operator, 2026-10-25): the goal is that points, edges,
  faces and entities can all be selected. A selection stays element-based (domain and
  canonical index, no lane); it is highlighted on every drawn lane that shows the element
  (vertex: surface, edges, points; edge: surface, edges; face: surface; graph node: edges,
  points), one highlight per distinct position source. The clicked lane only decides how a
  click is refined (slice 3a). Making points and edge lines with no surface behind them
  clickable is BUG-254.

## Defaults chosen by the planning session (veto in review)
- A uniform color/size/width field (and the arrow color) stays visible but greyed out while its
  row is bound to a property, showing the remembered value; Default restores that value. The
  arrow length is the exception: its uniform is the multiplier, so it stays enabled.
- Grey-white is `(0.9, 0.9, 0.9, 1)`, one constant shared by every lane default.
- Vector-field arrows keep their orange default color (grey-white arrows vanish on a
  grey-white surface).
- Window titles keep the `<Domain> / Processing / <Method>` pattern without the group name;
  only the Geodesics window is renamed, to `Mesh / Processing / Virtual Source Propagation`.
  Window ids are unchanged, so redirects, agent `open_window` and workspace layouts keep working.

## Context (investigated 2026-10-23 at `f849aafb1`)
- Menus: `DrawRegisteredWindowMenuTree` (`Sandbox.EditorShell.cpp:273-334`) builds submenus from
  `MenuPath` in first-registration order. Processing windows are registered in
  `Sandbox.MeshProcessingPanels.cpp:526-652` (canonical `view.*` windows under `{"View"}` plus
  per-domain redirect entries under `{<Domain>, "Processing"}`) and `Sandbox.MethodPanels.cpp:603-650,
  784` (K-Means, Progressive Poisson, Consolidation, Parameterize). Geodesics sits at
  `{"Mesh", "Geodesics"}` (`Sandbox.MeshProcessingPanels.cpp:553-556`).
- Appearance: `kAppearanceSections` (`Sandbox.DomainPanels.cpp:856`) maps Vertices/Edges/Faces to
  the PointCloud/Graph/Mesh lane models; `DrawAttributeSourceTable`
  (`Sandbox.PanelSupport.cpp:2494`) puts a row into a section by its **element domain**
  (`SectionOfAttributeDomain`, `:2438`), while the row's Color target is a **lane**
  (`ColorOverlayTargetFor`, `Runtime.VisualizationEditingOperations.AttributeBindings.cpp:43`).
  Consequences today, for a mesh: the surface's Position/Normal/Texcoord rows and the `v:color`
  row (which colors the surface) sit under Vertices; the point lane has no Color row of its
  own; Edges has no Position row; a graph's node Color row colors the Edges lane.
- Rules: `kRules` (`Runtime.VertexChannelBindings.cpp:68-91`) keyed by (attribute, element domain).
  Structural streams live in the per-entity `VertexChannelBindingSet`; Color is the lane's
  `VisualizationConfig`; size/width are `RenderPoints::SizeSource` / `RenderEdges::WidthSource`,
  a `std::variant<float, std::string>` (`Graphics.Component.RenderGeometry.cppm:101,118`), so
  binding a name drops the uniform value and Default restores 6 px / 1 px (UI-075 log).
- Default colors: `VisualizationConfig::Color{1, 0.6, 0, 1}` (`Graphics.Component.VisualizationConfig.cppm:80`)
  and the editor command defaults (`Runtime.VisualizationEditingOperations.cppm:415,502`) are orange.
  Imported color reaches only the surface: `v:color` feeds the surface's structural color stream
  (`Runtime.GeometryPlanBuilders.Mesh.cpp:215`), while points and edges on Default fall back to
  white (`assets/shaders/forward/point.vert:26`). The recipe command derives the Color lane from
  the property domain alone (`Runtime.VisualizationEditingOperations.Actions.cpp:1711`).
- Uniform fields are hidden, not disabled, while bound (`DrawAppearanceLane`,
  `Sandbox.DomainPanels.cpp:930-968`). The uniform color is already kept in the lane's
  `VisualizationConfig::Color` across a bind (comment at `:457`); size/width are not.
- Related open work: GRAPHICS-158 owns Vulkan evidence for rebound attributes (RUNTIME-315);
  GRAPHICS-105 owns the material normal channel (texture vs vertex), orthogonal to the
  structural per-lane normal here; UI-075 slices 1-4 are implemented (its Operational slice 5
  is open).

## Slice plan

### Slice 1 — grouped processing menus (app only, ~150 lines)
- One `constexpr` list of the nine group names in `Sandbox.PanelSupport.hpp`, used by both
  panel files, so a misspelt group cannot create a second submenu.
- Every processing registration gets `{<Domain>, "Processing", <Group>}`; canonical `view.*`
  processing windows get `{"View", "Processing", <Group>}`. Geodesics moves to
  `{"Mesh", "Processing", "Geodesics"}`. Vertex/Face Normals move from
  `Processing/Vertices` and `Processing/Faces` into `Processing/Normals` as "Vertex Normals" and
  "Face Normals"; Scalar Field Gradient moves from `Processing/Faces` into Fields & Spectral.
- Group submenus follow the list order: `DrawRegisteredWindowMenuChildren` orders children that
  appear in the list by list index and keeps first-registration order for all others.
- Tests (`IntrinsicSandboxEditorIntegrationTests`): update the id → menu-path table in
  `Test.SandboxEditorPresentation.cpp` (fails on the old paths); assert no processing window is a
  direct leaf of `<Domain>/Processing` or of `View`; assert the child order of Mesh > Processing;
  rename the Geodesics window lookups in `Test.SandboxProcessingPanels.cpp`.
- Docs: `docs/architecture/primitive-selection.md:30`, `src/app/Sandbox/README.md` and any doc
  naming the old paths (grep `Mesh / Geodesics`, `Processing / Vertices`, `Processing / Faces`).

### Slice 2 — lane-keyed attribute rows, grey-white default, remembered uniforms (~600 lines)
- `EditorAttributeBindingRow` gains `GeometryRenderLane Lane`; the model builds one row per
  (lane, attribute) for every lane the entity draws, and each candidate carries its source
  domain. `SectionOfAttributeDomain` is removed: the section is the lane. The rows and their
  admissible source domains (the `kRules` table becomes lane × attribute × domain):

  | Lane | Position | Normal | Texcoord | Color | Point size | Line width |
  | --- | --- | --- | --- | --- | --- | --- |
  | Surface (mesh) | vertex | vertex, corner | vertex, corner | vertex, face | – | – |
  | Edges (mesh) | vertex | – | – | vertex, edge | – | edge |
  | Edges (graph) | node | – | – | node, edge | – | edge |
  | Points (mesh) | vertex | vertex | – | vertex | vertex | – |
  | Points (graph) | node | node | – | node | node | – |
  | Points (cloud) | point | point | – | point | point | – |

  Corner sources stay surface-only: a point record is one per vertex, so several corner normals
  cannot feed it (`Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp:162`).
- `EditorAttributeBindingCommand` and the recipe command it calls carry the requested `Lane`
  (today the lane is derived from the domain, `Actions.cpp:1711`); Color binds that lane's own
  overlay target, so a mesh's point lane can be colored independently of its surface. The
  shaders already index these combinations (points: vertex-indexed, `point.vert:102`; edges:
  endpoint or edge index, `line.vert:72`). A pair outside the table is refused with a typed
  reason.
- Position/Normal rows are lane-keyed in the model and command but still write the shared
  structural stream until slice 3a; their status says "shared by all primitives".
- Color Default precedence, per lane (row on Default):
  1. a uniform color the user picked for this lane (remembered across binds);
  2. else the lane's imported color. Surface: the authored material/texture and `v:color` (as
     today). Points and Edges: the canonical color property of their source domain (`v:color`
     on mesh vertices, `p:color` on points). This is new; today they draw white.
  3. else grey-white `kDefaultUniformColor`, which also replaces the orange starting value
     (`VisualizationConfig::Color`, editor command defaults).
  The existing Settings entry "Material / default" clears a picked uniform (back to 2/3) and
  keeps its value for the next pick.
- Remembered uniforms: `RenderPoints::SizeSource` / `RenderEdges::WidthSource` become a uniform
  `float` plus an optional property name; scene codec, agent `set_render_hint`/`bind_attribute`,
  extraction lowering and undo follow. Uniform fields are drawn disabled while bound.
- Agent parity: `bind_attribute` takes `lane` (RUNTIME-316 surface), schema and tests updated.
- Tests (red first):
  - Model rows equal the table for a mesh, a graph and a point cloud, and an off-table pair is
    refused (`Test.VertexChannelBindings.cpp`, `Test.SandboxEditorVisualization.cpp`).
  - A Color bind on a mesh's Points lane changes the Points overlay and not the Surface.
  - Default precedence through extracted lane colors, not component defaults: an imported
    `v:color` mesh shows it on surface and points; picking a uniform overrides it; bind a
    property, then Default, brings the picked uniform back; "Material / default" goes back to
    the imported color; a mesh with no imported color draws grey-white on all three lanes
    (`Test.RuntimeRenderExtraction.cpp`).
  - Size 3.5 px → bind → Default restores 3.5 px.
  - Faces, Edges and Vertices list exactly the operator's attributes, and a bound row's
    uniform field is disabled (`Test.SandboxDomainPanels.cpp`).
  - Scene round trip of a bound size keeps the uniform (`Test.RuntimeSceneSerialization.cpp`).
  - Agent `bind_attribute` with `lane` (`Test.AgentOperations.cpp`, `Test.SandboxAgentServer.cpp`).
- `gpu;vulkan` smoke: a mesh's point lane colored apart from its surface, and the grey-white
  Default, read back.

### Slice 3a — independent Position per primitive (~700 lines)
Current state: a mesh already uploads three geometries (surface, edge view, vertex view; residency
sub-keys at `Runtime.RenderExtraction.Geometry.cpp:991-995`), but each resolves the one entity
binding (`ResolveDisplayedPositions(..., channelBindings)`,
`Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp:141`). A graph's point lane reuses the line
geometry (`EnsureGraphPointLaneInstance`, `Runtime.RenderExtraction.Geometry.cpp:1539-1564`).
Every position reader in the shaders (forward, id, depth passes) reads its own instance's
`geo.VertexBufferBDA`, so no GPU layout change is needed.
- Spike first (contract fixture, no production change): a mesh with a bound points-lane
  Position, picked through each ID pass and through the missing-hint CPU fallback, to pin the
  lane rules below before the extraction work starts.
- Storage: `VertexChannelBindingSet` holds explicit per-lane slots, `Surface{Position, Normal,
  Texcoord}`, `Edges{Position}`, `Points{Position, Normal}`, replacing the entity-level slots.
  An unset slot draws the canonical property; no lane inherits another lane's slot.
- Scene keys (scene version stays 6; existing scene files keep loading):
  - The bare keys `position`, `normal` and `texcoord` stay. Each names the entity's first lane,
    in Surface, Edges, Points order, that has the attribute: mesh Surface for all three; graph
    Edges for position and Points for normal; cloud Points.
  - New keys `edges.position`, `points.position` and `points.normal` hold the other lanes. A
    prefixed key for a lane that owns a bare key is rejected as a duplicate.
  - An old file's binding therefore drives the main primitive only. It used to drive every
    primitive, so on load the other primitives show their canonical positions.
  - This is one key per slot, not a second reader. Renaming the bare keys would make the
    strict binding parser (`Runtime.SceneSerialization.cpp:1646-1648`) fail every older scene
    that has a bound attribute (the caller returns `InvalidFormat`, `:2892-2896`). The operator
    asked to keep those scenes loading (2026-10-23).
- `ResolveDisplayedPositions`, `ResolveEntityDisplayedPositions` and
  `FindVertexChannelSourceBinding` take the lane; mesh views pass theirs and capture their own
  revision (`Runtime.RenderExtraction.Geometry.cpp:223-237, 360-384`).
- Graph: when the Points slot resolves to a different source than the Edges lane, upload a
  points-only graph geometry under its own residency sub-key and bind it in
  `EnsureGraphPointLaneInstance`; otherwise keep sharing (no extra memory in the common case).
- Position fronts (GRAPHICS-156): extraction observes one front per distinct displayed source
  instead of one per entity (`Runtime.RenderExtraction.cpp:511, 1674`). Each front's preview is
  routed to, and cleared from, exactly the geometries that display that source, and the preview
  flag and preview bounds become per geometry. A GPU method publishing a secondary lane's
  property is therefore observed too.
- Picking and highlights:
  - Refinement of a GPU hit uses the positions of the lane that **produced** the hint. The
    encoded id carries its primitive kind (`EncodedSelectionId` domain,
    `Graphics.SelectionSystem.cppm:40-46`): face → Surface, edge → Edges, point → Points. The
    depth-derived cursor and the compared positions are then in the same space. The result is a
    canonical element id. Example: in vertex mode a Surface face hit picks the face's vertex
    nearest the cursor in surface space (`Runtime.PrimitiveSelectionRefinement.cpp:412`), even
    when the points lane draws that vertex elsewhere.
  - The missing-hint CPU fallback (`Runtime.PrimitiveSelectionRefinement.cppm:92`,
    `.cpp:835-838`) uses the issuing request's target lane. Vertex requests use Points when
    drawn, else Surface (mesh) or Edges (graph). Edge requests use Edges when drawn, else
    Surface. Face requests use Surface.
  - Highlights choose the lane from each stored selection's domain by the same rule, not from
    the current global mode, so existing selections keep their place across mode changes and
    delayed readbacks (`Runtime.SelectionController.cppm:48`,
    `Runtime.SceneInteractionModule.Selection.cpp:35-38`).
  - The pick stamp records the position revision of every drawn lane. `BindingGeneration`
    alone misses an edit to an already-bound secondary property
    (`Runtime.SceneInteractionModule.cpp:168`).
- Other consumers:
  - Culling bounds are the union of the drawn lanes' displayed spans
    (`Runtime.RenderExtraction.cpp:447-492`). UI-082's bounding box, **F** framing and gizmo
    pivot use the same union.
  - Vector-field anchors and glyph order use the entity's first lane (mesh Surface, graph
    Edges, cloud Points), which is today's behavior.
  - Shadow and depth passes are surface-only.
- Persistence beyond the keys:
  - `AnyVertexChannelBindingEnabled` counts every lane slot (`Actions.cpp:317-323`); otherwise
    a set that holds only Points/Edges slots is deleted (`:1859-1862`).
  - The asset signature (`AssetWorkflowRecipePolicies.cpp:168-185`) covers the lane slots.
  - Agent rows and `bind_attribute` use the lane.
- Tests, red first (`IntrinsicRuntimeContractTests`, `IntrinsicRuntimeGraphicsCpuTests`):
  - A per-lane variant of `MeshGeometryExtraction.BoundPositionSourceFeedsTheSurfaceAndBothPrimitiveViews`:
    binding the surface leaves both views canonical, and binding points leaves the surface
    canonical.
  - A graph points binding gets its own geometry; an unbound one keeps sharing.
  - Culling bounds are the union.
  - Two simultaneous position sources with independent preview accept and discard
    (`Test.PositionPreviewExtraction.cpp`).
  - A fixture with widely separated Surface and Points positions (`Test.PrimitiveSelectionRefinementWiring.cpp`,
    `Test.PrimitiveSelection.cpp`):
    - a Surface face hit in vertex mode returns the vertex nearest in surface space and
      highlights it at its points-lane position;
    - a Points hit refines against points-lane positions;
    - a delayed pick after a secondary property edit is rejected as stale;
    - the missing-hint fallback uses the request's target lane;
    - a stored selection keeps its highlight lane after a mode change.
  - A scene saved by the current build with a bound `position` loads and binds the main
    primitive only. A duplicate bare/prefixed key is rejected.
  - Round trip of the lane keys; a Points-only set survives (`Test.RuntimeSceneSerialization.cpp`,
    `Test.SceneDocumentModule.cpp`).
  - Agent `bind_attribute` with `lane`.
- `gpu;vulkan` smoke: points drawn at a bound position while the surface stays canonical, and a
  pick at the displayed point pixel returns the canonical vertex id. The pick half moved to
  BUG-254 (on Vulkan a click on a point or edge line with no surface behind it misses).

### Slice 3c — highlight a selection on every drawn lane (~150 lines)
Operator request (2026-10-26, after BUG-254/255): continue with this slice ("Ja bitte").
Serves question 4-6 (per-primitive Position) together with the operator decision of 2026-10-25
on selection highlights; BUG-254 made every highlighted primitive clickable.
- Today `BuildPrimitiveSelectionRenderSnapshot` (`Runtime.SceneInteractionModule.Selection.cpp`)
  draws a selection on one lane (`SelectionLaneFor`): a mesh that draws points highlights a
  selected vertex only at its points-lane position, even when the surface shows it elsewhere.
- Change: the highlight lanes are the drawn lanes (`DisplayedPositionLanes`) whose primitive shows
  the selection's domain - vertex-like (mesh vertex, graph node, cloud point): surface, edges,
  points; mesh edge and halfedge: surface, edges; graph edge and halfedge: edges; face: surface.
  Each distinct displayed position source among them gets one set of markers (same source on two
  lanes: one marker). When no drawn lane shows the domain (a face selection on a mesh drawn only
  as points), the current `SelectionLaneFor` lane stays the fallback, so nothing that is
  highlighted today disappears. The distinct-source loop moves out of
  `ResolveEntityDisplayedPositions` into a helper both use (`ResolveDisplayedPositionSources`,
  lanes passed in); `SelectionLaneFor` stays for the pick's missing-hint fallback.
- Tests, red first (`IntrinsicRuntimeContractTests`, `Test.PrimitiveSelection.cpp`):
  - the harness quad drawing surface, edges and points with three distinct positions (surface
    z+3, edges z+2 bound; points canonical): a selected vertex has three markers (today: one);
    a selected mesh edge has lines on the surface and the edges lane only (today: one); a selected
    face stays on the surface; after binding edges to the surface's property a selected vertex has
    two markers (dedup by resolved property name);
  - a graph drawing edges (z+2 bound) and points (canonical): a node has two markers; a graph
    edge and a graph halfedge have a line on the edges lane only (halfedge index / 2 kept);
  - fallbacks kept: an entity with no render hint highlights on its primary lane
    (`DisplayedPositionLanes`, the existing `HighlightFollowsBoundPositions`); a face selected on
    a mesh drawn only as points keeps the surface (`SelectionLaneFor`).
  Vertex and edge cases fail before the change; the face, dedup and fallback checks may already
  pass and are shown sensitive by targeted mutations.
- `gpu;vulkan`: `RuntimeSandboxAcceptanceGpuSmoke.BoundPointPositionsDrawWhileTheSurfaceStaysCanonical`
  also selects the ReferenceTriangle's apex vertex and finds the orange highlight both at the
  canonical apex (surface) and at the shifted apex (points lane), with an orange predicate that
  rejects the red points, the grey surface and the background after tone mapping; the existing
  red assertions stay. Mutation: back to the single `SelectionLaneFor` lane (only the
  canonical-apex highlight disappears).

### Slice 3b — Normal on the point lane (~300 lines, new shader read)
Operator request (2026-10-26): continue with this slice, plan and review first ("Ja bitte").
Serves question 4 ("Vertices should let me control ... Position, Normal, Color ..., PointSize"):
the Vertices Normal row exists since slice 2 but changes nothing on screen.
- Today (investigated at `9a845417f`):
  - `GpuPointQuadCornerAt` (`assets/shaders/common/point_quad.glsl:74`, shared by the drawn point
    and the point pick since BUG-254) sets every view normal to +Z, so Surfel discs always face
    the camera. `GpuPointDiscMetric` already draws a Surfel as the disc's projected ellipse
    (minor axis |n.z|, clamped to 0.15) and `forward/point.frag` shades it by |n.z|.
  - Point normals did reach the shader once (oct-encoded in the UV slot, BUG-028 `899c8b890`);
    `dc2806d68` ("Restore surface normal shading") removed that encoding and left +Z.
  - Uploads: graph and point-cloud geometry carry a normal stream only when `Points.Normal` is
    bound (`PrepareBoundVertexChannels`, `Runtime.GeometryPlanBuilders.Graph.cpp:28`); a degenerate
    or non-finite element is written as object-space +Z. The mesh vertex view
    (`BuildMeshVertexViewPlan`) uploads no normals, and its revision snapshot zeroes `Normal`, so
    a normal rebind or a `v:normal` edit would not re-upload it.
  - Clip space is Y-up (negative viewport height), so the quad's disc UV and view-space x/y agree.
- Change:
  - `ResolvePointNormalSource(vertices, vertexDomain, bindings)` in `Runtime.VertexChannelBindings`
    next to `ResolveDisplayedPositions`: the bound `Points.Normal` source when it lives on
    `vertexDomain`, is vec3 and has one value per element; otherwise the canonical normal of the
    rule table (`v:normal` on mesh vertices, `p:normal` on cloud points, none on graph nodes) when
    it exists with the right count; otherwise none. Non-finite values do not disqualify a source
    (normals are repaired per element, as the table states).
  - `PrepareBoundVertexChannels` uploads that source, normalized; a zero-length or non-finite
    element is written as (0, 0, 0) instead of object-space +Z. The mesh vertex view calls it on
    `MeshVertex` and passes `NormalBytes`.
  - Revisions: the graph/point-cloud snapshot tracks the resolved source (today only a bound
    one); the vertex view keeps its `Normal` revision and `MeshPrimitiveViewRevisionChanged`
    compares it, so a rebind or a normal edit rebuilds the view. The edge view stays normal-free.
  - Shader: `GpuPointQuadCornerAt` reads `geo.NormalBufferBDA` at the point when the address is
    set, maps it by `transpose(inverse(mat3(dyn.Model)))` (the house normal matrix of
    `default_debug_surface.vert`) and the camera view, and keeps +Z unless the result has a finite,
    positive length (a zero normal; NaN or infinity from a singular model matrix). Superseded after
    the code review (log, 2026-10-27): Surfels only, the cofactor matrix instead of `inverse()`, a
    largest-component rescale instead of the length cutoff; a flattened cloud shows the plane's
    normal. The pick shares
    the helper, so a Surfel's click area follows its orientation (BUG-254 rule: click area = drawn
    shape).
  - Table text: `PointCloudPoint` Normal's canonical property becomes `p:normal`; the Points
    Normal row's default names it per domain: "mesh v:normal, point cloud p:normal, graph none;
    without one the disc faces the camera; only Surfel points read it". The agent's attribute
    listing reports the same text.
- Decisions for the operator (veto in review): Default draws the canonical normal, so existing
  meshes and point clouds with normals shown as Surfel become oriented (as before `dc2806d68`);
  back-facing normals draw like front-facing ones (|n.z|, unchanged); the 0.15 minimum thickness
  stays, so an edge-on Surfel is a thin line, not invisible.
- Tests, red first (predictions in the log before the first run):
  - `VertexChannelBindings` (`IntrinsicRuntimeContractTests`): the rule table names `p:normal`;
    `ResolvePointNormalSource` picks bound, falls back to canonical for a missing or wrong-domain
    binding, returns none for an unbound graph.
  - Extraction sequences, outcomes asserted as uploaded normal bytes with positions unchanged
    (no dirty tags; which upload mechanism runs, partial or full, is not asserted):
    - `PointCloudGeometryExtraction`: an unbound cloud with `p:normal` {(0,0,2), (0,0,0), NaN}
      uploads {(0,0,1), (0,0,0), (0,0,0)} (today no stream); editing `p:normal` re-uploads;
      removing it drops the stream and adding it back restores it; binding `Points.Normal` to
      `p:alt` uploads that; removing `p:alt` falls back to `p:normal`, adding it back binds again.
    - `GraphGeometryExtraction`: a bound node normal with a degenerate element uploads (0,0,0)
      there (today +Z); editing the bound property re-uploads, both for one geometry shared by
      lines and points and for a split points geometry (points Position bound elsewhere);
      unbinding drops the stream (may already pass; kept as a baseline, not red-first evidence).
    - `MeshPrimitiveViewExtraction`: the vertex view uploads `v:normal` by default (today none);
      editing `v:normal` re-uploads; binding `Points.Normal` uploads the bound values, unbinding
      returns to `v:normal`; the edge view keeps no normals.
  - `gpu;vulkan` (`RuntimeSandboxAcceptanceGpuSmoke`, new test on the BUG-254 click harness):
    - The camera is posed by `ApplyEditorCameraPoseCommand` at (3 sin 60°, 0, 3 cos 60°) looking at
      the origin, so the view rotation is not the identity. Points are placed by unprojecting
      chosen pixels through the controller's `GetView(extent)`.
    - A Surfel cloud of 32 px points whose world matrix scales y by 3. Each point's `p:normal` is
      the object-space normal (Sᵀ R_viewᵀ n_v) that should appear as view normal n_v: a tilted
      n_v = normalize(0.9, 0.3, 0.2) (short half-axis 3.3 px along (0.95, 0.32)), a camera-facing
      n_v = (0, 0, 1), and a zero normal. A Flat cloud with the tilted normal.
    - Vertex-mode clicks and the pixels under them: the tilted Surfel is hit 10 px along its long
      axis and missed 8 px along its short axis; the zero-normal Surfel and the Flat point are hit
      8-10 px along that short axis; the zero-normal Surfel is missed at the square's corner
      (13, 13) px; a last background click clears the selection. The zero-normal Surfel's centre
      has the camera-facing Surfel's colour; the tilted one is darker (shading 0.7 + 0.3 |n.z|).
    - Analytic check of the clicks (metric ≤ 1 means covered): correct: long 0.39, short 5.88;
      no camera transform (view normal (0.64, 0.31, -0.70)): short 0.51, hit; `mat3(Model)`
      (view normal (0.32, 0.95, 0.07)): long 11.4, miss; today's +Z: short 0.25, hit. A missing
      length guard either yields NaN (the square's corner is covered) or a zero normal (the
      centre shades as edge-on, darker than the facing Surfel).
  - Mutations: no shader read; no camera transform; `mat3(Model)` instead of the inverse
    transpose; no length check; normals applied in Flat mode; vertex view without normals; vertex
    view revision without `Normal`; graph/cloud revision tracking only a bound source; fallback +Z
    restored.

### Slice 4 — per-arrow length and color for vector fields (~800 lines, two review blocks)
Operator request (2026-10-27): continue with this slice, plan and review first ("Ja bitte, slice 4").
Serves question 8 ("Vector field rendering should also let me choose : length (uniform edit,
varying property), color (uniform color edit, varying property)") and, for the arrow color,
question 7 (the uniform color is active only on Default and remembers its value).
- Today (investigated at `215432c01`):
  - One GPU instance (9 vertices) per arrow, expanded in
    `assets/shaders/visualization_vector_field.vert`. Length (`Scale`, normalize flag) and color
    (`PackedColor`) are uniform fields of the 128-byte `VisualizationVectorFieldDrawRecord`
    (`Graphics.VisualizationOverlayUploadHelper.cppm:25`); bytes 116-127 are reserved.
  - Extraction (`Runtime.RenderExtraction.VectorFields.cpp`) keeps per-entity anchor, vec3 payload
    and glyph-order caches keyed by property revisions and lends their spans to
    `AppendVectorFieldPacket`. The renderer resolves each packet's buffer keys to addresses by
    domain, value type and element count (`Graphics.Renderer.cpp:2248`); the upload helper drops a
    packet whose addresses did not resolve.
  - Every pipeline uses the global layout with the bindless textures at set 0
    (`Backends.Vulkan.CommandContext.cpp:386`), so the arrow shader can sample a colormap as
    `forward/point.vert` does. `ColormapSystem::GetBindlessIndex` returns the invalid index until
    the lookup textures are ready.
  - The overlay's checked scalar conversion (`ToFiniteFloat`) and auto range (`ComputeRange`:
    2-98 % quantiles from 64 values on, a constant range widened by ±0.5) are file-local in
    `Runtime.VisualizationRecipes.cpp:155,238`.
  - Scene layers are read with every key required (`TryReadGeometryVectorFieldLayer`,
    `Runtime.SceneSerialization.cpp:1483`). No agent operation reaches vector fields.
- Recipe (`GeometryVectorFieldLayerRecipe`):
  - `std::string LengthProperty`, `std::string ColorProperty` (empty: none). Both name a property
    on the vector's own domain, so a plain name replaces the earlier plan's `GeometryPropertyRef`
    and there is no domain to validate. The kind is read from the property when drawn, so a float
    recomputed as double keeps working.
  - `Graphics::Colormap::Type ColorMap{Viridis}`, `bool ColorAutoRange{true}`,
    `float ColorRangeMin{0}`, `float ColorRangeMax{1}`, used while the color property is scalar.
  - `ValidateGeometryVectorFieldLayer` adds: colormap below `Count`; range finite with min < max
    (always, so a remembered manual range stays valid).
  - Scene keys `lengthProperty`, `colorProperty`, `colorMap`, `colorAutoRange`, `colorRange` are
    always written and optional on read (absent: the defaults), so saved scenes keep loading.
- Value rules (shared by extraction, the editor model and the command):
  - Length: a scalar kind the Color rows accept (float, double, int32, uint32, uint64, bool) on the
    vector's domain with one value per element. Arrow = `Length × value` in both length modes (Raw:
    `Length × value × v`). A value that is NaN, ±inf, outside float range or an integer float
    cannot hold exactly (the overlay's `ToFiniteFloat`), and every value ≤ 0, draws no arrow; a
    bool works as a show/hide mask.
  - Color: the same scalar kinds through the colormap, or vec3/vec4 as RGB(A) with components
    clamped to 0..1 (vec3 and colormap colors are opaque). An invalid scalar, or an RGB(A) value
    with a non-finite component, draws that arrow in the uniform color. Auto range: the overlay's
    `ComputeRange` over the valid values only (exported as one float function, not copied); with
    no valid value the range stays 0..1 and is unused.
  - A named property that is missing, of another kind or of another count draws the whole layer
    with the uniform length/color, counts `VectorFieldStyleFallbackCount`, and the layer row's
    status names it. This replaces the earlier plan's "all samples invalid" fallback: the per-arrow
    rules already cover that case (no arrows, or every arrow uniform).
- Extraction:
  - A style cache per (domain, property) beside the vec3 payload cache, released the same way:
    rebuilt when revision or count changes, reused within a frame. Scalars become one float per
    element (invalid: +inf, which the scalar upload accepts) plus the auto range of the valid
    values; vec3/vec4 become one vec4 per element (invalid: (-1, -1, -1, -1)). No cache entry is
    held across the insertion of another (the lent spans point into each entry's own buffers), and
    the payloads stay lent until the batch is submitted (release runs after submission, as
    today); superseded `std::deque`: see the log, 2026-10-27.
  - GPU previews (`geometry.property-coherence`, `docs/architecture/property-coherence.md`
    §Observation): a scalar style property is first offered to the extraction's GPU property
    observer, as the scalar recipe does (`Runtime.RenderExtraction.Recipes.cpp:175`). A front
    with one value per element is bound by address (no CPU upload), and its auto range is the
    front's `ScalarRange`, widened when constant as for the scalar recipe; without one the CPU
    auto range is kept, or 0..1 when the CPU has no such property. The observer is asked with the
    CPU property's scalar kind (a double maps to its float presentation ring), or Float for a name
    the CPU does not have yet. When the front ends (Accept, Discard), the CPU cache is used again,
    or the layer falls back if the CPU has no such property. Vec3/vec4 colors are not observed (the residency
    publishes scalar fronts only).
  - `VectorFieldPacketInputs` and `VectorFieldOverlayPacket` gain a length buffer and a color
    buffer (key, span or address, stamp), the color mode (uniform / colormap / RGBA), the colormap
    and the range. Buffers are lent per element like the vectors, under keys of their own
    (`vector.scalar` / `vector.rgba` + property), so one property used for length and color uploads
    once.
- GPU:
  - The record grows to 144 bytes: `ColormapID@116`, `LengthBufferBDA@120`, `ColorBufferBDA@128`,
    `ColorRangeMin@136`, `ColorRangeMax@140`; flag bits 1 (colormap color) and 2 (RGBA color). The
    C++ struct with `offsetof` asserts, the shader struct and `Test.VisualizationOverlayPass.cpp`
    change together.
  - The renderer resolves the two new keys like the vector key (ScalarFloat / RgbaFloat4, count =
    ElementCount); a named key that does not resolve makes the packet unrenderable, as for anchors
    and vectors. It fills `ColormapID` from `ColormapSystem::GetBindlessIndex`.
  - Shader, reading by the glyph's source row (`row`, so live rows and the UI-081 order apply):
    `s` not > 0 or infinite draws nothing; otherwise the scale is `Scale × s` (an overflow to
    infinity is caught by the existing finite-tip check). Colormap: `s ≤ min` gives t = 0 and
    `s ≥ max` t = 1 exactly; between them `t = (s - min) / (max - min)` on operands scaled by
    2^-100 when an end exceeds 1e30 (or 2^100 below 1e-30), computed `precise`, and t = 0 if the
    width still is not positive (superseded halving: see the log, 2026-10-27); sampled at LOD 0; an infinite or NaN `s` or an invalid colormap index
    keeps the uniform color. RGBA: a negative component keeps the uniform color.
- UI (`DrawVectorFieldLayer`, `Sandbox.DomainPanels.cpp:610`):
  - "Length property" combo: Default (no property) + the domain's scalar candidates. The uniform field reads
    "Scale" while one is bound and stays enabled (it multiplies).
  - "Color property" combo: Default + scalar and vec3/vec4 candidates. The uniform color is disabled
    while one is bound and keeps its value (question 7); a scalar shows Colormap, Auto range and,
    when that is off, Min/Max.
  - `EditorVectorFieldDomainOption` gains one `StyleProperties` list (name, kind, compatible,
    reason); the layer row's status adds the length/color fallbacks and is shown whenever it is
    non-empty (today the panel shows it only while the vector is unavailable,
    `Sandbox.DomainPanels.cpp:629`).
- Command (`ApplyEditorGeometryVectorFieldCommand`): on Add, and on Update when a style property
  name changes, a non-empty name must satisfy the value rules, or be a pending GPU scalar
  (`EditorVisualizationEditingContext::PendingResidentScalar`, as the display request accepts
  one, `Runtime.VisualizationEditingOperations.Actions.cpp:1710`). An unchanged name that no
  longer resolves stays (its layer falls back), so other edits of that layer keep working.
  `BuildEditorVectorFieldModel` takes the same pending check, so a name that only a GPU preview
  carries shows "GPU preview" instead of a fallback. The panel lists CPU properties only; a
  GPU-only result is bound by the agent or after Accept.
- Agent (`Runtime.AgentOperations.Editor.cpp`), split like `attribute_bindings` /
  `bind_attribute` so the listing stays read-only: `vector_fields` returns the vector-field model
  (domains, vec3 and style candidates, layers with status); `vector_field` takes `action` add,
  update or remove. Add uses the command's defaults, as the panel does, and takes no fields;
  update merges the given fields (every recipe field, snake_case) onto the current layer. Each
  action is one command, so one undo step.
- Review blocks: A = recipe, validation, scene keys, extraction, packet, record, shader, renderer
  resolution, their CPU tests and the smoke; B = model, command check, panel rows, agent operation
  and their tests. One code review per block.
- Tests, red first (predictions in the log before the first run):
  - `Test.RuntimeSceneSerialization.cpp`: nondefault values of the new keys round-trip; a present
    but malformed key (wrong type, unknown colormap, min ≥ max) rejects the document. A layer
    without the new keys loads with the defaults; that already passes today, so its sensitivity
    is shown by a mutation that makes a new key required.
  - `Test.SandboxEditorVisualization.cpp`: validation rejects a bad colormap and bad ranges.
  - `Test.RuntimeVectorFieldExtraction.cpp`:
    - Values: a float length {1, 2, NaN, -1, 0} uploads {1, 2, inf, -1, 0}; a double 1e39 and an
      inexact uint32 2^24 + 1 become +inf; int32, uint64 and bool convert; an int32 color
      {0, 10, 5} gets the auto range 0..10, a constant {3, 3} widens to 2.5..3.5, an invalid
      value is left out of the range; vec3 colors clamp with alpha 1, a NaN component becomes
      (-1, …); vec4 keeps its alpha; the length buffer stays element-indexed under a glyph order.
    - Fallback: a removed property falls back (counter, no length key) and adding it back
      restores; replacing it by a property of another kind falls back.
    - Caches: editing the property rebuilds only its style cache; steady frames scan nothing; two
      distinct properties carrying the same revision token keep their own values; one scalar used
      for length and color uploads once; one vec3 used as vector and as color keeps both
      representations; five layers with distinct style properties (cache growth) all keep their
      values.
    - GPU preview: with an injected observer returning a front for the length property, the
      packet binds the front's address and no CPU length bytes are uploaded; the front's range is
      used for an auto color range; a new front address, stamp and range with every CPU revision
      unchanged is followed by the next packet; once the observer stops returning it, the CPU
      upload returns. A name only the front carries draws from the front and falls back (counter)
      when the front ends.
  - `Test.VisualizationOverlayPass.cpp`: size 144 and the new offsets; the record carries the new
    fields and flags; an unresolved length or color key skips the packet; the record's
    `ColormapID` is the colormap system's index for the packet's map (invalid while the system is
    not ready, if the mock device lets that state be observed).
  - Block B: the command binds a GPU-only name that `PendingResidentScalar` reports and the model
    shows it as a GPU preview, then as a fallback once the check fails; the model's style
    candidates per kind equal what the command accepts (vec2 and
    wrong-count properties excluded; a sibling of
    `VectorFieldModelOffersExactlyTheVectorsTheCommandAccepts`); the unchanged-name rule; the panel
    sequence for question 7 (`Test.SandboxDomainPanels.cpp`): set a uniform color, bind a color
    property, the uniform color is disabled, the length multiplier still edits, Default restores
    the remembered color; undo and redo restore whole recipes; the agent operation yields the
    panel's recipe and undoes in one step (`Test.AgentOperations.cpp`,
    `Test.SandboxAgentServer.cpp`).
- `gpu;vulkan` (`RuntimeSandboxAcceptanceGpuSmoke`, a new test beside the vector-field probes): a
  point cloud at z = 0 away from the reference triangle, identity transform, element 0 deleted (so
  an instance is not its row), width 4 px, uniform color magenta. Five layers (five records, so
  the 144-byte stride is read past record 0); each layer's vector is zero outside its own rows, so
  no layer draws over another's probes:
  - L1, Normalized, Length 0.1 along +x, length `p:len`, color `p:heat` through Jet with the
    manual range 10..20: element 0 (deleted) len 3, heat 20; e1 len 1, heat 10 (short, blue); e2
    len 3, heat 20 (covers anchor + 0.2 x, dark red); e3 len NaN, e4 len -1, e5 len 0 (nothing at
    the anchor, along +x or, for -1, along -x); e6 len 1, heat NaN (magenta).
  - L2, color `p:rgba` (vec4): e8 (0, 1, 1, 0.5) is blended between cyan and the background; e9
    (NaN, 0, 0, 1) draws magenta.
  - L3, Raw, Length 0.2, vector (0.5, 0, 0) on e11 only, length `p:len` = 2 there (shared with
    L1, one upload): the arrow covers anchor + 0.15 x (0.1 long without the multiplier).
  - L4, color `p:huge` with auto range on e12-e14: values -3e38, 0, +3e38 (0 everywhere else)
    draw Jet's blue, middle green and dark red.
  - L5, color `p:tiny` with the manual range [a, b], a and b adjacent normal floats (bit patterns
    0x00800003 and 0x00800004), on e15-e16: a draws blue, b dark red.
- Mutations (each must fail a named check): the shader ignores the length buffer; reads length by
  instance instead of row; reads color by instance instead of row; draws an invalid, zero or
  negative length; applies the multiplier in Normalized mode only; ignores the color buffer;
  skips the range normalization; normalizes without the overflow halving (L4's middle value);
  halves always and drops the endpoint mapping (the first delta's shader, L5). Each of the last two
  protections alone is masked by the other on L5 (the width of adjacent floats there is a denormal
  whose flushing is hardware-defined), so it is recorded as not separately observable; skips the invalid-scalar check;
  drops the RGBA sentinel check; forces RGBA opaque; the renderer leaves `ColormapID` invalid; the
  auto range includes invalid values; the style cache ignores the revision; the style caches live
  in a `std::vector` (only under a sanitizer or by an asserted pointer check, else recorded as not
  observable); the GPU front is not observed; a new scene key is required.

### Slice 5 — combined and live check
- Run every slice's `gpu;vulkan` smoke together on the final tree. Then do a live Sandbox check
  through the `intrinsic-sandbox` MCP: menus, the three sections on a mesh, a graph and a point
  cloud, and a vector field with length and color properties. GRAPHICS-158 (rebound-attribute
  Vulkan evidence) receives the per-lane cases if it is picked up first.

### Order and checkpoints
- Slices land in order 1 → 2 → 3a → 3b → 4 → 5. Each slice carries its own smoke. After each
  slice the session stops and reports before starting the next (drift budget; the operator
  asked to be consulted before the renderer slices). Every slice gets a ponytail review plus an
  independent correctness review bound to its diff.

## Acceptance criteria
- [x] Every processing method sits in one of the nine groups under Mesh, Graph and PointCloud
      `> Processing` and under `View > Processing`; Geodesics is
      `Mesh > Processing > Geodesics > Virtual Source Propagation`.
- [ ] Vertices lists Position, Normal, Color, Point size; Edges lists Position, Color, Line width;
      Faces lists Position, Normal, Texcoord, Color; each row binds its own primitive from the
      source domains of the slice 2 table.
- [x] A uniform color/size/width edit is enabled only while its row is on Default and keeps its
      last value across a bind and Default. Color Default follows the slice 2 precedence:
      picked uniform, then imported color, then grey-white.
- [ ] Binding a Position or Normal on one primitive leaves the other primitives unchanged;
      position fronts, picking, highlights and bounds follow the slice 3a rules.
- [ ] A vector-field layer takes an optional length property (length = uniform × value) and an
      optional color property (scalar via colormap, vec3/vec4 as RGB), with the slice 4
      invalid-value rules.
- [ ] UI, agent tools and scene save/load reach the same commands; undo/redo is one step each.
- [ ] Operational on Vulkan: each slice's `gpu;vulkan` smoke passes and the live Sandbox check is
      recorded (or the remaining cases are handed to GRAPHICS-158 with a named owner).

## Verification
```bash
cmake --build --preset ci --target IntrinsicSandboxEditorIntegrationTests IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure --timeout 60 \
  -R '^(SandboxEditorPresentation|SandboxEditorGeodesics|SandboxProcessingPanels|SandboxDomainPanels|SandboxAgentServer|VertexChannelBindings|SandboxEditorUi|AgentOperations|RuntimeSceneSerialization|SceneDocumentModule|RuntimeRenderExtraction|RuntimeVectorFieldExtraction|VisualizationRecipes|VisualizationOverlayPassContract|PositionPreviewExtraction|PrimitiveSelection|PrimitiveSelectionRefinementWiring|MeshGeometryExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction)\.'
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
# Smokes (Vulkan host; one heavy build at a time):
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120 \
  -R 'VectorField|PointLane|PerLane|VisualizationOverlay'
```

## Log
- 2026-10-23: Plan counter-review (Claude author → Codex reviewer, `codex exec`, codex-cli
  0.163.0, requested `gpt-6-astra` at `xhigh`, read-only sandbox; thread
  `01a1685e-1f0a-73e3-adcf-cddf74bc8a5c`). Object: this note, sha256
  `53004043a20e5a3745d3533ffc5c6ca6a4630410af57bcf6c3ef411b34491431`, base `f849aafb1`.
  Recursion protection instruction-only: other MCP servers were not disabled. Verdict
  "revise", with 7 blocking and 4 optional findings: per-lane Default precedence (B1),
  per-source position fronts (B2), picking lane state and invalidation (B3), the lane × source
  matrix (B4), vector-field scalar kinds and invalid values (B5), vector-field agent parity
  (B6), and missing verification suites (B7). O1-O4 covered smoke placement and spikes,
  explicit per-lane slots, Color combinations and the record reserve. All were folded into
  this revision.
- 2026-10-23: Delta round 2 (same thread, revision sha256 `e272084a…`). Verdict "revise":
  B1, B2, B4-B7 and O1-O4 resolved. B3 was still blocking: the selection mode does not say which
  lane a GPU hint came from, so a Surface hit could be compared against displaced points. Fix:
  refinement uses the producing lane from the encoded id's domain, the fallback uses the
  request's target lane, and highlights use the stored selection's domain, with a
  separated-lane fixture. Optional: arrow length removed from the disabled-while-bound default.
- 2026-10-23: Delta round 3 (same thread, revision sha256 `dc6e46e6…`). Verdict verbatim:
  "approve. Still blocking: none. B3 is resolved at plan level: GPU-hit refinement uses the
  producing lane, fallback uses the issuing request's target, and highlights use the stored
  selection's domain." Implementation and runtime behavior are unverified.
- 2026-10-23: Operator: saved scenes must keep loading. Slice 3a now keeps the bare binding
  keys for each entity's main primitive and adds prefixed keys for the other lanes. This
  replaces the round-1 O2 choice of renaming every key, which would have made older scenes with
  a bound attribute fail to load. The change was not re-sent to the reviewer: it is the
  operator's decision and narrows the codec change.
- 2026-10-23: Slice 1 implemented (grouped processing menus).
  - Every processing registration in `Sandbox.MeshProcessingPanels.cpp` and
    `Sandbox.MethodPanels.cpp` sits at `{<menu>, "Processing", <group>}`. Geodesics is
    `Mesh/Processing/Geodesics`. The normals entries are "Vertex Normals" and "Face Normals"
    under Normals. Two titles are renamed: `Mesh / Processing / Virtual Source Propagation` and
    `Mesh / Processing / Scalar Field Gradient`. Window ids are unchanged.
  - `RegisteredMenuChildren` (exported from `Sandbox.EditorShell.cppm`) now owns the
    child-name loop of the menu tree. Under a `Processing` parent it orders the groups by a list
    local to `Sandbox.EditorShell.cpp`.
  - Deviation from the slice plan: there are no shared group-name constants. Panels use
    literals, and `ProcessingMenusListTheirGroupsInMenuOrder` fails if any processing window
    names a group outside the list.
  - Docs naming the old paths are updated.
  - Verification: every CPU test of `IntrinsicSandboxEditorIntegrationTests` passes (510/510).
  - Mutation checks: without the sort the order test fails (registration order); with the sort
    at every depth `MenuChildrenSortOnlyProcessingGroups` fails.
  - Layering, doc links, docs sync, task policy and `SourceRules` pass; the module inventory is
    unchanged; jscpd finds no clone in the changed ranges.
  - The live menu in a running Sandbox is not checked (slice 5).
- 2026-10-23: Slice 1 code review (Claude author → Codex reviewer, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a1688d-73de-77e2-b443-1417368820cb`;
  recursion protection instruction-only).
  - Ponytail (author): no findings.
  - Round 1 on diff sha256 `29b13f18…`: "revise required". C1: two docs still named old paths.
    C2: the METHOD-040 instructions pointed to Curvature instead of Curvature Segmentation. C3
    (optional): the sort ran at every menu depth. All fixed; C3 got a red-first test.
  - Round 2 on `cd2b7acb…`: C1-C3 resolved. New C4: the Progressive Poisson menu test still
    expected the old paths; the author's suite filter had missed that suite. Fixed, and
    verification widened to the whole executable.
  - Round 3 on `83f54e7c…`: verdict verbatim "approve. Still blocking: none".
- 2026-10-23: Operator decision (slice 2): Default is grey-white for the surface only. Edges and
  points keep their dark default (`0.02`), so a wireframe or point overlay stays visible on a
  grey-white surface. Imported colors still win on every primitive.
- 2026-10-23: Slice 2 implemented (lane-keyed attribute rows, Default color precedence,
  remembered uniforms).
  - `RenderLaneAttributeRows()` (`Runtime.VertexChannelBindings`) is the lane × attribute ×
    domain table. Model rows, commands (`EditorAttributeBindingCommand::Lane`), the panel
    sections and the agent tools are keyed by it.
  - Color binds the chosen lane's overlay (`EditorVisualizationRecipeCommand::Target`).
    `show_property` uses `DefaultColorLaneFor(domain)`, the one default lane mapping, which
    `ColorOverlayTargetFor` now uses too.
  - Position/Normal/Texcoord rows still write the one shared structural slot until slice 3a.
    Their Default text says "shared by all primitives", and Default clears a source on any
    domain of the row.
  - Colors: `kDefaultSurfaceColor` (0.9 grey) and `kDefaultLineAndPointColor` (0.02) in
    `Graphics.Component.VisualizationConfig`. Untextured surfaces get the grey base color; an
    albedo texture keeps a white factor so it shows as imported. Edge and point lanes on Default
    draw `v:color`/`p:color` when present (an extraction-made per-vertex overlay). A lane's
    uniform color starts at what the lane draws.
  - Remembered uniforms: `RenderPoints::Size` + `SizeProperty` and `RenderEdges::Width` +
    `WidthProperty` replace the `std::variant`. The scene codec stores the uniform as `value` in
    the property form, and older files without it load the component default. A uniform
    size/width command only sets the uniform; unbinding is the attribute Default.
  - UI: uniform color/size/width fields are always drawn and disabled while bound. ImGui 1.92.5
    still lets a disabled widget be activated by id, so a bound field never writes
    (`changed && !bound`).
  - Agent: `attribute_bindings` groups rows by `lane` with `domains`, and `source` is
    `{name, domain}`. `bind_attribute` takes `lane` and an optional `domain`, needed only when
    the name exists on several of the lane's domains. Docs: `agent-control-lane.md`, Sandbox
    README, `rendering-target-architecture.md`.
  - Verification: all CPU tests of `IntrinsicRuntimeContractTests`,
    `IntrinsicRuntimeGraphicsCpuTests`, `IntrinsicGraphicsContractCpuTests`,
    `IntrinsicGraphicsContractTests` and `IntrinsicSandboxEditorIntegrationTests` pass
    (2734/2734).
  - Mutation check: ten targeted tests each fail with their mechanism removed. The mechanisms
    are the imported edge/point color, grey-white base, lane starting color, remembered size
    on Default, `value` in the codec, lane target of Color, a lane row, the bound-field write
    guard and the lane-domain refusal.
  - The five GPU smoke sources with mechanical `Size`/`Width` renames compile only in
    `ci-vulkan` and were not compiled here.
- 2026-10-23: Slice 2 code review (Claude author → Codex reviewer, `codex exec`, requested
  `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a168c5-1fb3-7293-93b4-f1b81a8ba052`;
  recursion protection instruction-only).
  - Ponytail (author): one finding, the per-frame base-color patch would overwrite authored
    factors once GRAPHICS-105 projects them. Fixed: only the two defaults are switched.
  - Round 1: "revise required".
    - P1: the diff carried ARA edits from another session in this checkout; they were
      unstaged.
    - C1: albedo textures from presentation slots or a baked Appearance texture got the grey
      factor. Fixed: the factor is decided after all texture sources, from the resolved
      `AlbedoID`.
    - C2: same-named properties on two domains shared one property-buffer key. Fixed:
      `DomainKeyLane` adds the domain to the key.
    - C1 and C2 each got a regression test that fails with its fix removed.
  - Round 2: verdict verbatim "approve. Still blocking: none. New findings: none".
  - Full CPU suite 5876/5876 after the fixes.
- 2026-10-25: Slice 3a implemented (independent Position per primitive) on base `d3bec9527`.
  - `VertexChannelBindingSet` holds `Surface`/`Edges`/`Points` slots (`VertexChannelLaneBindings`).
    `ResolveDisplayedPositions`, `FindVertexChannelSourceBinding` and the plan builders take the
    lane; `PrimaryLaneFor`, `SelectionLaneFor` and `DisplayedPositionLanes` (the drawn lanes, or the
    primary lane when none is drawn) carry the lane rules. Default clears the lane's own slot.
  - Graph: while the points lane displays other positions than the edges lane, the point
    instance draws its own points-only upload (residency sub-key 1, rebuilt on revision or
    binding change, fail-closed). Accept on such a graph is not acknowledged and goes through
    the ordinary upload.
  - Position fronts are observed per displayed lane (a shared source once) and handed only to
    that lane's blocks. When one lane's preview ends, all blocks are restored and the remaining
    lanes are handed their front again; the graph point upload is rebuilt on restore.
  - Picking: face/edge/point hints refine against Surface/Edges/Points; a missing hint uses
    the vertex rule (`SelectionLaneFor`). Highlights use the stored selection's domain. The
    pick stamp records every lane's displayed revision.
  - Culling bounds, F framing and the gizmo pivot use the union of the displayed lanes, cached
    by `DisplayedPositionsSignature` (empty, so never trusted, while a source has revision 0).
    Vector-field anchors, glyph order and the default arrow length use the primary lane.
  - Scene keys: bare `position`/`normal`/`texcoord` for the primary lane, `edges.position`,
    `points.position`, `points.normal` (and any `<lane>.<attribute>` of a row) for the others;
    an unknown or duplicate slot rejects the document. Scene version stays 6.
  - `AnyVertexChannelBindingEnabled` and the asset signature cover every lane slot.
  - Deviations: no separate spike (the separated-lane pick fixture is the test); preview
    bounds stay entity-wide (unbounded while any lane previews); a point upload created during
    a preview gets its front one frame later (`shortcut:` comment in `Runtime.RenderExtraction.cpp`).
  - Tests: per-lane variants of the mesh and graph extraction tests (independence, union culling,
    own point upload and its reuse), two fronts with partial restore, graph point upload restored
    after a preview, separated-lane refinement (face, point, missing hint with and without
    points), highlight lane, pick stamp of a secondary lane, focus/pivot union, lane scene keys
    (round trip, older bare-key scene, duplicate and invalid keys), agent `bind_attribute`
    leaving the points lane canonical.
  - Mutation checks: ten mechanisms each removed (per-lane fronts, union bounds, hit-lane
    refinement, all-lane stamp, duplicate-key rejection, all-lane `AnyVertexChannelBindingEnabled`,
    graph point split, highlight lane, focus/pivot union, point upload restore); each makes its
    test fail.
  - Verification: all tests of `IntrinsicRuntimeContractTests`, `IntrinsicRuntimeGraphicsCpuTests`,
    `IntrinsicGraphicsContractCpuTests`, `IntrinsicGraphicsContractTests` and
    `IntrinsicSandboxEditorIntegrationTests` pass (2798 passed, 2 native-window tests skipped).
    Task policy, doc links, layering, test layout, root hygiene, docs sync and SourceRules pass;
    jscpd finds no exact clone (≥ 6 lines) in the changed lines.
  - Not done: the slice's `gpu;vulkan` smoke (points at a bound position, surface canonical,
    pick at the point pixel) and the live Sandbox check. The slice is CPU-contracted only.
- 2026-10-25: Slice 3a code review (Claude author → Codex reviewer, `codex exec`, codex-cli
  0.163.0, requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread
  `01a17080-6083-7341-8c0e-6e374141e0b0`; recursion protection instruction-only).
  - Ponytail (author): `PositionPreview` duplicated the per-lane flags (which let Accept leave a
    stale lane flag); it is now derived from them.
  - Round 1 on diff sha256 `413dd41e…`: "revise required", four blocking findings. B1: a missing
    hint ignored the issuing selection's target lane; `PickReadbackContext::TargetDomain` now
    carries it. B2: a hidden primary lane widened bounds, framing, pivot and preview state;
    `DisplayedPositionLanes` now returns the drawn lanes (primary only when none is drawn). B3:
    prefixed keys aliased primary slots; they are rejected. B4: the preview test checked only
    aggregate counters and no graph Accept; it now uses a seam-split surface so row counts tell the
    routing, and Accept on a split graph is asserted `NotOneToOne`. Optional ponytail: the bounds
    cache reuses `ComputeEntityLocalDisplayBounds`. Each fix has a mutation check that fails its test.
  - Round 2 on `56060d1f…`: B1-B3 resolved; B4 still blocking (the surface could take the points
    front unnoticed). Fixed with a surface-only three-row front step; mutation-checked.
  - Round 3 on `c070cc52…`: verdict verbatim "approve" ("Approval covers the CPU-contracted slice";
    "Remaining blocking findings: none. New findings: none."). The reviewer notes that the missing
    Vulkan smoke prevents an Operational claim until it runs or its deferral/handoff is recorded.
  - After the fixes: all tests of the five owning executables pass (2798 passed, 2 native-window
    skips).
- 2026-10-25: Slice 3a merged to main and pushed (`b8bdf73c5`, fast-forward). `build/ci-vulkan`
  rebuilt (it was a month old); the slice 2 smoke sources compile.
  - Vulkan runs (host `hochvogel`, NVIDIA GeForce RTX 4090, driver 580.159.04, X11 `:1`,
    1920x1080 window, VSync off): `IntrinsicPointLBVHGpuTests` 55 passed, 1 skipped;
    `IntrinsicRuntimePointCloudConsolidationGpuParityTests` 10 passed;
    `IntrinsicGraphicsVulkanSmokeTests` 36 passed; `IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests`
    42 passed (the new test filtered out).
  - New smoke `RuntimeSandboxAcceptanceGpuSmoke.BoundPointPositionsDrawWhileTheSurfaceStaysCanonical`:
    binds the ReferenceTriangle's points lane through `ApplyEditorAttributeBindingCommand` to
    positions shifted +1 in x; red points appear at all three shifted vertices, none at the
    canonical apex, the surface's centroid stays covered and the shifted centroid stays
    background. Passed twice (about 4 s each). With the vertex view reading the surface lane
    (mutation of `Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp`) it fails at the shifted
    apex, the shifted bottom-right vertex and the canonical apex; reverted.
  - The planned click half fails before lane-aware pick refinement is reached: the click reads back no hit
    (`hits=0 noHits=1`). Cause and evidence are in BUG-254. Operator decision on selection
    highlights recorded above; Slice 3c added.
- 2026-10-25: Review of the smoke, BUG-254 and this log (Claude author → Codex reviewer, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a1738b-56cd-7642-969d-eeec5a957c9c`;
  recursion protection instruction-only). Round 1: "revise required"; the smoke had no blocking finding,
  BUG-254 named the wrong cause (the pick ID pipelines' depth-equal test against the surface-only depth
  prepass was missing) and overstated "never hits". Round 2 on diff `dd36933d…`: "approve", "No remaining
  findings."
- 2026-10-25: The click half of the slice 3a smoke is covered by BUG-254 slice A: in
  `RuntimeSandboxAcceptanceGpuSmoke.ClicksPickTheNearestDrawnPrimitiveOfTheSelectionMode` a
  Vertex-mode click on a mesh point drawn from a bound Points position resolves to the canonical
  vertex 2 with its hit at the bound position, and a mesh drawn from bound edge positions is
  picked where its edges are drawn (RTX 4090, driver 580.159.04).
- 2026-10-26: Slice 3c plan review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a179f4-0740-7f23-864b-8511985e9004`;
  recursion protection instruction-only), on the plan at `407218b32`. Verdict verbatim "revise".
  Blocking, folded in: distinct positions on all three lanes so dropping a lane cannot hide behind
  dedup; a graph fixture for nodes, edges and halfedges (today `SelectionLaneFor` puts a graph
  halfedge on the points lane). Optional, followed: document and assert the two fallbacks; dedup
  by resolved property name with null bindings allowed and `ResolveEntityDisplayedPositions`'
  early return kept; an orange predicate valid after tone mapping.
- 2026-10-26: Slice 3c implemented.
  - `BuildPrimitiveSelectionRenderSnapshot` highlights a selection on every drawn lane
    (`DisplayedPositionLanes`) that shows its domain (`LaneShowsDomain`, anonymous namespace in
    `Runtime.SceneInteractionModule.Selection.cpp`), once per distinct displayed source; a domain
    no drawn lane shows keeps the `SelectionLaneFor` lane. `ResolveDisplayedPositionSources`
    (`Runtime.VertexChannelBindings`) holds the distinct-source loop that
    `ResolveEntityDisplayedPositions` used before (its no-bindings early return kept).
  - Tests: `PrimitiveSelection.HighlightShowsTheSelectionOnEveryDrawnLane` and
    `.GraphHighlightsNodesOnBothLanesAndEdgesOnTheEdgesLane` fail on the previous code in exactly
    the vertex, mesh-edge, face-order, dedup, graph-node and graph-halfedge assertions (the old code
    put a graph halfedge on the points lane); the graph-edge and face-fallback checks passed before,
    as predicted. Targeted mutations show the dedup, face-fallback and face-routing checks sensitive:
    dedup removed (same-source check fails), fallback removed (face fallback fails), faces on every
    lane (face checks fail); the graph-edge check has no mutation of its own.
    `IntrinsicRuntimeContractTests` 1796/1796, `IntrinsicRuntimeGraphicsCpuTests` 72/72.
  - Vulkan: `RuntimeSandboxAcceptanceGpuSmoke.BoundPointPositionsDrawWhileTheSurfaceStaysCanonical`
    selects the ReferenceTriangle's apex vertex and finds the highlight at the canonical apex and at
    the shifted apex. The first run measured the highlight at (245, 235, 136) after tone mapping
    (surface (245, 245, 245), background (170, 203, 231)); the provisional orange predicate was
    replaced by R > 200, G > 180, G > B + 60, recorded with the measurements. Passed three times;
    mutation "single `SelectionLaneFor` lane" fails only the canonical-apex highlight, as predicted;
    `RuntimeSandboxAcceptanceGpuSmoke` 46/46 (RTX 4090, driver 580.159.04, X11 `:1`).
  - jscpd: no new clone; duplicated lines fell by three with the shared helper.
- 2026-10-26: Slice 3c code review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17a0a-5144-7cb3-9ec1-14191995919d`;
  recursion protection instruction-only), on the staged diff sha256 `157b9758…`. Verdict verbatim
  "approve" ("Ponytail: no complexity findings."). Optional findings applied: a mesh halfedge
  (index 3, edge 1) is checked on both sources with its endpoints; the mutation claim above names
  the checks it covers. `PrimitiveSelection` passed again after the test addition.
- 2026-10-27: Slice 3b plan review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17a72-01f7-7402-b8ac-721fbc2ca385`;
  recursion protection instruction-only), on the plan diff sha256 `5dd9e6e7…` at `9a845417f`.
  Verdict verbatim "revise". Blocking, folded in: the reference camera's view rotation is the
  identity, so the smoke poses a rotated camera and checks the click geometry against the "no
  camera transform" normal as well; a camera-facing Surfel gives the zero-normal Surfel a colour
  reference, so a missing length guard fails either way; extraction sequences cover edits of a
  bound or canonical normal without dirty tags (graph shared and split geometry), removal and
  re-adding, and a bound source that disappears and returns. Optional, followed: per-domain
  Default text; a finite-length check. Not followed: asserting partial versus full uploads; the
  sequences assert the uploaded bytes after each step, which both mechanisms must produce.
- 2026-10-27: Slice 3b red-first predictions, written before the first run against `9a845417f`'s
  implementation (the direct `ResolvePointNormalSource` test is left out of that run: the function
  is new and would not compile):
  - `VertexChannelBindings.TableListsEveryAttributeDomainRowWithItsTypeRule` fails on the
    `PointCloudPoint` Normal row only (canonical "" instead of `p:normal`).
  - `PointCloudGeometryExtraction.PointNormalsFollowTheirSourceWithoutDirtyTags`: fails on the
    initial upload, the edit, the re-added `p:normal` and the fallback after `p:alt` is removed (no
    stream without a binding); passes on the removal, the bound `p:alt` and the re-added `p:alt`.
  - `GraphGeometryExtraction.BoundNodeNormalsFollowEditsOnSharedAndSplitPointGeometry`: the four
    normal checks while bound fail (the degenerate node uploads +Z); the unbound check passes.
  - `MeshPrimitiveViewExtraction.VertexViewUploadsThePointNormalSourceAndFollowsIt`: every vertex
    view check fails (no normal stream); both edge view checks pass.
  - Result of that run: exactly as predicted (table: the `PointCloudPoint` row only; cloud: initial,
    edit, re-added and fallback checks; graph: the four bound checks; mesh: the four vertex view
    checks). With the change all five tests pass, including the direct resolver test.
  - Predictions for `RuntimeSandboxAcceptanceGpuSmoke.SurfelsAreDrawnAndClickedAsTheDiscTheirNormalDescribes`
    with the change: every click and pixel check passes; the zero-normal Surfel's centre is within 6
    of the facing Surfel's colour and the tilted centre's red is at least 8 lower (provisional
    thresholds, to be measured). Mutations: no shader read (today's behaviour) and no camera
    transform fail the short-axis click; `mat3(Model)` fails the long-axis click; no length guard
    fails the corner click or the zero/facing colour check; normals read in Flat mode fail the Flat
    click.
- 2026-10-27: Slice 3b implemented.
  - `ResolvePointNormalSource` (`Runtime.VertexChannelBindings`) picks the points lane's normal:
    a bound `Points.Normal` on the vertex domain (vec3, one value per element), else the table's
    canonical one (`v:normal`, now `p:normal` for cloud points, none for graph nodes).
    `PrepareBoundVertexChannels` uploads it normalized with zero for a degenerate or non-finite
    element (was object-space +Z); the mesh vertex view now calls it and passes `NormalBytes`.
    The graph/cloud revision snapshot tracks the resolved source; the vertex view keeps and
    compares its `Normal` revision. `ToAttributeSourceType` and `BindingMatches` lost their last
    callers and are removed.
  - `GpuPointQuadCornerAt` (`point_quad.glsl`) reads the normal, maps it by the inverse-transpose
    model matrix and the camera view, and keeps +Z unless the length is finite and positive; the
    draw and the pick share it. The Points Normal row's default text names the source per domain.
  - Smoke measurement: the first run passed every click and pixel check but measured the tilted
    Surfel at (241, 0, 0) against (245, 0, 0) facing the camera: tone mapping flattens full red,
    so the provisional "8 lower" check failed. Recalibrated with a uniform red of 0.1: facing
    (170, 0, 0), zero normal (170, 0, 0), tilted (156, 0, 0), Flat (170, 0, 0); the thresholds
    (zero within 6 of facing, tilted at least 8 lower) are kept and the colour is recorded in the
    test.
  - GPU mutations, each fails the smoke where predicted: no shader read (short-axis click hits,
    tilted not darker); no camera transform (short-axis click hits, colours off); `mat3(Model)`
    (long-axis click misses); no length guard (the zero-normal Surfel turns black and its square
    corner is hit: NaN); Flat reading the normal (Flat click misses).
  - CPU mutations, each fails the extraction tests: vertex view without normals (mesh, all four
    vertex view checks); vertex view without its normal revision (mesh edit and rebinds); revisions
    tracking only a bound normal (cloud edit, removal and re-add; mesh edit); degenerate normals
    as +Z (graph four checks, cloud initial and edit, mesh bound check).
  - `jscpd`: the smoke's click-pixel check duplicated `ClickFootprintIsTheDrawnLineWidthAndPointShape`;
    it moved into `Bug254ClickCapture::ExpectClickPixelsShowTheHits`, used by both. The three
    extraction tests keep small local `expected`/`normalsOf` lambdas (test-local harness style).
  - CPU gate (`build/ci`, `-LE 'gpu|vulkan|slow|flaky-quarantine'`): 5904/5904. Layering, task
    policy, session brief, doc links, docs sync, source rules, test layout and compiler hazards
    pass.
  - Vulkan (host `hochvogel`, NVIDIA GeForce RTX 4090, driver 580.159.04, X11 `:1`, backbuffer
    1600x900, VSync off): every `gpu;vulkan` test of `build/ci-vulkan`, 167/167, including the new
    smoke and `ClickFootprintIsTheDrawnLineWidthAndPointShape` with the shared pixel check.
- 2026-10-27: Slice 3b code review (Claude author → Codex reviewer, fresh session, `codex exec`,
  requested `gpt-6-astra` at `xhigh`, read-only sandbox, thread `01a17a9d-b7e0-75f0-b369-94e6ff1de1ef`;
  recursion protection instruction-only), on the staged diff sha256 `5653eb28…`. Verdict verbatim
  "revise required". Blocking: (1) property revision tokens coalesce per epoch
  (`PropertyRevisionState::MarkModified`), so two sources written together share a token and a
  rebind or a fallback between them went unnoticed (mesh vertex view rebind; cloud fallback after
  the bound source is removed); (2) `inverse()` is undefined for a singular model matrix; (3) the
  absolute 1e-6 length cutoff turned valid normals camera-facing under a large uniform scale.
  Ponytail: read the normal only in Surfel mode. Optional: two stale comments.
  - Fixes: the revision snapshot records the hashed name of the resolved normal source
    (`NormalSource`), compared wherever `Normal` is; the shader uses the cofactor matrix (the
    inverse transpose times the determinant, defined for singular matrices; the sign does not
    matter for a two-sided Surfel), rescales by the largest component before normalizing, and
    reads the normal only for Surfels; both comments updated.
  - New cases: the mesh test binds `v:tilted` while it shares `v:normal`'s token, the cloud test
    falls back from `p:alt` to `p:normal` sharing a token (both premises asserted); the smoke adds
    a Surfel cloud scaled by 2e6 and one flattened onto z = 0 (drawn along the long axis,
    background 4 px past the short half-axis of the expected disc).
  - Predictions before running: without `NormalSource` the mesh test fails at the equal-token bind
    and the cloud test at the equal-token fallback, the other steps pass; with the previous shader
    (inverse transpose, 1e-6 cutoff) both new smoke discs fail their short-axis sample; with the
    fixes every test passes.
  - Results, as predicted: without `NormalSource` the mesh test fails at the equal-token bind
    and the cloud test at the equal-token fallback only; with the previous shader the smoke's
    scaled and flattened discs are drawn past their short axis, (170, 0, 0) and (170, 1, 2); with
    the fixes all `VertexChannelBindings` and new extraction tests pass (22) and the smoke passes
    twice. Shader mutations on the final shader, each failing the smoke: no normal read
    (short-axis click hits, tilted not darker, both new discs too wide); no camera transform
    (short-axis click, colours, both new discs); model matrix instead of the cofactor matrix
    (long-axis click misses, a new disc too wide); no zero guard (the zero-normal Surfel turns
    black and its corner is hit). The Surfel-only gate in `GpuPointQuadCornerAt` and the Flat
    branch of `GpuPointDiscMetric` each keep Flat points round, so removing one of them alone is
    not observable; the Flat click covers the pair.
  - Gates on the fixed tree: CPU 5904/5904; Vulkan `gpu;vulkan` 167/167 (RTX 4090, driver
    580.159.04, X11 `:1`).
- 2026-10-27: Slice 3b delta review (same Codex thread `01a17a9d-b7e0-75f0-b369-94e6ff1de1ef`,
  read-only, recursion protection instruction-only), on the staged diff sha256 `eb47053d…`.
  Verdict verbatim "approve" ("Remaining blocking findings: None."). Nonblocking, followed: the
  scaled cloud uses 1e12 instead of 2e6, so the squared length of its cofactor normal overflows a
  float and the largest-component rescale is tested (without it the disc is drawn black, NaN;
  with it the smoke passes); the original shader plan paragraph is marked superseded.
- 2026-10-27: Final check of slice 3b (same Codex thread), on the staged diff sha256 `d23927ab…`
  (the approved diff plus the two followed nonblocking findings). Verdict verbatim "approve"
  ("Approval stands for staged diff …", "No new findings.").
- 2026-10-27: Slice 4 plan review (Claude author → Codex reviewer, `codex exec`, codex-cli
  0.163.0, requested `gpt-6-astra` at `xhigh`, read-only sandbox; thread
  `01a17bfe-32c2-7de0-842a-da03917913ce`; recursion protection instruction-only). Object: this
  note at sha256 `d48ab667…`, base `215432c01`. Verdict "revise", four blocking findings: the
  colormap normalization overflows for a finite range such as ±3e38 (B1); cache entries can be
  invalidated while a second one is added, and shared-property cases are untested (B2); several
  planned mutations survive the smoke (identity range, no invalid RGBA, Normalized only, alpha
  1) (B3); GPU scalar previews are not observed, against `geometry.property-coherence` (B4).
  Optional: keep the steps, label the unbound choice Default, test the question-7 panel sequence
  with undo and redo, show fallback status while the vector resolves, separate compatibility
  checks from red-first ones. All were folded into the plan above.
- 2026-10-27: Slice 4 plan delta round 2 (same Codex thread), on sha256 `f1ba0381…`. Verdict
  "revise": B2 and B3 resolved; B1's unconditional halving collapses a valid tiny range (adjacent
  floats 0x00800003/0x00800004); B4 lacks the command boundary (a GPU-only scalar cannot be bound,
  and the status cannot tell a front from a fallback) and the observer test must change the front
  with CPU revisions unchanged. Nonblocking: keep the smoke layers spatially independent. Folded
  in: halving only on overflow with explicit endpoint mapping and a tiny-range layer; the command
  and model take `PendingResidentScalar`; the observer test changes address, stamp and range.
- 2026-10-27: Slice 4 plan delta round 3 (same Codex thread), on sha256 `f21f9a5e…`. Verdict
  verbatim "approve" ("B1 and B4 are resolved at plan level. No remaining or new blocking
  findings."). Implementation and Vulkan behavior unverified.
- 2026-10-27: Slice 4 block A, red-first predictions (written before the first run; the new
  recipe, packet and record fields exist, no behavior yet):
  - `RuntimeSceneSerialization.VectorFieldStyleKeysRoundTripAndStayOptional`: the round trip
    fails (keys not written, defaults restored); the older-scene load passes (already true today);
    all six malformed-key loads succeed instead of failing.
  - `SandboxEditorUi.VectorFieldCommandRejectsBadColormapsAndRanges`: all five rejections are
    Applied instead, and the layer differs from the base afterwards.
  - `RuntimeVectorFieldExtraction`: the four new tests fail wherever a length/color buffer, mode,
    range, fallback count or second style-cache reuse is expected; checks that hold with no style
    path (uniform mode, zero addresses, equal tokens, vector buffer) pass.
  - `VisualizationOverlayPassContract.VectorFieldRecordsCarryLengthAndColorBuffers`: both
    unresolved packets count as renderable and four records are written (stops there).
    `RendererResolvesVectorFieldStyleBuffersAndColormap`: no length address, the wrong-count
    packet is renderable; the colormap index check fails only if the mock reports the colormap
    ready.
  - Red results, as predicted: serialization (round trip, six malformed keys), command (five
    rejections, layer changed), the four extraction tests (no style buffers, modes, ranges,
    fallbacks or second reuse; equal tokens and vector buffers held) and both overlay tests
    (unresolved packets renderable, four records; no length address, wrong-count packet
    renderable). The mock reports the colormap ready (Jet index 4), so the index check failed too.
  - With block A implemented the eight tests pass, and the task's CPU suites pass 737/737.
  - GPU predictions for the new smoke on a shader that only carries the 144-byte record (no
    length or color reads): e1 not blue; e2 not red and not 0.3 long; e3-e5 drawn at the anchor
    and +0.05 (not at -0.05); the Raw arrow ends at 0.1; the half-cyan sample fails its upper R
    bound and both cyan checks; L4 and L5 show magenta (ends fail, the middle fails only its G
    check); five records. With the full shader every check passes.
  - GPU red on the record-only shader (RTX 4090, driver 580.159.04, X11 `:1`): as predicted
    except one sample: e1 magenta (245, 0, 245); e2 ends at 0.1 (background at 0.2); e3-e5 drawn
    at +0.05; the Raw arrow ends at 0.1; the half-cyan sample magenta; L4 and L5 magenta. Wrong
    prediction: the anchor samples of e3-e5 stayed background, because a uniform shaft starts
    exactly at the anchor (only a cap, as for a zero length, covers it).
  - GPU with the full shader: every check passed except L4's middle value, which drew Jet's low
    end (0, 0, 236). The CPU range was right (-3e38..3e38, probed). Halving on an infinite width
    and halving on large ends both failed the same way; forcing t = 0.5 for that range drew the
    middle green, and a branch probe showed the range finite and the scaling condition true. The
    NVIDIA compiler cancels a common factor out of `(v·k - lo·k) / (hi·k - lo·k)` and divides
    the overflowing original. Fix: operands scaled by 2^-100 (or 2^100 for tiny ranges) and the
    offset and width computed `precise`. With it the new smoke and all ten vector-field and
    overlay `gpu;vulkan` tests pass.
  - The explicit endpoint mapping was removed: with the power-of-two scaling the quotient maps
    both ends exactly, and the smoke stays green without it.
  - Block A mutations, each against the new tests. Shader (the new smoke, 14 of 14 detected): no
    length read; length or color read by instance instead of row; invalid, zero or negative
    lengths drawn; the multiplier only in Normalized mode; no color read; no range normalization;
    no power-of-two scaling; not `precise`; no invalid-scalar check; no RGBA sentinel check; RGBA
    forced opaque. CPU: the auto range over invalid values, no RGBA sentinel, the style cache
    ignoring the revision, the GPU front not observed (extraction tests) and the colormap index
    left invalid (renderer test) are detected; style caches in a `std::vector` survive, as
    planned (the lent spans point into the inner buffers, which a move keeps). "A new scene key
    is required" is rerun after block B (the block-B tests did not compile yet).
- 2026-10-27: Slice 4 block B, red-first predictions (the option list and the model's pending
  parameter exist, no behavior; the agent operations do not exist):
  - `SandboxEditorUi.VectorFieldStyleCandidatesAreExactlyWhatTheCommandAccepts`: no style
    options are offered, so every scalar, vec3 and vec4 name fails its "accepted iff offered"
    check (the command accepts any name today) and both counts are 0.
  - `SandboxEditorUi.VectorFieldStyleNamesResolveOnceAndReportTheirFallback`: the missing names
    are Applied instead of refused (so the Add does not fail first and the second Add answers
    NoChange); the row statuses are empty.
  - `SandboxDomainPanels.VectorFieldColorPropertyKeepsTheRememberedUniformColor`: the Color
    property combo does not exist, so the dropdown does not open and later steps fail.
  - `AgentOperations.VectorFieldOperationsValidateTheirArgumentsBeforeTouchingTheScene` and
    `SandboxAgentServer.VectorFieldAddsStylesRefusesAndUndoes`: the operations are unknown.
  - Red results, as predicted: the parity test fails every "accepted iff offered" check (no
    options offered, everything accepted); the name test fails at the first Add (Applied instead
    of refused) and stops at the second (NoChange); the panel's Color property dropdown does not
    open (the test first lacked the history service, `SceneDocumentModule`, and was rerun); the
    agent contract test finds no operation; the agent server test gets "Unknown tool" (its reads
    were first made safe for replies without "result", which had aborted the test binary).
- 2026-10-27: Slice 4 block A code review (Claude author → Codex reviewer, fresh `codex exec`
  thread `01a17c26-e240-7da3-aed2-d5e03491187f`, codex-cli 0.163.0, requested `gpt-6-astra` at
  `xhigh`, read-only sandbox; recursion protection instruction-only), on the block-A patch sha256
  `7fdede75…` against `215432c01`. Verdict "revise required". Blocking: a GPU may flush denormal
  operands before the 2^100 scaling, so a denormal range (bit patterns 0x1..0x4) collapses to the
  low end (portability; not reproduced on the RTX 4090). Nonblocking: the auto range copied the
  valid values a second time (`ComputeRange` already skips infinities); the deque is unnecessary
  (no entry pointer is held across an insertion). Fixes: denormals are scaled through their bits
  (`ScaleUp100`), the smoke gains layer L6 with denormal ends and an interior value; the auto
  range runs `ComputeRange` with infinities skipped; the style caches are a `std::vector` again.
  - Predictions for L6 (range 0x1..0x4, values 0x1, 0x3, 0x4): with `ScaleUp100` every check
    passes. With the reviewed shader's plain `x * 2^100` (mutation M15) the outcome depends on
    whether this GPU flushes denormal operands: preserved, L6 passes and the fix is shown only
    by the code and the review's reading of the Vulkan spec; flushed, L6 draws the low end for
    0x3 and 0x4.
  - Results: with `ScaleUp100` the new smoke passes. M15 (plain `x * 2^100`) is detected: L6
    draws Jet's low end (0, 0, 236) for 0x3 and 0x4, so this GPU flushes denormal operands and
    the review's finding reproduces here. No large-range scaling (L4's middle) and no tiny-range
    scaling (L5's high end) are detected. Without `precise` the smoke now passes: with the scaling
    in its own branches the compiler no longer cancels it (the earlier one-expression form failed);
    `precise` stays as a guard against that measured rewrite and is recorded as not separately
    observable.
- 2026-10-27: Slice 4 block B implemented: `EditorVectorFieldStyleOption` candidates from one rule
  (`VectorFieldStyleOption`: scalar for length; scalar, vec3 or vec4 for color; no topology rows),
  row statuses for fallbacks and GPU previews, the command's new-name check (CPU property or
  `PendingResidentScalar`), the panel's Length/Color property combos with the disabled,
  not-written uniform color and the colormap/range controls, and the agent operations
  `vector_fields` / `vector_field`. The five block-B tests pass. Mutations: no style options,
  no name check, unchanged names rechecked, the command or the model ignoring GPU previews, the
  panel writing a bound color, and agent add taking fields are detected; the agent's
  `UseLayerStyle` on update survived because the command reads that flag only on add, so it was
  removed. "A new scene key is required" (block A) is detected by the serialization test.
  ImGui 1.92.5 activates a disabled item through `ActivateItemByID`, so the panel test types into
  the disabled color and asserts that nothing is written (the house rule for bound uniforms); a
  disabled length field would not be caught by its activation check.
- 2026-10-27: Slice 4 block-B review (same Codex thread as block A, `01a17c26-…`), on the staged
  slice sha256 `6d2aae50…`. Verdict "revise required". Blocking: a style property with the wrong
  element count is offered and accepted, then falls back silently (B1); unknown agent arguments
  are ignored, so `"lenght":2` answers NoChange and a typo beside a valid field applies the rest
  (B2). Nonblocking: the length label needs no `std::string`; the panel test never edits the
  length with a length property bound. The block-A findings were judged addressed.
  - Red-first predictions for the new checks (tests written before the fixes): the parity test
    offers `v:short` (float, two of three values), so its length count is 7, not 6, and its
    color count 10, not 9; the name test finds no status for the shrunken bound `v:g`; both
    agent arguments with `lenght` are accepted (the detached call then says "not attached"
    instead of `invalid_params`). The extended panel test (a bound length property, then the
    multiplier set to 2) is expected to pass already; it is shown sensitive by a mutation.
  - Red results, as predicted: lengths 7 / colors 10 offered, no status for the shrunken `v:g`,
    both `lenght` arguments accepted; the extended panel test passed. Fixes: the shared
    candidate rule reads the property's descriptor and requires one value per element; the agent
    operation refuses any key outside its schema before touching the workspace; the length label
    is a literal again. With them the block-B tests and the agent suite pass (42 tests). The
    panel test's new length step is detected by a mutation that drops the edit while a length
    property is bound.
- 2026-10-27: Slice 4 delta review (same Codex thread), on the delta sha256 `7ae4db0e…` to the
  staged slice sha256 `216baae6…`. Verdict verbatim "approve" ("B1 and B2 are resolved. No
  remaining or new blocking findings."); the reviewer checked that the reviewed patch plus the
  delta reproduce all 29 staged files.
- 2026-10-27: Slice 4 gates on the final tree: CPU 5917/5917; Vulkan `gpu;vulkan` 168/168 (RTX
  4090, driver 580.159.04, X11 `:1`, backbuffer 1600x900). Layering, task policy, session brief,
  source rules, test layout, docs sync and compiler hazards pass. jscpd: no new clone in the
  changed files beyond include blocks and the usual test setup. The acceptance criteria are left
  for the closing review; slice 5 (the combined and live check) is open.
