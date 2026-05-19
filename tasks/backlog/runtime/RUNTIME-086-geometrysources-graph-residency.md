# RUNTIME-086 — `GeometrySources` graph residency bridge

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
- [ ] Add a runtime graph packer that reads canonical node positions (`v:position` on `Nodes`) and edge endpoints (`e:v0`, `e:v1` on `Edges`) and emits upload descriptors for line-list edge indices and point-list node positions.
- [ ] Define the sidecar model for graph entities that request only lines, only points, or both; document whether one `GpuGeometryHandle` or two handles are used and how they bind to the single renderable instance contract.
- [ ] Extend `RenderExtractionCache::ExtractAndSubmit` to detect graph-domain entities with `RenderLines` and/or `RenderPoints`, upload/reuse graph geometry, set line/point render flags, and bind valid geometry before snapshot submission.
- [ ] Drain graph-relevant dirty tags (`DirtyVertexPositions`, `DirtyEdgeTopology`, `DirtyVertexAttributes`, `DirtyEdgeAttributes`, `GpuDirty`) for processed graph entities.
- [ ] Release/deferred-retire graph residency when graph entities disappear or no longer carry graph render hints.
- [ ] Add diagnostics such as `GraphGeometryUploads`, `GraphGeometryReuseHits`, `GraphGeometryReuploads`, `GraphGeometryInvalidEdges`, `GraphGeometryMissingNodes`, `GraphGeometryReleases`.
- [ ] Fail closed for empty graphs, missing node positions, non-finite positions, out-of-range edge endpoints, and contradictory render-source hints.

## Tests
- [ ] Add `contract;runtime` coverage for a two-node/one-edge graph with `RenderLines`: extraction uploads/binds line geometry and submits one line renderable.
- [ ] Add `contract;runtime` coverage for a graph with `RenderPoints`: extraction uploads/binds point geometry and submits one point renderable.
- [ ] Add `contract;runtime` coverage for a graph with both hints and deterministic line/point render flags.
- [ ] Add dirty topology and position reupload tests.
- [ ] Add malformed graph tests for missing positions, out-of-range endpoints, empty graph, and non-finite coordinates.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` with graph residency ownership, sidecar state, and diagnostics.
- [ ] Update `docs/architecture/rendering-three-pass.md` only if the graph line/point residency contract changes the documented primitive lanes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added.

## Acceptance criteria
- [ ] A promoted ECS graph entity can render as retained lines and/or points through runtime-owned `GpuWorld` residency.
- [ ] Runtime sidecars own all graph-to-GPU handle mapping; ECS remains CPU-only.
- [ ] Dirty graph topology/position changes are deterministic and tested.
- [ ] Invalid graph data cannot leave stale geometry bound.

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
- Importing graph containers or ECS ownership from `src/graphics/*`.
- Storing `GpuGeometryHandle`, `GpuInstanceHandle`, RHI handles, or bindless indices in canonical ECS graph/source components.
- Folding transient vector-field overlays into retained graph cull buckets; `RUNTIME-083` / `GRAPHICS-078` own visualization overlays.
- Mixing graph file ingest with runtime residency.

## Maturity
- Target: `CPUContracted` for retained graph residency with CPU/null verification.
- `Operational` visual proof is owned by the final sandbox acceptance task after default line/point and present wiring are complete.

