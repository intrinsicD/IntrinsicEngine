# RUNTIME-085 — `GeometrySources` mesh residency bridge

## Goal
- Implement the runtime-owned bridge that converts promoted ECS mesh `GeometrySources` (`Vertices`/`Edges`/`Halfedges`/`Faces`) plus `Graphics::Components::RenderSurface` into retained `GpuWorld` geometry records and bound renderable instances, independent of asset-file ingest.

## Non-goals
- No CPU asset/model ingest (`ASSETIO-001`) and no asset-backed cache state machine (`GRAPHICS-034`).
- No graph or point-cloud residency; those are `RUNTIME-086` and `RUNTIME-087`.
- No mesh helper edge/vertex view lifecycle; that is `RUNTIME-088`.
- No graphics-side ECS imports or GPU handles stored in canonical ECS components.
- No deferred/PBR shader feature expansion beyond using the existing material/default-material seams.

## Context
- Owner/layer: `runtime`; consumes promoted ECS `GeometrySources` and writes graphics-owned handles only into `RenderExtractionCache` sidecars.
- Upstream completed contracts: `HARDEN-065` provides `GeometrySources` population and dirty-domain tags; `GRAPHICS-030B` proves the procedural `GpuWorld::UploadGeometry()` / `SetInstanceGeometry()` extraction pattern; `GRAPHICS-070` wires retained forward surface commands.
- This task covers runtime-authored/in-memory mesh entities. File-backed mesh assets still flow through `ASSETIO-001` and `GRAPHICS-034` once those land.
- Runtime must preserve `graphics/* -> no live ECS knowledge`: renderer/pass code consumes immutable snapshots and `GpuWorld` records only.

## Required changes
- [ ] Add a runtime mesh packer that reads canonical mesh properties (`v:position`, optional `v:normal`, `f:halfedge`, `h:to_vertex`, `h:next`, `h:face`) from `GeometrySources` and emits a `Graphics::GpuWorld::GeometryUploadDesc` with triangle-list indices and deterministic bounds.
- [ ] Extend `RenderExtractionCache::RenderableSidecar` with a mesh-source residency key/state that is distinct from procedural and asset-source residency.
- [ ] In `RenderExtractionCache::ExtractAndSubmit`, detect mesh-domain entities with `RenderSurface` and no `ProceduralGeometryRef` / no valid asset source, upload or reuse the mesh geometry, and call `GpuWorld::SetInstanceGeometry(instance, geometry)`.
- [ ] Drain relevant geometry dirty-domain tags for processed mesh entities and re-upload/rebind only when topology or vertex positions changed.
- [ ] Release and deferred-retire mesh geometry when the entity is destroyed or no longer qualifies as a mesh renderable.
- [ ] Add deterministic diagnostics to `RuntimeRenderExtractionStats` (for example `MeshGeometryUploads`, `MeshGeometryReuseHits`, `MeshGeometryReuploads`, `MeshGeometryInvalidTopology`, `MeshGeometryMissingPositions`, `MeshGeometryReleases`).
- [ ] Reject malformed meshes fail-closed: missing positions, out-of-range indices, degenerate faces with no triangles, non-finite positions, or empty geometry must not bind stale geometry.

## Tests
- [ ] Add `contract;runtime` coverage for a small triangle mesh populated through `GeometrySources::PopulateFromMesh`: extraction allocates one instance, uploads one mesh geometry, binds it, and submits one renderable snapshot.
- [ ] Add `contract;runtime` coverage for a quad/ngon mesh triangulation path if supported by the packer; otherwise assert unsupported faces are diagnosed explicitly.
- [ ] Add `contract;runtime` coverage for dirty vertex-position reupload and topology reupload counters.
- [ ] Add `contract;runtime` cleanup coverage proving entity removal releases the mesh residency sidecar and eventually calls `GpuWorld::FreeGeometry()` through the deferred retire path.
- [ ] Add malformed-input tests for missing positions, out-of-range halfedges/faces, empty meshes, and non-finite positions.
- [ ] No `gpu`/`vulkan` test in this slice; renderer visibility is covered by downstream default-recipe acceptance.

## Docs
- [ ] Update `src/runtime/README.md` to document the mesh `GeometrySources` residency bridge and diagnostics.
- [ ] Update `docs/architecture/rendering-three-pass.md` or `docs/architecture/graphics.md` only if the public residency contract changes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added.

## Acceptance criteria
- [ ] A promoted ECS mesh entity with `RenderSurface` becomes a bound `GpuWorld` surface renderable without asset ingest or procedural geometry.
- [ ] Geometry residency is runtime-owned and sidecar-based; ECS stores no graphics/RHI handles.
- [ ] Dirty-domain changes produce deterministic reupload/rebind behavior and diagnostics.
- [ ] Malformed mesh data fails closed without stale geometry remaining bound.

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
- Importing ECS, runtime, or asset-service ownership from `src/graphics/*`.
- Adding graphics/RHI handles to canonical ECS components.
- Mixing asset-backed residency or file IO into this runtime-authored mesh slice.
- Reusing procedural-geometry cache keys for mesh `GeometrySources` residency.
- Mixing mechanical moves with semantic residency changes.

## Maturity
- Target: `CPUContracted` for runtime mesh residency with CPU/null verification.
- `Operational` visual proof is owned by the final sandbox acceptance task after default-recipe pass and present wiring are complete.

