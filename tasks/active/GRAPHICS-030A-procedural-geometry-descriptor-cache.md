# GRAPHICS-030A — Procedural geometry descriptor, cache, and triangle packer

## Goal
- Implement the first runtime-owned procedural geometry slice from `GRAPHICS-030`: add the CPU-only procedural geometry authoring component, runtime descriptor/cache types, and the triangle packer without wiring extraction yet.

## Non-goals
- No `Runtime.RenderExtraction` detection or `ExtractAndSubmit()` lifecycle wiring; that is `GRAPHICS-030-Impl-B`.
- No reference scene provider implementation; `GRAPHICS-029-Impl-A/B` consume this once the component exists.
- No renderer pass bodies, shader/material fallback, or minimal recipe work; those are `GRAPHICS-031` / `GRAPHICS-032` implementation children.
- No Vulkan operational-readiness work; that is `GRAPHICS-033` implementation work.
- No asset-backed mesh residency; that is `GRAPHICS-034` and depends on asset ingest ownership.
- No GPU-typed fields in canonical ECS components.

## Context
- Status: in-progress.
- Owner/layer: `ecs` for `Extrinsic.ECS.Component.ProceduralGeometryRef`; `runtime` for `Extrinsic.Runtime.ProceduralGeometry` and `Extrinsic.Runtime.ProceduralGeometryPacker`.
- This is the earliest unblocked implementation child on Theme A, the shortest path to sandbox visible geometry.
- Planning source: `tasks/done/GRAPHICS-030-runtime-geometry-residency-bridge.md`, especially Decisions 1, 2, 3, 4, 6, 7, 10, 11, 12, and 14. This is the implementation child previously identified there as `GRAPHICS-030-Impl-A`.
- `src/runtime/README.md` already lists the planned modules and `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` location.
- Current source search confirms these modules do not exist yet.
- This task intentionally makes no engine frame behavior visible by itself; it unlocks `GRAPHICS-029-Impl-B` and `GRAPHICS-030-Impl-B`.

## Required changes
- [ ] Add `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` exporting `Extrinsic.ECS.Component.ProceduralGeometryRef` with CPU-only POD authoring data for procedural geometry.
- [ ] Add `src/runtime/Runtime.ProceduralGeometry.cppm` exporting the descriptor/key/cache surface from `GRAPHICS-030` Decisions 1–4.
- [ ] Add `src/runtime/Runtime.ProceduralGeometryPacker.cppm` exporting the triangle-only packer and reusable scratch buffer shape from Decision 6.
- [ ] Wire the new ECS and runtime modules into the relevant `CMakeLists.txt` files using the existing C++23 module patterns.
- [ ] Keep `ecs -> core` only for the new ECS component; runtime may import ECS procedural value types and graphics `GpuWorld` value types per the recorded layering audit.
- [ ] Add diagnostics/counter fields needed for cache-only tests where they belong in the runtime procedural module; do not append `RuntimeRenderExtractionStats` fields until `GRAPHICS-030-Impl-B` wires extraction.

## Tests
- [ ] Add `contract;runtime` tests for descriptor identity: identical `(Kind, Params)` produce identical keys/hashes; differing params produce distinct keys.
- [ ] Add `contract;runtime` tests for `ProceduralGeometryCache` direct use: first `EnsureResident` uploads once, second identical `EnsureResident` hits reuse and increments refcount, `Release` decrements refcount.
- [ ] Add `contract;runtime` tests for triangle packer shape: valid triangle params emit a `GeometryUploadDesc` with 3 vertices, 3 surface indices, deterministic debug name, and the vertex layout required by `GRAPHICS-031`.
- [ ] Add failure tests for invalid triangle params and missing/unsupported kind behavior if the implementation surface exposes those paths in this slice.
- [ ] Do not add `gpu` or `vulkan` tests in this task.

## Docs
- [ ] Update `src/runtime/README.md` planned-module rows to current-state public module rows where modules land.
- [ ] Update `src/ecs/README.md` and `src/ecs/Components/README.md` with the new procedural component.
- [ ] Update `src/graphics/renderer/README.md` only if the packer contract references renderer-facing upload layout in a way not already documented.
- [ ] Refresh `docs/api/generated/module_inventory.md` after adding module surfaces.
- [ ] Update `tasks/backlog/rendering/README.md` to note that `GRAPHICS-030-Impl-A` has started/landed when this task is retired.

## Acceptance criteria
- [ ] The new ECS procedural component is CPU-only and passes layering checks.
- [ ] The runtime procedural descriptor/key/cache/packer modules compile through the `ci` preset.
- [ ] Cache direct-use tests prove upload/reuse/refcount behavior without running `Runtime.RenderExtraction`.
- [ ] Triangle packer output is deterministic and compatible with the planned default debug surface vertex layout.
- [ ] No frame extraction behavior changes occur in this slice.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeTests IntrinsicEcsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Runtime.*`, `Extrinsic.Asset.*`, or live asset/renderer services from the new ECS component.
- Querying live ECS from `src/graphics/*`.
- Adding `Runtime.RenderExtraction` procedural-source detection or `GpuWorld::SetInstanceGeometry()` calls in this slice.
- Adding shaders, material registry changes, frame recipes, pass bodies, or Vulkan operational-state changes.

## Next verification step
- Implement the three module surfaces and focused contract tests, then run the verification commands above.


