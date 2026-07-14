# RUNTIME-088 — Mesh primitive view lifecycle for vertices and edges

## Status
- State: `done` — retired to `tasks/done/` on 2026-05-31 at maturity `CPUContracted`.
- Owner: agent. Branch: `claude/intrinsicengine-agent-onboarding-RQtst`
  (Slice B; Slice A landed earlier on `claude/intrinsicengine-agent-onboarding-m6JHG`).
- PR/commit: Slice B landed on `claude/intrinsicengine-agent-onboarding-RQtst`
  as commit `69b3fb4`. PR: _TBD_.
- Maturity reached: Slice A landed at `Scaffolded` (standalone derivation packers
  + control-surface vocabulary, CPU contract-tested in isolation). Slice B closed
  `Scaffolded → CPUContracted` by wiring the `RenderExtractionCache` residency.
  `Operational` visual proof of the three lanes stays owned by `RUNTIME-095`.
- Resolution (Slice B): `RenderExtractionCache` now owns a per-entity
  `MeshPrimitiveViewSettings` map (cache-owned runtime/editor state, set via
  `SetMeshPrimitiveViewSettings` / `ClearMeshPrimitiveViewSettings` /
  `GetMeshPrimitiveViewSettings`; never in ECS components, never carrying
  graphics handles). While a mesh entity's surface is resident,
  `ExtractAndSubmit` reconciles each enabled view through
  `PackMeshEdgeView` / `PackMeshVertexView` into its **own** `GpuWorld` instance
  + `GpuGeometryHandle` recorded in the parent sidecar
  (`MeshEdgeView*`/`MeshVertexView*`), re-submitted to `m_Transforms` as an extra
  `GpuRender_Line`/`GpuRender_Point` unlit lane carrying the parent transform /
  bounds / material slot. Views repack on the shared mesh dirty signal (captured
  before `BindMeshGeometry` drains the tags), release on view-disable / parent
  eligibility-flip / destruction / shutdown through a shared
  `TickMeshPrimitiveViewGeometry` deferred-retire window (wired in
  `Engine::RunFrame`), and report fifteen `Mesh{Edge,Vertex}View*` +
  `MeshPrimitiveViewFreeRetires` counters. Fail-closed per view: a missing/invalid
  edge view drops just itself without disturbing the surface or the vertex view.
- Verification (2026-05-31, `ci` preset): `IntrinsicRuntimeContractTests` full
  suite 193/193 (12 new `MeshPrimitiveViewExtraction.*` cases + all existing
  mesh/graph/point-cloud/procedural residency suites green); layering,
  test-layout, doc-links, task-policy, and module-inventory (no diff — no new
  module) checks all clean. Graphics contract suite rebuilt + run for the
  consuming line/point flags (no graphics source changed).

## Slice plan
- **Slice A (landed).** Standalone `Extrinsic.Runtime.MeshPrimitiveViewPacker`:
  the runtime/editor control-surface type `MeshPrimitiveViewSettings`
  (`EnableEdgeView` / `EnableVertexView`), the 20-byte `MeshPrimitiveVertex`
  layout shared with the mesh/graph/point packers, the reusable
  `MeshPrimitiveViewBuffer` scratch, the fail-closed `MeshPrimitiveViewStatus`
  taxonomy, and the two derivation entry points: `PackMeshEdgeView`
  (line-list from `Vertices` positions + `Edges` `e:v0/e:v1`, endpoint-range
  validated, mirroring the graph line lane) and `PackMeshVertexView`
  (point list from `Vertices` positions, mirroring the point-cloud packer).
  Both read only the authoritative mesh `GeometrySources` — no CPU data
  duplication, no graphics imports beyond the existing `GpuWorld` value-type
  edge. Pure-CPU `contract;runtime` packer tests cover success lanes,
  zero-edge validity, and every fail-closed status. Preserves the CPU gate.
  Defers all residency/binding wiring to Slice B.
- **Slice B (landed).** Wire the views into `RenderExtractionCache`. Each enabled
  view binds through its own `GpuWorld` instance + `GpuGeometryHandle` recorded
  in the parent mesh's sidecar (the runtime-sidecar model — see the deferred
  decision below), reusing the retained line/point pipelines. Owns
  dirty-domain reupload (positions update both views; edge-topology updates the
  edge view's line indices), view enable/disable create/release, eligibility
  flips, deferred-retire + shutdown drain, and `MeshPrimitiveView*` diagnostics
  counters. Lands the `contract;runtime` extraction tests required below. Closes
  `Scaffolded → CPUContracted`. `Operational` visual proof stays owned by
  `RUNTIME-095`.

## Deferred decision (resolved at Slice B)
- **Representation model — RESOLVED: runtime sidecars.** Slice B implemented
  edge/vertex views as **runtime sidecars** attached to the parent mesh
  renderable (additional per-view `GpuGeometryHandle` + `GpuWorld` instance owned
  by `RenderExtractionCache`), *not* as child ECS entities. Rationale held: it
  mirrors the mesh/graph/point residency sidecar pattern, keeps all
  graphics-handle ownership in runtime (no ECS storage of graphics/RHI handles),
  and has the smaller blast radius (no ECS scene mutation/parenting/stable-identity
  duplication). Cross-entity authoring identity, if it ever becomes a hard
  requirement, is a future task, not a blocker for the CPUContracted lifecycle.
- **Settings storage — RESOLVED (new decision at Slice B).** The
  `MeshPrimitiveViewSettings` are stored in a `RenderExtractionCache`-owned
  `std::unordered_map<stableId, MeshPrimitiveViewSettings>` with a public
  `Set`/`Clear`/`Get` API, consumed during mesh binding. This is the natural
  reading of "runtime/editor state, consumed in `RenderExtractionCache`" and
  keeps the flags out of ECS components entirely. Settings are erased when the
  renderable is retired so the map does not accumulate stale entries.

## Goal
- Add runtime-owned lifecycle support that derives optional retained edge and vertex render views from a mesh entity so sandbox users can render/select mesh edges and vertices in addition to filled faces.

## Non-goals
- No base mesh surface residency (`RUNTIME-085`) implementation in this task.
- No graph or point-cloud residency (`RUNTIME-086`, `RUNTIME-087`).
- No graphics pass command work; retained line/point pass command routing is `GRAPHICS-071` and already done.
- No editor UI panel for toggling views; this task exposes runtime/editor-facing control seams only.
- No ECS storage of graphics/RHI handles.

## Context
- Owner/layer: `runtime`; it may read ECS mesh `GeometrySources` and attach CPU render-hint components or maintain sidecars, but graphics never reads live ECS.
- Upstream: `RUNTIME-085` provides surface mesh residency; `GRAPHICS-071` records line/point command bodies; `GRAPHICS-074` and `RUNTIME-093` consume primitive IDs for selection.
- Architecture docs describe mesh helper lines/points as views over authoritative mesh geometry; this task makes that explicit and testable for the promoted path.

## Required changes
- [x] Define a runtime control surface for mesh primitive views (`MeshPrimitiveViewSettings` in cache-owned runtime/editor state, set via `Set/Clear/GetMeshPrimitiveViewSettings`) that enables edge view, vertex view, or both for a mesh entity without moving policy into graphics. (Slice A defined the type; Slice B added the storage + consumption.)
- [x] Decided and implemented: edge/vertex views are **runtime sidecars** attached to the parent renderable (per-view `GpuWorld` instance + `GpuGeometryHandle`), not child ECS entities; trade-off documented in the deferred-decision section and `src/runtime/README.md`.
- [x] Edge views derive a line-list from mesh `GeometrySources::Edges` (`e:v0`/`e:v1`, endpoint-validated) via `PackMeshEdgeView`; halfedge-derived edges when explicit `Edges` are absent are a documented later concern (fails closed as `MissingEdgeTopology` for now).
- [x] Vertex views derive point geometry from mesh vertices via `PackMeshVertexView` and bind through a point lane; the parent mesh stable id is preserved on the view instance.
- [x] View lifetime stays synchronized with the parent mesh: create on enable, repack/reupload on the shared mesh dirty signal, release on disable / parent eligibility-flip / destruction / shutdown.
- [x] Diagnostics: `Mesh{Edge,Vertex}ViewUploads/ReuseHits/Reuploads/Releases/FailedPack/MissingPositions`, edge `MissingEdgeTopology`/`InvalidEdges`, and shared `MeshPrimitiveViewFreeRetires`.

## Tests
- [x] `contract;runtime` coverage for enabling edge view on a triangle mesh: extraction uploads a separate line renderable (`EnableEdgeViewUploadsSeparateLineRenderable`; deterministic edge count pinned by the Slice A packer tests).
- [x] `contract;runtime` coverage for enabling vertex view: extraction uploads a separate point renderable (`EnableVertexViewUploadsSeparatePointRenderable`); plus `EnableBothViewsAllocatesThreeIndependentRenderables`.
- [x] Dirty-domain coverage: vertex-position changes repack both views (`VertexPositionDirtyRepacksBothViews`); reuse on clean frames (`RepeatedExtractionReusesViewsWithoutReupload`).
- [x] Disable/destruction/shutdown/eligibility-flip cleanup coverage (`DisablingEdgeViewReleasesItsGeometryAfterWindow`, `EntityDestructionReleasesViewsAfterWindow`, `ShutdownReleasesViewResidency`, `ProceduralRefFlipReleasesViews`).
- [x] Invalid topology tests: missing edges (`MissingEdgeTopologyFailsEdgeViewButKeepsSurface`) and out-of-range endpoints (`OutOfRangeEdgeEndpointIncrementsInvalidEdgesCounter`); plus `NonMeshEntityWithSettingsCreatesNoViews`.

## Docs
- [x] Updated `src/runtime/README.md` with the runtime-sidecar mesh primitive view model (residency prose, counter list, test-seam fields, maintenance-phase tick).
- [x] `docs/architecture/rendering-three-pass.md` unchanged — the sidecar model reuses the existing retained line/point lanes and does not alter primitive-lane or selection-domain wording (selection is owned by `GRAPHICS-074`/`RUNTIME-093`).
- [x] `docs/api/generated/module_inventory.md` regenerated: no diff (no new module added; only existing `Runtime.RenderExtraction` / `Runtime.Engine` surfaces changed).

## Acceptance criteria
- [x] Runtime exposes mesh faces, edges, and vertices as three separate renderable instances/lanes while deriving all three from one authoritative mesh `GeometrySources`.
- [x] Mesh edge/vertex render views participate in retained line/point rendering (reusing the existing `GpuRender_Line`/`GpuRender_Point` lanes) with no graphics-side mesh-topology traversal; later selection is owned by `GRAPHICS-074`/`RUNTIME-093`.
- [x] View enable/disable, dirty updates, and cleanup are deterministic and covered by 12 `contract;runtime` cases.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract;runtime|contract;graphics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Adding mesh topology traversal or view creation to `src/graphics/*`.
- Storing graphics-owned handles in ECS components.
- Implementing editor UI panels or input policy in this runtime lifecycle task.
- Duplicating full mesh CPU data when a shared view or sidecar can preserve ownership cleanly.

## Maturity
- Target: `CPUContracted` for mesh primitive view lifecycle.
- `Operational` visual proof is owned by the final sandbox acceptance task after selection and present wiring land.

