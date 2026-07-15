# UI Backlog

Editor/UI integration seams that consume runtime/renderer composition and
emit user-driven commands/events. UI must not own simulation state, render
ownership, or asset ownership; it should pass commands/events to owning
systems instead.

See [`tasks/backlog/README.md`](../README.md) for the cross-domain convergence
map.

## Tasks

- [UI-035 — Sandbox point-cloud consolidation editor panel](UI-035-sandbox-pointcloud-consolidation-editor-panel.md)
  (LOP/WLOP/CLOP/EAR strategy + CPU/GPU backend picker driving the
  `RUNTIME-175` config-lane apply path; gated on `runtime/RUNTIME-175`;
  coordinate with active `ARCH-006` Slice 4).

Further UI children open from the deferred triggers recorded by the retired
RORG-031F seed.

The runtime SpatialDebug closest-face picking consumer for `GEOM-039` is
retired under the runtime backlog as `RUNTIME-135`.

## Convergence

- UI-034 is retired under Theme F. It adopts the framework24 viewer
  interaction/layout conventions through the ADR-0024 EditorUiModule/panel
  registry direction: structured domain-window contributions, lazy window
  lifecycle, one input-capture snapshot, a global UI toggle, and generic
  scalar-property plot widgets.
- RORG-031F is retired as the Theme F planning seed that kept the promoted
  Sandbox editor inventory and deferred-workflow trigger list aligned while
  concrete UI children were opening and retiring independently.
- UI-027 (retired) was the paired editor command/UI for the `GEOM-016`
  point-cloud outlier-removal operators under **Theme F**, continuing the
  `bcg_code_base` geometry-processing port into interactive Sandbox workflows.
- UI-028 is retired as the mesh simplification method-window follow-up: `Mesh >
  Processing > Simplify` drives the `GEOM-014` classical QEM / FA-QEM kernel
  through a runtime-owned undoable command, preserves UV seams when texcoords
  are present, and surfaces simplification diagnostics without UI owning engine
  state.
- UI-029 is retired as the ICP registration editor follow-up for the `GEOM-055`
  observer seam: the top-level `ICP Registration` panel selects source and
  target point-cloud entities, runs `Runtime.RegistrationAlignment`, stores the
  convergence trajectory, and lets the user scrub intermediate poses through an
  undoable runtime-owned transform command.
- UI-030 is retired as the 2026-07-01 editor-stutter diagnostic follow-up. The
  bounded `ExtrinsicSandbox --frame-pacing-report` capture loop ranks the
  default run as present/fallback-frame-lifecycle dominated, rules out editor
  callback work and ImGui draw-data copying as dominant causes, and splits the
  default sandbox Vulkan validation-gate fallback to `BUG-056`. Source-level
  selected-entity findings remain tracked by `RUNTIME-138`, retired
  `GRAPHICS-113`, and retired `GRAPHICS-114`.
- UI-031 is retired as the information-architecture follow-up from the
  2026-07-01 domain-UI review: `Properties` is now a pure data explorer,
  `Appearance` owns render hints, visualization, binding, and texture-bake
  controls, and domain processing leaves open focused method windows. The
  broader generation-keyed async selected-analysis cache/job pipeline remains
  owned by open `RUNTIME-138`.
- UI-001 is retired as part of **Theme A — Working sandbox app path** and depends on
  `RUNTIME-090` + `GRAPHICS-079` for ImGui frame production/presentation plus
  runtime selection/geometry-residency tasks for live content. RUNTIME-095 closes
  the scoped visual/interactive proof; broad file-backed UI workflows remain
  future work.
- UI-002 is a retired post-acceptance EditorUI follow-up on the promoted
  `SandboxEditorUi` shell: it adds first-class `Mesh`, `Graph`, and
  `PointCloud` ImGui menu slots whose windows control selected-entity
  components through runtime-owned command surfaces.
- UI-003 is a retired promoted EditorUI follow-up that reimplements the legacy
  geometry-processing capability discovery seam against promoted
  `GeometrySources`. The domain menus expose `Processing` windows with
  source-domain, stable algorithm-entry, and K-Means source-domain affordances.
- UI-004 is a retired promoted EditorUI follow-up that reimplements the first
  legacy geometry-processing execution seam: CPU K-Means over mesh vertices,
  graph nodes, and point-cloud points. The command writes legacy-compatible
  label/color properties and marks vertex attributes dirty. `RUNTIME-103`
  records synchronous CPU K-Means as the intended endpoint for current
  workflows; asynchronous scheduling, centroid entities, topology mutation, and
  broader algorithm execution require a future value-gated runtime/method task
  with a concrete workload. `GRAPHICS-086` retires CUDA from the promoted
  default path unless a future method/backend task supplies that workload.
- UI-005 is a retired promoted EditorUI follow-up that reimplements the legacy
  property-enumeration-to-visualization seam for current `GeometrySources`.
  Selected mesh, graph, and point-cloud visualization windows now list eligible
  scalar, isoline, color-buffer, and vector-candidate properties and route
  supported presets through runtime-owned `VisualizationConfig` commands.
  Generic GPU residency/upload for arbitrary property arrays remains future
  runtime/renderer work.
- UI-006 is a retired promoted EditorUI follow-up that reimplements the legacy
  `Frame Graph` diagnostics panel against current `RenderGraphFrameStats`:
  compile/execute counts, queue/timeline stats, command pass statuses,
  diagnostics, and the compiler debug dump are copied through a runtime-owned
  data model and rendered from the app-owned `EditorShell`.
- UI-007 is a retired promoted EditorUI follow-up that exposes drag/drop import
  status and payload hints while runtime/platform own events, decoding,
  `AssetService`, and ECS materialization.
- UI-008 is a retired value-gated workflow child from `LEGACY-011`;
  it consumes `RUNTIME-102`
  for command history / dirty-state source of truth. The
  `PLATFORM-006`
  file-dialog boundary is retired: current workflows use runtime/UI path entry
  plus platform dropped-path events, while native dialogs remain deferred unless
  a new platform/runtime task accepts them. Sample/debug scene expansion and
  app-level debug workflow clones remain deferred until a runtime-owned command
  accepts them.
- UI-013 is a retired promoted EditorUI follow-up that turns the UI-002
  render-hint status windows into controls for the existing promoted
  `RenderSurface`, `RenderEdges`, and `RenderPoints` value components.
  Commands are undoable through `EditorCommandHistory`, graph edge-lane edits
  repack runtime graph residency, and uniform point settings flow to retained
  point GPU config. Retained-line per-entity width rasterization remains a
  future renderer task.
- UI-014, UI-016, and UI-017 retired the framework24-style selected-geometry
  usability follow-up after `UI-015`: all geometry properties remain visible,
  compatible binding choices show their reasons, bound-state rows report render
  lanes, slots, defaults, properties, textures, readiness, diagnostics, and
  derived-job/bake progress, and mesh UV/bake controls route through runtime
  command surfaces without UI owning geometry, runtime, asset, or graphics
  state.
- UI-018 retired the sandbox startup organization follow-up: the editor now
  starts menu-first, with top-level panels available from `View` and all
  sandbox/domain windows closed until explicitly opened.
- UI-019 retired the visualization uniform-color edit follow-up: mesh, graph,
  point-cloud, and top-level geometry visualization windows show an ImGui color
  edit widget when `VisualizationConfig::ColorSource::UniformColor` is active,
  with edits routed through the existing runtime visualization command seam.
- UI-020 retired the visualization lane-color follow-up: mesh, graph, and
  point-cloud domain visualization windows now target surface, edge, and point
  render lanes by source-row presence, with optional lane overrides letting mesh
  vertices and graph nodes use point-lane uniform colors independently of edge
  and surface visualization.
- UI-021 retired the migration that removed the remaining UI-local
  availability policy from the Sandbox editor presentation: domain windows,
  visualization controls, property catalogs, and processing affordances now
  consume the runtime resolver from `RUNTIME-117`, while preserving provenance
  labels for mesh/graph/point-cloud origins.
- UI-022 retired the vertex-processing menu request: mesh, graph, and
  point-cloud `Processing > Vertices` submenus now expose `Normals` method
  windows backed by the retired `GEOM-026` geometry modules.
- UI-023 retired the render recipe editing follow-up: sandbox UI now inspects
  renderer descriptors, optional recipe slots, binding overrides, view/output
  recipes, validation/preview status, artifact lifetime/status, and activation
  outcomes without UI owning renderer state.
- UI-025 retired the remesh/subdivide method-window follow-up: mesh processing
  now exposes `Remesh` and `Subdivide` windows that call the geometry-owned
  GEOM-043/GEOM-044 kernels through runtime-owned undoable command seams and
  defer renderer synchronization through geometry dirty tags.
- UI work that depends on renderer overlays/handoff coordinates with the
  retired `GRAPHICS-024`
  parity matrix, the `RUNTIME-104` decision not to retain a persistent
  derived-overlay producer for current workflows, and the rendering DAG in
  [`tasks/backlog/rendering/README.md`](../rendering/README.md).

## Retired

Retired entries moved here verbatim by the PROC-008 state/history
split; narratives live in the retirement log.

- [UI-036 — Sandbox parameterization editor panel and resizable UV split view](../../done/UI-036-sandbox-parameterization-editor-and-uv-split-view.md)
  (done, 2026-07-15, `Operational`): delivered implemented-strategy controls,
  validated config-lane apply/undo, aggregate diagnostics, and a resizable
  CPU `ImDrawList` UV view over the pointer-free model delivered by retired
  `RUNTIME-176`. The optional dense-mesh GPU-shaded upgrade is active as
  [`GRAPHICS-122`](../../active/GRAPHICS-122-uv-view-offscreen-render-target.md).
- [RORG-031F — UI integration backlog seed](../../archive/RORG-031F-ui-integration.md)
  (done, 2026-07-05, `Scaffolded`): planning-only umbrella retired after
  current UI children through `UI-031` were indexed, the remaining deferred
  workflows kept reserved prospective IDs (`UI-009..012`) and external
  triggers, and no concrete UI implementation task remained open.
- [UI-001 — Sandbox editor shell and core panels](../../archive/UI-001-sandbox-editor-shell-panels.md) (done, 2026-06-03, `CPUContracted`).
- [UI-002 — Sandbox EditorUI domain menu windows](../../archive/UI-002-editor-domain-menu-windows.md) (done, 2026-06-07, `CPUContracted`).
- [UI-003 — Sandbox EditorUI geometry processing capabilities](../../archive/UI-003-sandbox-editor-geometry-processing-capabilities.md) (done, 2026-06-07, `CPUContracted`).
- [UI-004 — Sandbox EditorUI K-Means execution command seam](../../archive/UI-004-sandbox-editor-kmeans-execution.md) (done, 2026-06-07, `CPUContracted`).
- [UI-005 — Sandbox EditorUI visualization property presets](../../archive/UI-005-sandbox-editor-visualization-property-presets.md) (done, 2026-06-07, `CPUContracted`).
- [UI-006 — Sandbox EditorUI render graph diagnostics panel](../../archive/UI-006-sandbox-editor-rendergraph-panel.md) (done, 2026-06-08, `CPUContracted`).
- [UI-007 — Sandbox EditorUI drag/drop import status](../../archive/UI-007-sandbox-editor-drag-drop-import.md) (done, 2026-06-08, `CPUContracted`).
- [UI-008 — Editor file dialog, dirty-state, and debug workflows](../../archive/UI-008-editor-file-dialog-dirty-debug-workflows.md) (done, 2026-06-09, `CPUContracted`):
  path-entry file boundary, dirty-state/undo-redo affordances, new/open/save/close
  command routing, and app-to-runtime-only dependency proof.
- [UI-013 — Sandbox EditorUI domain render hint controls](../../archive/UI-013-domain-render-hint-controls.md) (done, 2026-06-11, `CPUContracted`).
- [UI-015 — Progressive render-data inspector](../../archive/UI-015-progressive-render-data-inspector.md)
  (done, 2026-06-16, `CPUContracted`): selected-entity progressive inspector
  models and ImGui rows now expose entity shape, presentation slots, uniform
  defaults, source-property pickers, compatible/incompatible reasons, derived
  jobs, and composition summaries through runtime-owned command/history seams.
- [UI-016 — Geometry property catalog and binding usability](../../archive/UI-016-geometry-property-catalog-and-binding-usability.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh, graph, and point-cloud
  property catalogs now list internal/connectivity/user/generated properties,
  supported value previews, unsupported diagnostics, and compatible binding
  targets without raw property pointers.
- [UI-017 — Bound render state inspector](../../archive/UI-017-bound-render-state-inspector.md)
  (done, 2026-06-17, `CPUContracted`): selected mesh, graph, point-cloud, and
  composition models now expose render lanes, progressive slots, source kind,
  bound defaults/properties/textures, readiness diagnostics, and derived-job
  progress rows.
- [UI-014 — UV backend and texture bake controls](../../archive/UI-014-uv-backend-and-texture-bake-controls.md)
  (done, 2026-06-17, `CPUContracted`): selected-mesh UV diagnostics,
  xatlas-backed regeneration commands, property-catalog-driven bake sources,
  target semantic/encoder/output-size controls, and selected-mesh bake command
  routing are now present in the runtime-owned sandbox editor UI.
- [UI-018 — Sandbox menu-first UI defaults](../../archive/UI-018-sandbox-menu-first-ui.md)
  (done, 2026-06-17, `CPUContracted`): sandbox startup now shows only the
  main menu bar; top-level panels open from `View`, while domain windows remain
  closed until opened from the PointCloud/Graph/Mesh menus.
- [UI-019 — Visualization uniform color edit widget](../../archive/UI-019-visualization-uniform-color-edit.md)
  (done, 2026-06-19, `CPUContracted`): selected mesh, graph, point-cloud, and
  top-level geometry visualization windows expose an ImGui color edit widget
  for active uniform-color visualization configs without UI owning renderer or
  asset state.
- [UI-020 — Visualization lane uniform color controls](../../archive/UI-020-visualization-lane-uniform-color.md)
  (done, 2026-06-19, `CPUContracted`): domain visualization windows target
  surface, edge, and point render lanes by source-row presence, and optional
  lane overrides let mesh vertices and graph nodes use independent uniform
  point-lane colors.
- [UI-021 — Sandbox editor geometry availability migration](../../archive/UI-021-sandbox-editor-geometry-availability-migration.md)
  (done, 2026-06-19, `CPUContracted`): domain windows, visualization targets,
  property catalogs, primitive-view commands, render hints, K-Means affordances,
  and mesh UV/bake diagnostics now consume the runtime availability resolver.
- [UI-022 — Sandbox EditorUI vertex-normal recompute windows](../../archive/UI-022-sandbox-editor-vertex-normal-recompute.md)
  (done, 2026-06-28, `CPUContracted`): mesh, graph, and point-cloud normal
  recompute windows consume domain-owned geometry modules, publish canonical
  `v:normal`, and defer renderer synchronization through `DirtyVertexNormals`.
- [UI-023 — Sandbox render recipe editing UI](../../archive/UI-023-render-recipe-ui-editing.md)
  (done, 2026-06-24, `CPUContracted`): render recipe editor models and ImGui
  rows expose declared renderer slots, binding overrides, view/output recipes,
  validation/preview/activation state, and artifact publish/apply commands
  through runtime-owned seams without UI owning renderer state.
- [UI-024 — Sandbox EditorUI mesh denoising window](../../archive/UI-024-editor-mesh-denoise-window.md)
  (done, 2026-06-28, `CPUContracted`): mesh denoise commands consume the
  geometry-owned bilateral denoiser, publish canonical `v:position`, and defer
  renderer synchronization through dirty tags.
- [UI-025 — Sandbox EditorUI remeshing and subdivision windows](../../archive/UI-025-editor-remesh-subdivide-windows.md)
  (done, 2026-06-28, `CPUContracted`): remesh/subdivide commands consume
  GEOM-043/GEOM-044 topology operators, replace selected mesh `GeometrySources`
  through undoable runtime command history, and defer renderer synchronization
  through dirty tags.
- [UI-026 — Sandbox EditorUI curvature analysis window and principal-direction field](../../archive/UI-026-editor-curvature-analysis-window.md)
  (done, 2026-06-28, `CPUContracted`): mesh curvature commands consume
  `Geometry.Curvature`, publish canonical scalar/direction properties, and
  feed scalar colormap plus principal-direction visualization adapters.
- [UI-027 — Sandbox EditorUI point-cloud outlier-removal window](../../archive/UI-027-editor-pointcloud-outlier-removal-window.md)
  (done, 2026-06-29, `CPUContracted`): a `PointCloud > Processing > Remove
  Outliers` window drives the `GEOM-016` statistical/radius outlier-removal
  operators, rebuilds the entity's point `GeometrySources` via
  `PopulateFromCloud`, and is undoable through `EditorCommandHistory`.
- [UI-028 — Sandbox EditorUI mesh simplification window](../../archive/UI-028-editor-mesh-simplification-window.md)
  (done, 2026-07-05, `CPUContracted`): `Mesh > Processing > Simplify` drives the
  `GEOM-014` classical QEM / FA-QEM simplification kernel through an undoable
  runtime command, preserves UV seam inputs, reports collapse/rejection/pin
  diagnostics, and defers renderer synchronization through geometry dirty tags.
- [UI-029 — Editor ICP registration panel + convergence visualization](../../archive/UI-029-editor-registration-convergence-visualization.md)
  (done, 2026-07-05, `Operational`): the top-level `ICP Registration` panel
  consumes `Runtime.RegistrationAlignment` to align selected point-cloud
  entities, records the per-iteration convergence trajectory, scrubs trajectory
  poses, and routes transform writeback through undoable runtime commands.
- [UI-030 — Sandbox EditorUI frame-pacing diagnostics](../../archive/UI-030-editor-frame-pacing-diagnostics.md)
  (done, 2026-07-05, `Operational`): bounded sandbox captures now emit
  `intrinsic.frame_pacing.v1` JSON with runtime, ImGui producer, render-graph,
  and Vulkan lifecycle timing buckets. The report ranks the default capture as
  present/fallback-frame-lifecycle dominated, rules out editor callback and
  ImGui draw-data copy work as dominant causes, and files `BUG-056` for the
  default sandbox Vulkan validation gate fallback.
- [UI-031 — Sandbox EditorUI domain-window reorganization](../../archive/UI-031-editor-domain-ui-reorganization.md)
  (done, 2026-07-05, `CPUContracted`): Mesh/Graph/PointCloud domain
  `Appearance` windows now co-locate render hints, visualization, uniform/lane
  color, property/attribute binding, bound-state inspection, and texture-bake
  controls; `Properties` windows are pure property explorers; and processing
  menu leaves open focused method windows instead of the old omnibus processing
  surface.
