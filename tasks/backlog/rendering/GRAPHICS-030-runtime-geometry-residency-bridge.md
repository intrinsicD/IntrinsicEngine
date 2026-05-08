# GRAPHICS-030 — Runtime ECS-to-GpuWorld geometry residency bridge with procedural triangle test seam

## Goal
Implement the runtime-owned residency bridge that uploads procedural geometry for renderable ECS entities and binds it via `GpuWorld::SetInstanceGeometry()`, so that the candidate produced by GRAPHICS-029 carries a valid instance/geometry binding inside `GpuWorld` even when the device is the null backend. This is the first concrete implementation slice of the GRAPHICS-028 planning contract.

## Non-goals
- No asset-backed mesh residency (`AssetInstance::Source` → `GpuAssetCache` → `GpuWorld`); that lives in GRAPHICS-034.
- No new render passes, shader changes, material registry expansion, or pipeline selection (GRAPHICS-031 / GRAPHICS-032 own those).
- No live ECS knowledge inside `src/graphics/*`; bridge stays in `runtime` per AGENTS.md section 2.
- No new GPU-typed ECS components; residency state is runtime-owned sidecar/cache data per GRAPHICS-028.
- No `GpuWorld` capacity, compaction, or eviction policy changes (those live with GRAPHICS-004/005).
- No procedural primitive expansion beyond what is needed for the minimal triangle test seam.

## Context
- Owner layer: `runtime`. Implementation home is `Runtime.RenderExtraction` (or a sibling `Runtime.RenderResidency`) — the only writer of the renderable sidecar/cache per GRAPHICS-028.
- `src/graphics/renderer/Graphics.GpuWorld.cppm` already exposes the low-level primitives this bridge requires: `UploadGeometry(const GeometryUploadDesc&)`, `FreeGeometry(GpuGeometryHandle)`, and `SetInstanceGeometry(GpuInstanceHandle, GpuGeometryHandle)`.
- `tests/contract/graphics/Test.MinimalTriangleAcceptance.cpp` proves the CPU-side renderer world can host a triangle today, but the path is invoked manually and is not wired from runtime extraction.
- `src/runtime/Runtime.RenderExtraction.cppm` currently allocates a renderable sidecar and submits transform/light/visualization snapshots, but never calls `UploadGeometry` or `SetInstanceGeometry` for the candidate.
- The 2026-05-08 review (`docs/reviews/2026-05-08-sandbox-geometry-rendering-gap-analysis.md`, sections "Exact missing pieces / 2 + 3" and "minimal milestone plan / 2. Geometry residency milestone") records this as the next gap after the bootstrap from GRAPHICS-029.
- `Extrinsic::Graphics::Components::GpuSceneSlot` already carries `InstanceSlot/Generation` and `GeometrySlot/Generation`; this task uses that field set without expanding it (asset-generation tracking landed in GRAPHICS-023A and rebind in GRAPHICS-023B/C/D).

## Required changes
- Define a runtime-owned procedural geometry source for the bridge:
  - A small CPU-side descriptor (e.g. `Runtime::ProceduralTriangleDescriptor`) used by the reference scene bootstrap from GRAPHICS-029 to mark its renderable as backed by a runtime-owned procedural primitive.
  - A runtime cache keyed by descriptor identity that performs `GpuWorld::UploadGeometry()` once and reuses the resulting `GpuGeometryHandle` across instances; freeing happens when the last referencing renderable is destroyed.
- Extend `Runtime.RenderExtraction` to:
  - Detect the procedural geometry source on a candidate.
  - Upload (or look up) the geometry through the runtime residency cache.
  - Allocate the `GpuInstanceHandle` if not yet present, store/refresh `GpuSceneSlot` in the runtime sidecar, and call `GpuWorld::SetInstanceGeometry(instance, geometry)`.
  - Free geometry through the runtime cache on entity removal or candidate disqualification, observing existing `GpuWorld` and `GpuAssetCache` retire-queue ordering rules.
- Keep the asset-source path (`AssetInstance::Source`) unchanged in this slice; the bridge handles only procedural sources here so that GRAPHICS-034 can layer the asset path on top without rework.
- Cross-link decisions with GRAPHICS-004 (allocation/lifetime), GRAPHICS-016 (extraction handoff), GRAPHICS-023A/B/C/D (asset-generation observation/rebind) and GRAPHICS-028 (residency planning).

## Tests
- Extend the runtime extraction contract suite under `tests/contract/runtime/` to assert that, with the reference scene from GRAPHICS-029 enabled, one extraction tick yields:
  - one renderable sidecar entry,
  - one valid `GpuInstanceHandle`,
  - one valid `GpuGeometryHandle`,
  - and a bound instance/geometry relationship in `GpuWorld` (queryable via existing `GpuWorld` introspection or by reproducing the assertions in `Test.MinimalTriangleAcceptance.cpp`).
- Add a runtime-side regression test that destroying the renderable entity drops the sidecar, releases the instance, and (when the descriptor refcount hits zero) frees the geometry through the runtime cache without violating `GpuWorld` deferred-free semantics.
- Tests stay `contract;runtime` (and `contract;graphics` where the assertion crosses into renderer state); GPU-labeled coverage is not required for this slice.
- Verification gate: default CPU-supported correctness target.
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` "ECS renderable residency bridge" section to record that the procedural-source path is implemented and that asset-source residency remains follow-up work in GRAPHICS-034.
- Update `src/runtime/README.md` and `src/graphics/renderer/README.md` cross-links for the new procedural-source residency seam.
- Update `tasks/backlog/rendering/README.md` DAG to insert this task.
- Update `docs/migration/nonlegacy-parity-matrix.md` row(s) for renderable residency progress.

## Acceptance criteria
- A reference-scene triangle entity, after one extraction tick, has a runtime sidecar with valid `GpuInstanceHandle` and `GpuGeometryHandle`, and `GpuWorld` reports the instance/geometry binding.
- Procedural geometry residency obeys descriptor refcount semantics; freeing is correct under entity destruction and re-creation.
- Layering invariants hold: no `graphics/*` imports in `src/ecs/*`, no live ECS access in `src/graphics/*`, residency cache state lives only in `runtime`.
- All new tests pass under the default CPU gate; null device path remains the supported execution path for this slice.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- No GPU-typed ECS components.
- No live ECS access from graphics layers.
- No asset-backed mesh residency in this slice.
- No renderer pass body or device backend changes.
- No mixing of mechanical file moves with semantic refactors.
