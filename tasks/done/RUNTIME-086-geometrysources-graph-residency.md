# RUNTIME-086 — `GeometrySources` graph residency bridge

## Status
- Commits: Slice A landed earlier on merged Theme A history
  (`fade19e` runtime graph geometry packer); Slices B + C on branch
  `claude/intrinsicengine-agent-onboarding-c9ql3` (this PR). PR: _TBD_.
- 2026-05-30 (post-review fix, same PR): the `BindGraphGeometry` reuse guard
  now tracks the packed render-lane mask (`RenderLines` / `RenderPoints`) and
  repacks when the requested lanes change even without a geometry dirty tag —
  otherwise a points-only graph that later gains `RenderLines` would rebind a
  lineless upload. Covered by
  `GainingLineHintRepacksGraphWithoutDirtyTag` /
  `LosingLineHintRepacksGraphWithoutDirtyTag`.
- `done` (retired 2026-05-30, maturity `CPUContracted`). Branch
  `claude/intrinsicengine-agent-onboarding-c9ql3`. Verified against the default
  CPU gate: `IntrinsicTests` builds clean under the `ci` preset; the focused
  `-R 'GraphGeometry|MeshGeometry'` contract+runtime suite is 73/73; the full
  default gate (`-LE 'gpu|vulkan|slow|flaky-quarantine'`) is 2371/2373 with only
  the two pre-existing, unrelated `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run`/
  `.Validate` (Not Run) failures. Layering, test-layout, doc-links, task-policy,
  and module-inventory (no diff) checks are clean. `Operational` visual proof
  remains owned by the final working-sandbox acceptance task.
- `in-progress`. Owner: `claude/intrinsicengine-agent-onboarding-c9ql3`.
  Branch: `claude/intrinsicengine-agent-onboarding-c9ql3`. Promoted from
  `tasks/backlog/runtime/` on 2026-05-29 as the next unblocked Theme A
  working-sandbox leaf after `RUNTIME-085` (mesh residency) retired; all
  cross-domain anchors (`HARDEN-065`, `GRAPHICS-030B`, `GRAPHICS-070/071`)
  are in `tasks/done/`.
- 2026-05-30: Slices B and C landed together in one slice. The extraction
  upload path is not leak-free without the retire/shutdown lifecycle, and the
  mesh template (`RUNTIME-085`) implements upload + reuse + dirty-reupload +
  eligibility-flip + deferred-retire + shutdown as a single inseparable
  residency lifecycle. Splitting a graph upload (B) from its retire path (C)
  would leak `GpuGeometryHandle`s on graph-entity destruction, so the smallest
  *robust* slice is the full lifecycle. Implemented by mirroring the mesh
  bridge: `BindGraphGeometry`, `EnqueueGraphRetire`, `TickGraphGeometry`,
  `GraphGeometry*` counters, the `RenderableSidecar.GraphGeometry` field, and
  the `Engine::RunFrame` maintenance tick. Next: retire to `tasks/done/` after
  the default CPU gate passes.

## Slice plan
- **Slice A (this slice):** Add the runtime graph packer
  (`Extrinsic.Runtime.GraphGeometryPacker`) that converts a graph-domain
  `ConstSourceView` into one canonical `GpuWorld::GeometryUploadDesc`
  holding node-position vertices (point lane) plus optional edge line
  indices (line lane), with fail-closed status codes and `contract;runtime`
  packer unit tests. No extraction wiring yet.
- **Slice B:** Wire `RenderExtractionCache::ExtractAndSubmit` to detect
  graph-domain entities carrying `RenderLines`/`RenderPoints`, upload/reuse
  the packed handle, set line/point render flags, bind instance geometry,
  add the `GraphGeometry*` diagnostics counters, and handle
  eligibility-flip releases.
- **Slice C:** Dirty-domain reupload (`DirtyVertexPositions`,
  `DirtyVertexAttributes`, `DirtyEdgeTopology`, `GpuDirty`) and the
  deferred-retire window mirroring `TickMeshGeometry`.

### Clarification (nonblocking)
- The task body lists a `DirtyEdgeAttributes` tag. No such tag exists in
  `ECS.Component.DirtyTags`; the available edge tag is `DirtyEdgeTopology`.
  Slice C drains the available tags (`DirtyVertexPositions`,
  `DirtyVertexAttributes`, `DirtyEdgeTopology`, `GpuDirty`).
- Single `GpuGeometryHandle` per graph entity: node positions are the
  shared vertex buffer; the point lane draws the vertex buffer directly and
  the line lane indexes it via `LineIndices`. This matches the canonical
  single-renderable-instance contract.

## Goal
- Implement the runtime-owned bridge that converts promoted ECS graph `GeometrySources` (`Nodes` + `Edges` + `HasGraphTopology`) plus `RenderLines` / `RenderPoints` hints into retained `GpuWorld` line and point geometry bindings.

## Non-goals
- No mesh residency (`RUNTIME-085`) or point-cloud residency (`RUNTIME-087`).
- No graph file ingest; graph import/export remains geometry/assets-owned (`ASSETIO-001` consumes geometry IO when file-backed ingest lands).
- No visualization-vector-field overlay baking; that is `RUNTIME-083` and `GRAPHICS-078`.
- No editor graph styling UI.
- No graphics-side live ECS access or GPU handles in ECS components.

## Context
- Owner/layer: `runtime`; reads ECS `GeometrySources` and owns extraction sidecars / residency maps.
- Upstream completed contracts: `HARDEN-065` can populate graph `GeometrySources`; `GRAPHICS-071` wires retained line/point command bodies; `Runtime.RenderExtraction` already sets `GpuRender_Line` and `GpuRender_Point` when render hints are present.
- Graph rendering should treat edges and nodes as equal retained renderable lanes: `RenderLines` drives edge geometry; `RenderPoints` drives node geometry.
- The bridge must preserve the canonical instance-slot contract: cull buckets use `firstInstance` as the renderable identity and graphics never queries live graph data.

## Required changes
- [x] Add a runtime graph packer that reads canonical node positions (`v:position` on `Nodes`) and edge endpoints (`e:v0`, `e:v1` on `Edges`) and emits upload descriptors for line-list edge indices and point-list node positions. _(Slice A: `Extrinsic.Runtime.GraphGeometryPacker::PackGraph`.)_
- [x] Define the sidecar model for graph entities that request only lines, only points, or both; document whether one `GpuGeometryHandle` or two handles are used and how they bind to the single renderable instance contract. _(Decision recorded in Slice plan + `src/runtime/README.md`: one handle; node positions are the shared vertex buffer, point lane draws it directly, line lane indexes it via `LineIndices`. Sidecar wiring lands in Slice B.)_
- [x] Extend `RenderExtractionCache::ExtractAndSubmit` to detect graph-domain entities with `RenderLines` and/or `RenderPoints`, upload/reuse graph geometry, set line/point render flags, and bind valid geometry before snapshot submission. _(Slice B: `BindGraphGeometry`; render flags already set by the existing `BuildRenderFlags` from `RenderLines`/`RenderPoints` presence.)_
- [x] Drain graph-relevant dirty tags (`DirtyVertexPositions`, `DirtyEdgeTopology`, `DirtyVertexAttributes`, `GpuDirty`) for processed graph entities. _(Slice C: `BindGraphGeometry` drains the four tags on (re)upload. Note: no `DirtyEdgeAttributes` tag exists; see Slice plan clarification.)_
- [x] Release/deferred-retire graph residency when graph entities disappear or no longer carry graph render hints. _(Slice C: eligibility-flip release + `RetireMissingRenderables` + `Shutdown` route through `EnqueueGraphRetire`/`TickGraphGeometry`.)_
- [x] Add diagnostics such as `GraphGeometryUploads`, `GraphGeometryReuseHits`, `GraphGeometryReuploads`, `GraphGeometryInvalidEdges`, `GraphGeometryMissingNodes`, `GraphGeometryReleases`. _(Slice B/C: plus `GraphGeometryFailedPack` and `GraphGeometryFreeRetires`.)_
- [x] Fail closed for empty graphs, missing node positions, non-finite positions, out-of-range edge endpoints, and contradictory render-source hints. _(Slice A: `GraphPackStatus` covers `EmptyGraph`, `MissingNodes`, `NonFinitePosition`, `InvalidEdge`, `MissingEdgeTopology`, `NoRenderLane`.)_

## Tests
- [x] Add `contract;runtime` coverage for a two-node/one-edge graph with `RenderLines`: extraction uploads/binds line geometry and submits one line renderable. _(Slice B: `Test.GraphGeometryExtraction.cpp::LineGraphUploadsOnceAndBindsInstanceGeometry`.)_
- [x] Add `contract;runtime` coverage for a graph with `RenderPoints`: extraction uploads/binds point geometry and submits one point renderable. _(Slice B: `PointGraphUploadsOnceAndBindsInstanceGeometry`.)_
- [x] Add `contract;runtime` coverage for a graph with both hints and deterministic line/point render flags. _(Slice B: `LineAndPointGraphUploadsSingleHandleForBothLanes` — one upload / one handle bound; render flags are produced by the unchanged `BuildRenderFlags`.)_
- [x] Add dirty topology and position reupload tests. _(Slice C: parameterized `GraphGeometryExtractionDirtyTag` over `GpuDirty`/`DirtyVertexPositions`/`DirtyVertexAttributes`/`DirtyEdgeTopology`.)_
- [x] Add malformed graph tests for missing positions, out-of-range endpoints, empty graph, and non-finite coordinates. _(Slice A: `Test.GraphGeometryPacker.cpp`.)_
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/runtime/README.md` with graph residency ownership, sidecar state, and diagnostics. _(Slice A: packer row added; residency/diagnostics prose extended in Slice B.)_
- [x] Update `docs/architecture/rendering-three-pass.md` only if the graph line/point residency contract changes the documented primitive lanes. _(No change needed: reuses the existing retained line/point lanes; decision recorded, no edit required.)_
- [x] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added.

## Acceptance criteria
- [x] A promoted ECS graph entity can render as retained lines and/or points through runtime-owned `GpuWorld` residency. _(Slice B: `BindGraphGeometry` uploads + binds; `Operational` visual proof is owned by the final sandbox acceptance task.)_
- [x] Runtime sidecars own all graph-to-GPU handle mapping; ECS remains CPU-only. _(Slice B: `RenderableSidecar.GraphGeometry`; no GPU handles stored in ECS.)_
- [x] Dirty graph topology/position changes are deterministic and tested. _(Slice C: parameterized dirty-tag reupload coverage.)_
- [x] Invalid graph data cannot leave stale geometry bound. _(Slice A packer fails closed; Slice B/C keep prior residency on transient pack failure and never bind an invalid upload — `MissingNodePositions`/`OutOfRangeEdge`/`SurfaceOnlyGraph` coverage.)_

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
# ctest label flags are matched per-label (AND), not as a single regex with ';'.
ctest --test-dir build/ci --output-on-failure -L contract -L runtime -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GraphGeometryPacker' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Importing graph containers or ECS ownership from `src/graphics/*`.
- Storing `GpuGeometryHandle`, `GpuInstanceHandle`, RHI handles, or bindless indices in canonical ECS graph/source components.
- Folding transient vector-field overlays into retained graph cull buckets; `RUNTIME-083` / `GRAPHICS-078` own visualization overlays.
- Mixing graph file ingest with runtime residency.

## Maturity
- Target: `CPUContracted` for retained graph residency with CPU/null verification.
- `Operational` visual proof is owned by the final sandbox acceptance task after default line/point and present wiring are complete.

