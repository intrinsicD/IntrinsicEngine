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

### Slice 3b — Normal on the point lane (~250 lines, new shader read)
- `assets/shaders/forward/point.vert:103` sets the view normal to +Z for every point, so Surfel
  discs face the camera even for point clouds that upload normals; `point.frag:25` already
  handles surfel ellipticity and facing. Read `geo.NormalBufferBDA` when the geometry has one
  and transform it to view space by the inverse transpose; a zero or non-finite normal keeps +Z.
  Mesh vertex views and graph point lanes upload `NormalBytes` from the `Points.Normal` slot
  (else `v:normal`) through `PrepareBoundVertexChannels`.
- Visible effect: Surfel orientation and shading. Flat and Sphere ignore the normal, and the row
  tooltip says so. The Edges lane gets no Normal row: lines are unlit and read no normal.
- Tests:
  - Extraction uploads points-lane normals for a mesh vertex view and a graph point lane.
  - A non-uniformly scaled entity keeps surfels perpendicular to the transformed normal.
  - A zero normal falls back to +Z.
  - A `gpu;vulkan` readback shows an edge-on surfel for a normal perpendicular to the view.

### Slice 4 — per-arrow length and color for vector fields (~750 lines)
Current state:
- Each arrow is one GPU instance (9 vertices) expanded in
  `assets/shaders/visualization_vector_field.vert`.
- Length, LengthMode and Color are uniform fields of the 128-byte
  `VisualizationVectorFieldDrawRecord` (`Graphics.VisualizationOverlayUploadHelper.cppm:25-40`).
- The 12 reserved bytes at offsets 116-127 hold one address at most (at 120). Two addresses
  plus a range do not fit, so the record grows.
- No agent operation reaches vector fields.

Plan:
- Spike first: prove a typed per-element buffer resolves by count through the existing packet
  path (`Graphics.Renderer.cpp:2246-2283`) and that a not-yet-ready colormap index is observable.
- Recipe (`Runtime.GeometryPresentation.cppm:177`):
  - Optional `LengthProperty` and `ColorProperty` (`GeometryPropertyRef`, both on
    `Vector.Domain`), plus `ColorMap`, `ColorAutoRange` and `ColorRangeMin/Max`.
  - `ValidateGeometryVectorFieldLayers` accepts every scalar kind the Color rows accept (float,
    double, signed/unsigned integer, bool) through the overlay's checked conversion
    (`Runtime.VisualizationRecipes.cpp:591`), and Vec3/Vec4 for color.
  - It also checks that the colormap is in range and that a manual range is finite with
    min < max. The editor command resolves the named refs.
- Extraction (`Runtime.RenderExtraction.VectorFields.cpp`):
  - Resolve each ref by kind and count. A ref that no longer resolves falls back to the
    uniform value with a counter and a layer status (the `Vector` pattern).
  - A float payload cache keyed like the vector cache converts with the checked conversion.
    It marks NaN, ±inf and overflowing values invalid as +inf (scalar uploads reject NaN,
    `Graphics.VisualizationPackets.cpp:290-293`).
  - The auto range comes from the overlay's `ComputeRange`
    (`Runtime.VisualizationRecipes.cpp:237-310`, exported, not copied), run over valid samples
    only. A constant range is widened as the overlay does. All samples invalid gives the uniform
    length/color for the layer and a status line.
  - A Vec3/Vec4 color with any non-finite component draws the uniform color for that arrow;
    components are clamped to 0..1.
- GPU:
  - The record grows to 144 bytes: `ColormapID@116`, `LengthBDA@120`, `ColorBDA@128`,
    `RangeMin@136`, `RangeMax@140`, plus two flag bits for the color kind.
  - The C++ struct, its `static_assert`, the shader struct and
    `Test.VisualizationOverlayPass.cpp:583` change together.
  - The shader scales by `Scale * s` and draws no arrow for `s <= 0` or an invalid `s`. It reads
    the property through the glyph row index, so the UI-081 order indirection applies.
  - A scalar color samples the bindless colormap texture, as `point.vert` does. An invalid
    sample or a not-yet-ready colormap draws the uniform color.
- UI (`Sandbox.DomainPanels.cpp:609-696`):
  - A Length row: the uniform field is labelled Scale and stays enabled while a property is
    bound, because it multiplies.
  - A Color row: the uniform color is disabled while bound; a scalar shows colormap, auto range
    and min/max.
  - The vector-field model gets its own candidate lists, so
    `VectorFieldModelOffersExactlyTheVectorsTheCommandAccepts` stays valid.
- Agent parity: one agent operation for vector-field layers (add/update/remove with the full
  layer recipe) through the existing `ApplyEditorGeometryVectorFieldCommand`, in
  `Runtime.AgentOperations.Editor.cpp`, with its schema. There is no new command layer.
- Persistence: optional scene keys (absent = no property). Undo snapshots the whole recipe and
  the recipe's `operator==` is defaulted, so both cover the new fields.
- Tests, red first:
  - Recipe validation and model candidates for every scalar kind
    (`Test.VisualizationRecipes.cpp`, `Test.SandboxEditorVisualization.cpp`).
  - Extraction (`Test.RuntimeVectorFieldExtraction.cpp`): resolve, overflow, mixed
    invalid/valid samples, constant range, all invalid, a removed property falling back, and
    rows read through the glyph order.
  - Record layout (`Test.VisualizationOverlayPass.cpp`).
  - Round trip (`Test.RuntimeSceneSerialization.cpp`).
  - Panel rows (`Test.SandboxDomainPanels.cpp`).
  - The agent operation matches the UI result (`Test.AgentOperations.cpp`,
    `Test.SandboxAgentServer.cpp`).
- `gpu;vulkan` smoke: two arrows with different property lengths and a scalar color at both
  colormap ends, read back (extends `RuntimeSandboxAcceptanceGpuSmoke.VectorField*`).

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
