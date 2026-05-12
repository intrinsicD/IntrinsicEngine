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
- Status: done.
- Owner/layer: `ecs` for `Extrinsic.ECS.Component.ProceduralGeometryRef`; `runtime` for `Extrinsic.Runtime.ProceduralGeometry` and `Extrinsic.Runtime.ProceduralGeometryPacker`.
- This was the earliest unblocked implementation child on Theme A, the shortest path to sandbox visible geometry.
- Planning source: `GRAPHICS-030-runtime-geometry-residency-bridge.md` in this directory, especially Decisions 1, 2, 3, 4, 6, 7, 10, 11, 12, and 14. This is the implementation child previously identified there as `GRAPHICS-030-Impl-A`.
- This task intentionally makes no engine frame behavior visible by itself; it unlocks `GRAPHICS-029-Impl-B` and `GRAPHICS-030-Impl-B`.

## Required changes
- [x] Add `src/ecs/Components/ECS.Component.ProceduralGeometryRef.cppm` exporting `Extrinsic.ECS.Component.ProceduralGeometryRef` with CPU-only POD authoring data for procedural geometry.
- [x] Add `src/runtime/Runtime.ProceduralGeometry.cppm` (interface) and `Runtime.ProceduralGeometry.cpp` (implementation) exporting the descriptor/key/cache surface from `GRAPHICS-030` Decisions 1–4.
- [x] Add `src/runtime/Runtime.ProceduralGeometryPacker.cppm` (interface) and `Runtime.ProceduralGeometryPacker.cpp` (implementation) exporting the triangle-only packer and reusable scratch buffer shape from Decision 6.
- [x] Wire the new ECS and runtime modules into the relevant `CMakeLists.txt` files using the existing C++23 module patterns.
- [x] Keep `ecs -> core` only for the new ECS component; runtime imports the ECS procedural value types and graphics `GpuWorld` value types per the recorded layering audit.
- [x] Add diagnostics/counter fields needed for cache-only tests where they belong in the runtime procedural module (`ProceduralGeometryCacheStats`); no `RuntimeRenderExtractionStats` fields are appended in this slice.

## Tests
- [x] Added `contract;runtime` tests for descriptor identity in `tests/contract/runtime/Test.ProceduralGeometryCache.cpp`: identical `(Kind, Params)` produce identical keys/hashes; differing params produce distinct keys; keys stable across invocations.
- [x] Added `contract;runtime` tests for `ProceduralGeometryCache` direct use: first `EnsureResident` uploads once, second identical `EnsureResident` hits reuse and increments refcount, `Release` decrements refcount.
- [x] Added `contract;runtime` tests for triangle packer shape: valid triangle params emit a `GeometryUploadDesc` with 3 vertices, 3 surface indices, deterministic debug name `"Procedural.Triangle"`, and the 20-byte `{pos.xyz, uv}` vertex layout consumed by `Test.MinimalTriangleAcceptance`.
- [x] Added failure tests for invalid triangle params (rejected) and failed upload (counter increment, no entry).
- [x] No `gpu` or `vulkan` tests added in this task.

## Docs
- [x] Updated `src/runtime/README.md` to move the procedural descriptor/packer rows from "Planned modules" into the current-state public module table.
- [x] Updated `src/ecs/README.md` and `src/ecs/Components/README.md` with the new procedural component listing and CPU-only boundary note.
- [x] `src/graphics/renderer/README.md` not touched; the packer references the existing `GpuWorld::GeometryUploadDesc` surface without new renderer-facing layout requirements.
- [x] Refreshed `docs/api/generated/module_inventory.md` after adding the three new module surfaces (now 424 modules).
- [x] Updated `tasks/backlog/rendering/README.md` to retire the `GRAPHICS-030A` entry against this `tasks/done/` file.

## Acceptance criteria
- [x] The new ECS procedural component is CPU-only and passes layering checks (`check_layering.py --strict`).
- [x] The runtime procedural descriptor/key/cache/packer modules compile through the `ci` preset (built `IntrinsicRuntimeContractTests`).
- [x] Cache direct-use tests prove upload/reuse/refcount behavior without running `Runtime.RenderExtraction` (12 new tests, all passing).
- [x] Triangle packer output is deterministic and compatible with the existing minimal-triangle vertex layout (`Test.MinimalTriangleAcceptance` matches the 20-byte `{pos.xyz, uv}` packing).
- [x] No frame extraction behavior changes occur in this slice; `Runtime.RenderExtraction` is unchanged.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicEcsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

Note: the verification target list is `IntrinsicRuntimeContractTests` (the new
CPU-only contract executable) rather than `IntrinsicRuntimeTests`, which is
labeled `gpu vulkan slow` and excluded by the default CPU gate. The new
executable is gated on `if(TARGET ExtrinsicRuntime)` and builds in both headless
and full configurations.

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Importing `Extrinsic.Graphics.*`, `Extrinsic.RHI.*`, `Extrinsic.Runtime.*`, `Extrinsic.Asset.*`, or live asset/renderer services from the new ECS component.
- Querying live ECS from `src/graphics/*`.
- Adding `Runtime.RenderExtraction` procedural-source detection or `GpuWorld::SetInstanceGeometry()` calls in this slice.
- Adding shaders, material registry changes, frame recipes, pass bodies, or Vulkan operational-state changes.

## Completion
- Completed: 2026-05-12.
- Commit reference: pending — recorded on branch `claude/setup-agentic-workflow-fWjvR`; commit hash and PR link will be appended on push.
- Verification commands above all executed in-session: configure + build succeeded under the `ci` preset (clang-20, headless-no-glfw auto-enabled), `IntrinsicRuntimeContractTests` + `IntrinsicEcsContractTests` pass with 17/17 tests including 12 new procedural-geometry contract tests, layering / test-layout / doc-link / task-policy structural checks report zero findings, and the module inventory regenerates cleanly (ecs 21→22, runtime 4→6).
- Notes:
  - `ProceduralGeometryKind` and `ProceduralGeometryParams` live in `Extrinsic.ECS.Component.ProceduralGeometryRef` (single canonical definition) and are re-used by `Extrinsic.Runtime.ProceduralGeometry` via `using` aliases to satisfy the Decision 14 layering constraint (`ecs -> core` only) while keeping the runtime descriptor surface a single import.
  - The cache decouples upload via an injected `UploadFn` (`std::function<GpuGeometryHandle(const GeometryUploadDesc&)>`) so contract tests exercise refcount/reuse semantics without standing up a live `GpuWorld` or `IRenderer`. `GRAPHICS-030-Impl-B` supplies the upload functor from `RenderExtractionCache::ExtractAndSubmit()` calling into `GpuWorld::UploadGeometry`.
  - Default-constructed `ProceduralGeometryParams{}` (VertexCount==0, IndexCount==0) requests the canonical reference triangle so `GRAPHICS-029-Impl-B` can attach an empty `ProceduralGeometryRef{Triangle, {}}` and still receive the minimal visible surface; authored overrides must match `(VertexCount==3, IndexCount==3)`. Other shapes are rejected (`std::nullopt`), to be removed when `Cube`/`Quad`/etc. packers land per Decision 13.
  - Deferred-retire / `Tick(currentFrame, framesInFlight)` is intentionally not implemented here; `Release` simply decrements the refcount, leaves the entry resident, and exposes `RefCount==0` for the Impl-C retire-queue test.
