# RUNTIME-087 — `GeometrySources` point-cloud residency bridge

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
- [ ] Add a runtime point-cloud packer that reads canonical `v:position` from `GeometrySources::Vertices`, validates finiteness, computes bounds, and emits a point-compatible `GeometryUploadDesc`.
- [ ] Define how `RenderPoints::SizeSource` maps into the initial GPU config: uniform radius only in this slice unless per-point radius upload is explicitly implemented and tested.
- [ ] Extend `RenderExtractionCache::ExtractAndSubmit` to detect point-cloud-domain entities with `RenderPoints`, upload/reuse cloud geometry, set `GpuRender_Point`, and bind geometry before snapshot submission.
- [ ] Drain point-cloud dirty tags (`DirtyVertexPositions`, `DirtyVertexAttributes`, `GpuDirty`) for processed entities.
- [ ] Release/deferred-retire point-cloud geometry when entities disappear or lose `RenderPoints`.
- [ ] Add diagnostics such as `PointCloudGeometryUploads`, `PointCloudGeometryReuseHits`, `PointCloudGeometryReuploads`, `PointCloudGeometryMissingPositions`, `PointCloudGeometryInvalidPoints`, `PointCloudGeometryReleases`.
- [ ] Fail closed for empty clouds, missing positions, non-finite points, unsupported size-source variants, and invalid render modes.

## Tests
- [ ] Add `contract;runtime` coverage for a small point cloud populated through `GeometrySources::PopulateFromCloud`: extraction uploads/binds point geometry and submits one point renderable.
- [ ] Add dirty vertex-position reupload coverage.
- [ ] Add tests for uniform size-source propagation or deterministic unsupported-size diagnostics.
- [ ] Add malformed cloud tests for empty input, missing positions, and non-finite coordinates.
- [ ] No `gpu`/`vulkan` test in this slice.

## Docs
- [ ] Update `src/runtime/README.md` with point-cloud residency ownership, sidecar state, and diagnostics.
- [ ] Update `docs/architecture/rendering-three-pass.md` only if the point primitive contract changes.
- [ ] Refresh `docs/api/generated/module_inventory.md` if new runtime modules are added.

## Acceptance criteria
- [ ] A promoted ECS point-cloud entity with `RenderPoints` becomes a bound retained point renderable without asset ingest.
- [ ] Runtime owns all point-cloud-to-GPU mapping; ECS remains CPU-only.
- [ ] Dirty point positions trigger deterministic reupload/rebind.
- [ ] Invalid point-cloud data fails closed without stale geometry.

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

