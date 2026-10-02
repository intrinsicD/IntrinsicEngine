---
id: RUNTIME-315
theme: J
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; evidence is the diff, tests, and CI
maturity_target: Operational
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, runtime.editor-prepared-frame-locality]
---
# RUNTIME-315 — Choose the source property of every render attribute, per element domain

## Goal
- One runtime-owned binding model lets the operator pick, per entity and per render element domain (Vertices/points, Edges, Faces), which typed property feeds each shader attribute: position, normal, texcoord, color, point size/radius and line width. Today only color (and, partly, normal) is selectable. Positions, size and width are hard-wired to canonical names.
- Operator feedback 2026-10-02: in the Appearance panels the user cannot see or choose which property is bound to each shader varying attribute, as in Framework24; e.g. another `vec3` property must be usable as the positions attribute when rendering.

## Non-goals
- No change to the canonical geometry data: a binding is a rendering-time source choice, never a copy into `v:position` and never a conversion entity (same rule as UI-051).
- The material-channel texture-vs-vertex source (`GRAPHICS-105`) stays orthogonal; this task owns structural vertex/line/point streams and per-element appearance values.
- Model-space radius units, projection and depth semantics stay in `RUNTIME-222`; this task provides the source binding that `RUNTIME-222` consumes.
- The panel is `UI-075`; agent parity is `RUNTIME-316`; Vulkan Operational evidence is `GRAPHICS-158`.

## Context
Investigated 2026-10-02 against `main` (50e4de523).

Existing mechanisms (three, overlapping):
- `VertexChannelBindingSet` (`src/runtime/GeometryIntegration/Runtime.VertexChannelBindings.cppm:20`) holds only `Normal` and `Color` overrides. `VertexChannel` also names `Position`, `Texcoord`, `Tangent` (`Runtime.VertexAttributeBinding.cppm:21`), and `ResolveVec3Channel/Vec2Channel/ColorChannelPackedUnorm8` give typed, count-checked, fallback-aware resolution (`AttributeBindStatus`: Bound/EmptyBinding/PropertyMissing/TypeMismatch/CountMismatch). `FindMutableVertexChannelBinding` returns `nullptr` for Position/Texcoord (`Runtime.VisualizationEditingOperations.Actions.cpp:332`), and `VertexChannelExpectedTypeText` says "unsupported vertex channel" (`Runtime.EditorWorkspaceSnapshots.Models.cpp:586`). Edit command: `ApplyEditorVertexChannelBindingCommand`, Sandbox UI only under Appearance > Advanced > "Vertex channels" (`Sandbox.DomainPanels.cpp:175`, `:743`). The set is **not serialized** (no hit in `Runtime.SceneSerialization.cpp`); only `GeometryPresentationRecipe` is (`:1480`, `:2539`).
- `GeometryPresentationSlotRecipe` (`Runtime.GeometryPresentation.cppm:122`): slots keyed by lane and semantic (Albedo, Normal, Roughness, Metallic, ScalarField, Displacement, PointColor, PointScalarField, PointSize, PointNormalOrientation, LineColor, LineScalarField, LineWidth), `SourceKind` UniformDefault/AuthoredTextureAsset/GeneratedTextureAsset/PropertyBake/PropertyBuffer, readiness/provenance, undo through `CommitGeometryPresentationChange`, serialized with the scene. Commands `ApplyEditorGeometryPresentationSlotDefaultCommand` / `...PropertyCommand` (`Runtime.VisualizationEditingOperations.Actions.cpp:1951`, `:2018`) validate type/domain/count with `ResolveGeometryProperty`. Extraction only consumes the texture-backed semantics; `PointColor`, `PointSize`, `LineColor`, `LineWidth`, `Displacement` return unsupported (`Runtime.RenderExtraction.cpp:1072`, `:1157`) and point size/line width come only from uniform render hints (`:351-389`). The Sandbox lists these slots read-only ("Binding targets", `Sandbox.DomainPanels.cpp:144`).
- `VisualizationConfig` color/scalar overlays (`Graphics.Component.VisualizationConfig.cppm:55-103`) — the only path driven from the main Property dropdown (`Sandbox.DomainPanels.cpp:417`).

Positions are not bindable (answer to "can the renderer use another vec3 as positions?": not today; the renderer-side stream is name-agnostic, the runtime is not):
- The graphics upload takes position bytes (`GeometryUploadDesc::PositionBytes`); it does not know the property name. Runtime plan builders hard-code `v:position`: mesh `Runtime.GeometryPlanBuilders.MeshPrimitiveView.cpp:139`, graph `...Graph.cpp:198`, point cloud `...PointCloud.cpp:~100`, mesh pack `...Mesh.cpp:~330-380`.
- Invalidation/revision tracking is keyed on the name: `RenderExtractionGeometrySourceRevisions.Position = PropertyRevisionOf(properties, kPosition)` (`Runtime.RenderExtraction.Geometry.cpp:127`), `ObservePositionFront` hard-codes `"v:position"` for the GPU position ring (`Runtime.RenderExtraction.cpp:468`, GRAPHICS-156).
- Consumers that assume the canonical positions: culling bounds are computed by the plan builder from the packed positions (so they follow whatever span is packed); click picking and primitive refinement read `v:position` on the CPU (`Runtime.PrimitiveSelectionRefinement.cpp:274,488,585`, `Runtime.SceneInteractionModule.Selection.cpp:31`, pick stamp `Runtime.SceneInteractionModule.cpp:72`, `Runtime.SelectionController.Primitives.cpp:103`); vector-field anchors read `v:position` (`Runtime.RenderExtraction.VectorFields.cpp:182,514`); `SpatialIndexCache` BVH/KD consumers index canonical positions. A displaced display therefore diverges from picking unless picking/vector fields are told which positions are displayed.
- Normals are not derived from positions: the mesh builder reads `v:normal` (and corner `h:normal`), falling back to +Z per element (`Runtime.GeometryPlanBuilders.Mesh.cpp:164-185`, `:266-316`); shading normals therefore stay stale when positions are rebound unless the user also binds normals or the runtime derives them.
- Domain handling: `ResolveGeometryProperty`/`ResolveGeometryElementCount` already reject type/count mismatches per `GeometryElementDomain`; mesh face/edge properties reach the vertex stream only through corner-seam GPU vertex splits (`MeshSourceVertexForGpuVertex`), which must be reused for non-vertex sources.

Contract review: `geometry.element-domain-sources` (property-name-independent binding, per-domain eligibility, same-domain publication), `geometry.property-coherence` (a bound property's CPU revision must invalidate/upload, GPU residency observes), `runtime.editor-prepared-frame-locality` (visualization editing helpers/command declarations). `graphics.recipe-slot-lookup` is about render-graph recipe slots, not presentation slots; not applicable.

## Operator decisions (2026-10-02)
- **Model:** unify the existing mechanisms (`VertexChannelBindingSet` + `GeometryPresentationSlotRecipe`) behind one binding command. The VisualizationConfig overlay maps onto it. No parallel new model.
- **Rebound positions:** picking, culling bounds, BVH/spatial queries and vector-field anchors follow the displayed (bound) positions.
- **Shading normals:** stay canonical (`v:normal`) or the explicitly bound normal property. They are not recomputed from rebound positions.
- **Color:** one mechanism only. `show_property` and the panel's Property selection become the Color binding, so there is no precedence conflict.
- **Size/width:** bind in pixels in this task. Model-space units remain owned by RUNTIME-222. (Default chosen; the operator was not asked.)

## Control surfaces
- Config: none new; bindings are authored scene state, saved/loaded with the scene (recipe JSON).
- UI: `UI-075` (unified Appearance panel) is the paired UI; it must call the same command (UI-parity rule, `docs/architecture/agent-control-lane.md`).
- Agent/CLI: `RUNTIME-316` (`bind_attribute`, extend `show_property`) calls the same commands.

## Maturity
- Slices 1-6 close `CPUContracted`. `Operational` (actually rendered, picked, culled with a rebound position under Vulkan) is owned by `GRAPHICS-158`, using `gpu;vulkan` smokes under the Xephyr nested X server.

## Slice plan
Design recommendation (confirm in slice 1, record in a Log entry): extend the already-wired `VertexChannelBindingSet` (adds Position and Texcoord, keyed per `GeometryElementDomain`) for structural streams, and use `GeometryPresentationSlotRecipe` `PropertyBuffer` slots for per-element appearance values (PointSize, LineWidth, PointColor, LineColor); expose both through one `ApplyEditorAttributeBindingCommand` facade and one `BuildEditorAttributeBindingModel` so UI and agent see a single table. `VisualizationConfig` remains the sci-vis colormap overlay.
1. **Binding table and model (CPUContracted, ~350 lines).** Define the attribute x element-domain table (Position, Normal, Texcoord, Color per vertex/point; Color/Width per edge; Color per face) with required type, count rule, default source and fallback; `BuildEditorAttributeBindingModel` lists compatible/incompatible candidate properties with reasons. No render change.
2. **Command, validation, undo (~350 lines).** `ApplyEditorAttributeBindingCommand` over the facade; reject type/domain/count mismatch via `ResolveGeometryProperty`; `Default` restores the canonical source; undo/redo as one history step; extend `FindMutableVertexChannelBinding` to Position/Texcoord; stable `EditorCommandStatus` reasons.
3. **Persistence (~250 lines).** Serialize `VertexChannelBindingSet` and attribute slots in the scene file; stale-name handling on load (missing property falls back to default with a diagnostic, never silently); round-trip tests.
4. **Normal, color, texcoord, size, width consumption (~400 lines).** Plan builders and uniform-size/width lowering consume bindings for the existing builder-owned streams; bound properties revalidate on revision change (`BindingGeneration`); size/width bindings feed `GpuEntityPointConfig::PointSizeBDA`/line width through the PropertyBuffer path, with `RUNTIME-222` unit semantics deferred.
5. **Position binding and its consequences (~400 lines).** Plan builders and `ObservePositionFront`/revision tracking resolve the bound position property; culling bounds from the displayed span; picking, vector-field anchors and primitive refinement use an explicit "displayed positions" resolver so pick results map back to canonical element ids; normals: keep canonical/bound normals (documented) or recompute shading normals from displayed positions on request (decision recorded; default keeps canonical normals); BVH/`SpatialIndexCache` stays on canonical positions and is documented as such.
6. **Graphs and point clouds on the same table (~300 lines).** One table drives graph nodes/edges and point clouds; mesh vertices viewed as points/edges reuse the same entity binding (no per-lane copies), consistent with `UI-051`.

## Acceptance criteria
- [ ] A finite `vec3` vertex property on a mesh, graph or point cloud can be bound as the position attribute; the rendered, culled and picked geometry follows it, canonical `v:position` is unchanged, and `Default` restores it.
- [ ] Normal, color (`vec3`/`vec4`), texcoord (`vec2`), point size and line width (`float`) accept any compatible property on the right element domain; mismatches are refused with typed reasons.
- [ ] Bound-property edits (including GPU method results) update the displayed attribute without re-binding (revision/`BindingGeneration` invalidation).
- [ ] Bindings undo/redo as one step and survive scene save/load; a missing property on load degrades to the default with a visible diagnostic.
- [ ] Position-bound picking returns canonical element ids and does not select by stale canonical positions.
- [ ] Contract tests cover every row of the attribute x domain table; Vulkan evidence is delivered by `GRAPHICS-158`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'GeometryPresentation|SandboxEditorVisualization|MeshGeometryExtraction|GraphGeometryExtraction|PointCloudGeometryExtraction|RuntimeSceneSerialization|VertexChannelStreams|RuntimeRenderExtraction' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Log
- 2026-10-02: Slice 1 design confirmed and adjusted. One table
  (`RenderAttributeRules()` in `Runtime.VertexChannelBindings`) defines every
  attribute x element-domain row: Position (vec3, finite; mesh vertex, graph
  node, point), Normal (vec3; mesh vertex or corner, graph node, point),
  Texcoord (vec2; mesh vertex or corner), Color (float/vec3/vec4; every vertex,
  edge and face domain), PointSize (float, finite, px; vertex domains) and
  LineWidth (float, finite, px; edge domains). `ResolveRenderAttributeSource`
  is the single validation (missing/kind/count/finite, typed
  `GeometryPropertyResolutionStatus`); `BuildEditorAttributeBindingModel` lists
  rows, current sources (a stale source is reported as a fallback) and every
  candidate with its reason. Storage reuses existing owners, no new model:
  structural streams stay in `VertexChannelBindingSet` (now Position, Normal,
  Texcoord); Color is the visualization overlay written by the `show_property`
  recipe path; PointSize/LineWidth are the existing `RenderPoints::SizeSource` /
  `RenderEdges::WidthSource` name alternative (already serialized and
  undoable, and next to the uniform pixel default) instead of the
  `GeometryPresentationSlotRecipe` PointSize/LineWidth slots, which no default
  recipe creates and extraction reports unsupported; those slot semantics stay
  unsupported. The `VertexChannelBindingSet::Color` stream is the duplicate
  color mechanism and is retired with the old vertex-channel command.
  Revised slices: 2 command/undo; 3 retire the duplicate color binding and old
  command; 4 persistence; 5 normal/texcoord/size/width consumption; 6 position
  consumption (bounds, picking, vector fields, GPU front); 7 graphs/point
  clouds/primitive views and docs.
- 2026-10-02: Slice 1 review decisions (coordinator, on shader evidence).
  Sizes stay pixels: `point.vert` `ResolvePointSizePx` and `line.vert` read
  `PointSizeBDA`/`LineWidthBDA` as pixels clamped to 0.5..32; the
  "world-space radii" note in `Graphics.Component.GpuSceneSlot.cppm` was stale
  and is corrected; model-space radii stay `RUNTIME-222`. Color: the overlay
  is the single Color binding. The promoted surface shaders
  (`forward/default_debug_surface.vert`, `deferred/gbuffer.vert`) read the
  overlay color per vertex, so it interpolates; the nearest-vertex Voronoi in
  `surface_color_resolve.glsl` belongs to the unreferenced legacy pipeline.
  Canonical `v:color` (unbound default through `PackedVertexColors`) is kept
  unchanged; only the `VertexChannelBindingSet::Color` binding and its command
  retire in slice 3 (its packed colors are read by the surface shader only,
  never by the point/line shaders).
- 2026-10-02: Slice 2 (command). `ApplyEditorAttributeBindingCommand` commits
  each attribute as one undo step on its owner: structural streams through
  the vertex-channel mutation, Color through the `show_property` recipe path
  (its encoder is the Color validator, `ResolveEditorAttributeBindingSource`),
  point size/line width through the render-hint mutation. Typed refusals:
  `UnsupportedRenderAttribute`, `AttributeSource{Missing,TypeMismatch,
  CountMismatch,NonFinite}`. Per-element pixel sizes are not extracted yet
  (a named size stops the lane drawing), so the command refuses to author
  them and the model marks such rows `Consumed = false` until slice 5.
  One domain-to-overlay-lane mapping (`ColorOverlayTargetFor`) now serves the
  binding, the recipe path and the property-preset command; the recipe
  encoder's own `ToVisualizationDomain` maps to a different enum and module
  and stays. Value scans are memoized by property content revision.
