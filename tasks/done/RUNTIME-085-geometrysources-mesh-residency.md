# RUNTIME-085 — `GeometrySources` mesh residency bridge

## Status
- Status: retired (Slices A–C landed; Slice D closure check complete) on 2026-05-28.
- Maturity reached: `CPUContracted` (CPU/null contract gate green; `Operational` visual proof deferred to `RUNTIME-095`).
- Owner: unassigned.
- Branch: Slice A on `claude/optimistic-hypatia-yJ5qw`; Slice B on `claude/intrinsicengine-agent-onboarding-FLLuF`; Slice C on `claude/gallant-knuth-Y4iFV`; Slice D closure on `claude/serene-albattani-3KDrI`.
- Commits: Slice A `81b523e`/`a861919`; Slice B `8908afe`/`da90b11`; Slice C `63e2009`. Slice D closure: PR #943 on `claude/serene-albattani-3KDrI`.
- Promoted from `tasks/backlog/runtime/` on 2026-05-27 as the earliest unblocked Theme A working-sandbox leaf (all three active Theme B' rendering tasks GRAPHICS-076/077/078 parked on Vulkan-host blockers; HARDEN-065, GRAPHICS-030B, GRAPHICS-070/071 all retired in `tasks/done/`).
- Slice D closure verification (2026-05-28): full `IntrinsicTests` built under the `ci` preset; default CPU gate `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` reports 2322/2324 passed. The two failures are the pre-existing `IntrinsicBenchmarkSmoke.HalfedgeSmoke.Run`/`.Validate` (Not Run) cases unrelated to this task and unchanged across prior sessions. All 44 `MeshGeometryExtraction`/`MeshGeometryPackerTest` cases (including the 4 parameterized `MeshGeometryExtractionDirtyTag.DirtyTagTriggersReupload` cases, `MeshGeometryExtraction.MultipleDirtyTagsCoalesceIntoSingleReupload`, `MeshGeometryExtraction.ReuploadFailureKeepsExistingResidencyAndPreservesDirtyTag`, and the deferred-retire window cases) pass. Layering, test-layout, doc-links, task-policy strict checks all report 0 findings; module inventory unchanged at 450 modules.

## Slice plan
- **Slice A — Mesh packer module (landed).** Added `Extrinsic.Runtime.MeshGeometryPacker` providing `PackMesh(ConstSourceView, MeshPackBuffer&) -> MeshPackResult` with deterministic triangle-list output, local-space sphere bounds, and a fail-closed `MeshPackStatus` enum covering wrong-domain, missing positions, missing halfedge/face topology, out-of-range indices, non-finite positions, empty meshes, and degenerate face fans. CPU-only unit tests cover happy-path triangle + quad + ngon fan-triangulation, scratch-buffer reuse, determinism, and every failure mode. No changes to `Runtime.RenderExtraction` and no new `RuntimeRenderExtractionStats` counters yet; that is Slice B.
- **Slice B — Extraction wiring (this slice, landed).** Detects mesh-domain entities with `RenderSurface` and neither `ProceduralGeometryRef` nor an `AssetInstance::Source` in `RenderExtractionCache::ExtractAndSubmit`, calls the Slice A packer against a sidecar-owned `MeshPackBuffer` reused across ticks, uploads to `GpuWorld`, stores the resulting `GpuGeometryHandle` in a new sidecar-owned `MeshGeometry` field distinct from `ProceduralKey` / `HasSourceAsset`, calls `SetInstanceGeometry`, and adds the `MeshGeometryUploads` / `MeshGeometryReuseHits` / `MeshGeometryFailedPack` / `MeshGeometryMissingPositions` / `MeshGeometryInvalidTopology` / `MeshGeometryReleases` counters to `RuntimeRenderExtractionStats`. Subsequent frames short-circuit to the cached `MeshGeometry` handle and increment `MeshGeometryReuseHits` without re-packing; dirty-domain reupload is deferred to Slice C. `RetireMissingRenderables` and `Shutdown` free the runtime-owned mesh upload through `GpuWorld::FreeGeometry` and increment `MeshGeometryReleases` immediately (no deferred-retire window in Slice B); 11 new `contract;runtime` tests in `tests/contract/runtime/Test.MeshGeometryExtraction.cpp` cover the happy path, reuse semantics, two-entity independent uploads, entity destruction, shutdown release, procedural/asset preemption, the per-status counter routing for `MissingPositions`/`InvalidTopology`/`DegenerateAllFaces`, and the non-mesh-domain ignore path. `RenderableSidecarView` gains `MeshGeometry` and `HasMeshResidency` fields so tests can confirm which slot bound the entity without exposing the private sidecar layout.
- **Slice C — Dirty-domain reupload + retire ordering (landed).** `BindMeshGeometry` now takes the entity/registry, evaluates `any_of<GpuDirty, DirtyVertexPositions, DirtyFaceTopology, DirtyEdgeTopology>` on the entity, and on a dirty hit repacks via the existing `MeshPackBuffer`, uploads a fresh `GpuGeometryHandle`, swaps the instance binding via `SetInstanceGeometry`, enqueues the prior handle into a new runtime-owned mesh-residency retire queue (`MeshRetireRecord{Handle, Deadline, DeadlineSet}` mirroring `ProceduralGeometryCache::RetireRecord`), removes the consumed dirty tags, and increments `MeshGeometryReuploads` + `MeshGeometryReleases`. The eligibility-flip release (`!stillMeshAttached && sidecar->MeshGeometry.IsValid()`) and `RetireMissingRenderables` now enqueue rather than free immediately; eligibility flip also calls `SetInstanceGeometry(instance, {})` when no other path re-bound the instance, so the instance never observes the queued-but-still-live mesh slot. A new `RenderExtractionCache::TickMeshGeometry(currentFrame, framesInFlight, renderer)` mirrors `TickProceduralGeometry` semantics (anchor `Deadline = currentFrame + framesInFlight` on first observation, free on `Deadline <= currentFrame`) and is invoked from `Engine::RunFrame` alongside the procedural tick in the maintenance phase. `RuntimeRenderExtractionStats` gains `MeshGeometryReuploads` and `MeshGeometryFreeRetires`; `FreeRetires` is the per-tick delta of the running `m_MeshFreeRetires` accumulator. A dirty-reupload pack failure preserves the prior residency *and* the dirty tags so the caller has a chance to fix the input on a later frame, mirroring the fail-closed contract of the eligibility-flip release. `Shutdown` collapses the deferred window and drains pending retires inline. 18 of 115 `contract;runtime` tests now exercise the mesh path, including 4 parameterized dirty-tag cases (`GpuDirty`/`DirtyVertexPositions`/`DirtyFaceTopology`/`DirtyEdgeTopology`), a multi-tag coalesce test, a reupload-failure preservation test, and updated deferred-retire expectations on the destruction/eligibility-flip/lost-domain paths.
- **Slice D — Acceptance consolidation (landed).** Closed the task at `CPUContracted` after Slices A–C landed and the default CPU verification gate reported green (2322/2324; the 2 unrelated pre-existing benchmark-smoke failures unchanged). `Operational` visual proof is deferred to `RUNTIME-095`.

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
- [x] Add a runtime mesh packer that reads canonical mesh properties (`v:position`, optional `v:normal`, `f:halfedge`, `h:to_vertex`, `h:next`, `h:face`) from `GeometrySources` and emits a `Graphics::GpuWorld::GeometryUploadDesc` with triangle-list indices and deterministic bounds. (Slice A.)
- [x] Extend `RenderExtractionCache::RenderableSidecar` with a mesh-source residency key/state that is distinct from procedural and asset-source residency. (Slice B: added `MeshGeometry` handle + matching `MeshGeometry`/`HasMeshResidency` on `RenderableSidecarView`.)
- [x] In `RenderExtractionCache::ExtractAndSubmit`, detect mesh-domain entities with `RenderSurface` and no `ProceduralGeometryRef` / no valid asset source, upload or reuse the mesh geometry, and call `GpuWorld::SetInstanceGeometry(instance, geometry)`. (Slice B.)
- [x] Drain relevant geometry dirty-domain tags for processed mesh entities and re-upload/rebind only when topology or vertex positions changed. (Slice C — `BindMeshGeometry` evaluates `any_of<GpuDirty, DirtyVertexPositions, DirtyFaceTopology, DirtyEdgeTopology>` and repacks-then-replaces on a hit; the consumed tags are removed via `registry.remove<...>(entity)` on successful (re)upload.)
- [x] Release and deferred-retire mesh geometry when the entity is destroyed or no longer qualifies as a mesh renderable. (Slice C — release sites now enqueue into the runtime-owned `m_MeshRetire` queue and `TickMeshGeometry` frees the handles once the `framesInFlight` deadline elapses, mirroring `ProceduralGeometryCache::Tick`. Shutdown collapses the window and drains pending retires inline.)
- [x] Add deterministic diagnostics to `RuntimeRenderExtractionStats` (for example `MeshGeometryUploads`, `MeshGeometryReuseHits`, `MeshGeometryReuploads`, `MeshGeometryInvalidTopology`, `MeshGeometryMissingPositions`, `MeshGeometryReleases`, `MeshGeometryFreeRetires`). (Slice B added the per-frame counters; Slice C adds `MeshGeometryReuploads` for dirty-induced repack-and-replace events and `MeshGeometryFreeRetires` as the per-tick delta of actual `GpuWorld::FreeGeometry` calls fired from `TickMeshGeometry`.)
- [x] Reject malformed meshes fail-closed: missing positions, out-of-range indices, degenerate faces with no triangles, non-finite positions, or empty geometry must not bind stale geometry. (Slice B routes pack statuses to the correct counter and never calls `SetInstanceGeometry` on a failed pack.)

## Tests
- [x] Add `contract;runtime` coverage for a small triangle mesh populated through `GeometrySources`: extraction allocates one instance, uploads one mesh geometry, binds it, and submits one renderable snapshot. (Slice B — `MeshGeometryExtraction.SingleMeshEntityUploadsOnceAndBindsInstanceGeometry`.)
- [x] Add `contract;runtime` coverage for a quad/ngon mesh triangulation path if supported by the packer; otherwise assert unsupported faces are diagnosed explicitly. (Slice A — `MeshGeometryPackerTest.QuadFanTriangulatesIntoTwoTriangles`.)
- [x] Add `contract;runtime` coverage for dirty vertex-position reupload and topology reupload counters. (Slice C — parameterized `MeshGeometryExtractionDirtyTag.DirtyTagTriggersReupload` covers `GpuDirty`/`DirtyVertexPositions`/`DirtyFaceTopology`/`DirtyEdgeTopology` individually; `MeshGeometryExtraction.MultipleDirtyTagsCoalesceIntoSingleReupload` proves multi-tag entities still produce a single reupload event; `MeshGeometryExtraction.ReuploadFailureKeepsExistingResidencyAndPreservesDirtyTag` proves transient pack failures on a still-mesh-domain entity preserve the prior residency and the dirty tags; `MeshGeometryExtraction.EntityDestructionRetiresMeshGeometryAfterDeferredWindow` proves the deferred-retire window through `TickMeshGeometry`; the eligibility-flip tests now also drive `TickMeshGeometry` to verify the deferred-retire fires.)
- [x] Add `contract;runtime` cleanup coverage proving entity removal releases the mesh residency sidecar and eventually calls `GpuWorld::FreeGeometry()`. (Slice B — `MeshGeometryExtraction.EntityDestructionFreesMeshGeometryAndIncrementsRelease` + `ShutdownReleasesPendingMeshResidency`; the deferred-retire path is owned by Slice C.)
- [x] Add malformed-input tests for missing positions, out-of-range halfedges/faces, empty meshes, and non-finite positions. (Slice A covers them at the packer level; Slice B adds `MeshGeometryExtraction.MissingPositionsIncrementsMissingPositionsCounter` / `InvalidTopologyIncrementsInvalidTopologyCounter` / `DegenerateAllFacesIncrementsFailedPackCounter` for the extraction-layer counter routing.)
- [x] No `gpu`/`vulkan` test in this slice; renderer visibility is covered by downstream default-recipe acceptance.

## Docs
- [x] Update `src/runtime/README.md` to document the mesh `GeometrySources` residency bridge and diagnostics. (Slice B extends the `RenderExtractionCache` section.)
- [x] Update `docs/architecture/rendering-three-pass.md` or `docs/architecture/graphics.md` only if the public residency contract changes. (Resolved as not required: Slices A–C reuse the existing `GpuWorld::UploadGeometry`/`SetInstanceGeometry`/`FreeGeometry` surface without changing the public residency contract, so no architecture-doc change is owed.)
- [x] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added. (Slice A bumped the inventory to 449 modules; Slice B adds no new modules.)

## Acceptance criteria
- [x] A promoted ECS mesh entity with `RenderSurface` becomes a bound `GpuWorld` surface renderable without asset ingest or procedural geometry. (Slice B.)
- [x] Geometry residency is runtime-owned and sidecar-based; ECS stores no graphics/RHI handles. (Slice B stores the `GpuGeometryHandle` only on the cache's private `RenderableSidecar`.)
- [x] Dirty-domain changes produce deterministic reupload/rebind behavior and diagnostics. (Slice C.)
- [x] Malformed mesh data fails closed without stale geometry remaining bound. (Slice B.)

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

