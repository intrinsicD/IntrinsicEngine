# RUNTIME-087 — `GeometrySources` point-cloud residency bridge

## Status
- `done` — retired to `tasks/done/` on 2026-05-30 at maturity `CPUContracted`.
- Owner: agent. Branch: `claude/intrinsicengine-agent-onboarding-efVu8`.
- PR/commit: landed on `claude/intrinsicengine-agent-onboarding-efVu8`. PR: _TBD_.
- Landed as one robust slice mirroring the retired `RUNTIME-086` (graph
  residency): the point-cloud upload path is not leak-free without the
  retire/shutdown lifecycle, so the standalone `Extrinsic.Runtime.
  PointCloudGeometryPacker`, `RenderExtractionCache` residency wiring,
  deferred-retire window, and shutdown drain landed together. Point clouds are
  simpler than graphs (positions only, no edge/line lane, so no lane-mask
  repack).
- Resolution: `RenderExtractionCache::ExtractAndSubmit` now routes
  `Domain::PointCloud` entities that carry `RenderPoints` through
  `BindPointCloudGeometry` (upload/reuse/dirty-reupload), owns the per-entity
  `PointCloudGeometry` handle (distinct from `MeshGeometry`/`GraphGeometry`),
  drains the cloud dirty-domain tags (`DirtyVertexPositions`,
  `DirtyVertexAttributes`, `GpuDirty`), releases on eligibility flip /
  destruction / shutdown through `EnqueuePointCloudRetire` + the
  `TickPointCloudGeometry` deferred-retire window (wired in `Engine::RunFrame`
  maintenance phase), and reports eight `PointCloudGeometry*` counters. Only a
  uniform float `RenderPoints::SizeSource` is supported; a per-point size buffer
  (string) fails closed into `PointCloudGeometryFailedPack`. A point-cloud-domain
  entity without `RenderPoints` is intentionally not a renderable, so a mesh that
  loses topology back to a bare vertex set is not silently re-bound as points.
- Verification (2026-05-30, `ci` preset): focused
  `-R 'PointCloudGeometry|GraphGeometry|MeshGeometry'` 96/96; full default CPU
  gate (`ctest -LE 'gpu|vulkan|slow|flaky-quarantine'`) 2394/2396 — only the two
  pre-existing unrelated `IntrinsicBenchmarkSmoke.HalfedgeSmoke` Run/Validate
  Not-Run failures remain. Layering, test-layout, doc-links, task-policy, and
  module-inventory (regenerated) checks all clean.
- 2026-05-30 (post-review fix, same PR): tightened the shared fail-closed
  contract across all three residency bridges (mesh/graph/point-cloud). A
  dirty-reupload pack/upload failure — or, for point clouds, switching a resident
  cloud to an unsupported per-point size source — now releases the prior
  residency (deferred-retire + instance detach + `*GeometryReleases`) instead of
  leaving stale geometry bound; the dirty tags stay set for later recovery. This
  touches the retired `RUNTIME-085`/`RUNTIME-086` bridges for consistency: the
  prior "preserve stale residency on dirty-reupload failure" behavior rendered
  authoritative-but-invalid data until the input recovered. `src/runtime/README.md`
  prose and the mesh/graph/point-cloud regression tests
  (`ReuploadFailureReleasesStaleResidency*`,
  `SwitchingToUnsupportedSizeSourceReleasesResidency`) updated to match.
- `Operational` visual proof is owned by the final working-sandbox acceptance
  task (`RUNTIME-095`) after default point and present wiring complete.

## Goal
- Implement the runtime-owned bridge that converts promoted ECS point-cloud `GeometrySources::Vertices` plus `Graphics::Components::RenderPoints` into retained `GpuWorld` point geometry bindings.

## Non-goals
- No mesh residency (`RUNTIME-085`) or graph residency (`RUNTIME-086`).
- No point-cloud file ingest; CPU import routing is owned by `ASSETIO-001` consuming geometry IO.
- No Gaussian splatting / anisotropic covariance rendering (`GRAPHICS-048`).
- No visualization adapter or point-color/radius UI beyond preserving data needed by later `RUNTIME-083`.
- No graphics-side ECS imports or GPU handles in ECS components.

## Context
- Owner/layer: `runtime`; reads ECS `GeometrySources`, owns sidecars, uploads through graphics public `GpuWorld` seams.
- Upstream completed contracts: `HARDEN-065` can populate cloud `GeometrySources`; `GRAPHICS-071` wires retained point command recording; `RenderPoints` already carries render mode and size-source metadata.
- The initial bridge should make point-cloud positions visible as retained point primitives; richer attribute/radius/color paths can build on the same sidecar and dirty-domain policy.

## Required changes
- [x] Add a runtime point-cloud packer that reads canonical `v:position` from `GeometrySources::Vertices`, validates finiteness, computes bounds, and emits a point-compatible `GeometryUploadDesc`.
- [x] Define how `RenderPoints::SizeSource` maps into the initial GPU config: uniform radius only in this slice unless per-point radius upload is explicitly implemented and tested.
- [x] Extend `RenderExtractionCache::ExtractAndSubmit` to detect point-cloud-domain entities with `RenderPoints`, upload/reuse cloud geometry, set `GpuRender_Point`, and bind geometry before snapshot submission.
- [x] Drain point-cloud dirty tags (`DirtyVertexPositions`, `DirtyVertexAttributes`, `GpuDirty`) for processed entities.
- [x] Release/deferred-retire point-cloud geometry when entities disappear or lose `RenderPoints`.
- [x] Add diagnostics such as `PointCloudGeometryUploads`, `PointCloudGeometryReuseHits`, `PointCloudGeometryReuploads`, `PointCloudGeometryMissingPositions`, `PointCloudGeometryInvalidPoints`, `PointCloudGeometryReleases`.
- [x] Fail closed for empty clouds, missing positions, non-finite points, unsupported size-source variants, and invalid render modes.

## Tests
- [x] Add `contract;runtime` coverage for a small point cloud populated through `GeometrySources::PopulateFromCloud`: extraction uploads/binds point geometry and submits one point renderable.
- [x] Add dirty vertex-position reupload coverage.
- [x] Add tests for uniform size-source propagation or deterministic unsupported-size diagnostics.
- [x] Add malformed cloud tests for empty input, missing positions, and non-finite coordinates.
- [x] No `gpu`/`vulkan` test in this slice.

## Docs
- [x] Update `src/runtime/README.md` with point-cloud residency ownership, sidecar state, and diagnostics.
- [x] Update `docs/architecture/rendering-three-pass.md` only if the point primitive contract changes.
- [x] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added.

## Acceptance criteria
- [x] A promoted ECS point-cloud entity with `RenderPoints` becomes a bound retained point renderable without asset ingest.
- [x] Runtime owns all point-cloud-to-GPU mapping; ECS remains CPU-only.
- [x] Dirty point positions trigger deterministic reupload/rebind.
- [x] Invalid point-cloud data fails closed without stale geometry.

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
- Importing point-cloud containers or ECS ownership from `src/graphics/*`.
- Storing GPU/RHI handles in ECS point-cloud/source components.
- Implementing Gaussian splatting or transparency/OIT in this residency task.
- Mixing file import or asset-backed cache state into this runtime-authored point-cloud slice.

## Maturity
- Target: `CPUContracted` for retained point-cloud residency with CPU/null verification.
- `Operational` visual proof is owned by the final sandbox acceptance task after default point and present wiring are complete.

