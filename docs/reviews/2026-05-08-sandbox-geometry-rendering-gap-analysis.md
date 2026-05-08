# Sandbox Geometry Rendering Gap Analysis (2026-05-08)

## Scope

This review records what is currently missing before `Sandbox::App` can run as the generic reference application and render actual geometry through the promoted runtime/graphics path.

The analysis is intentionally scoped to the promoted source tree and current runtime composition path. It does not propose moving application-specific behavior into `Sandbox::App`; per the app/runtime boundary, sandbox should remain policy-light and runtime should own feature wiring and reference configuration.

## Executive summary

`ExtrinsicSandbox` can currently build, but that is not equivalent to rendering geometry. The promoted path is still missing four distinct layers of work:

1. **Reference scene/content creation**: no runtime-owned bootstrap currently creates a camera, lights, transform hierarchy, or renderable entity for sandbox.
2. **Runtime geometry residency bridge**: `Runtime.RenderExtraction` allocates render instances and submits transform/light/visualization snapshots, but it does not upload mesh data or bind a geometry handle to the instance.
3. **Operational renderer pass bodies**: `Graphics::CreateRenderer()` still returns the null renderer, and most visible pass command bodies are soft-skipped even when a device exists.
4. **Operational Vulkan backend**: runtime defaults to a null device unless promoted Vulkan is explicitly enabled, and the promoted Vulkan backend is documented as fail-closed/non-operational for command recording.

The shortest path to a visible triangle is therefore not just “add geometry in `Sandbox::App`”. It requires a runtime-owned sample scene/bootstrap seam, a geometry upload/binding bridge, renderer pass command bodies, and an operational device path.

## Current known state

### Sandbox app is intentionally empty

`src/app/Sandbox/Sandbox.cppm` defines lifecycle hooks that currently do not mutate engine behavior. That is aligned with the app boundary: generic sandbox should exercise runtime composition, not become a special gameplay/demo layer.

`src/app/Sandbox/main.cpp` now obtains configuration through `Extrinsic::Runtime::CreateReferenceEngineConfig()` and imports runtime plus sandbox only. This is the correct direction for keeping app dependencies at `app -> runtime`.

### The executable builds

The last verified focused build was:

```bash
cmake --build --preset ci --target ExtrinsicSandbox
```

Result recorded during this session: the target linked successfully. This proves the target is buildable after the sandbox/runtime boundary refactor and stale test-source fix, but it does not prove that a GPU frame renders visible geometry.

### Runtime defaults do not create an operational GPU path

`src/runtime/Runtime.Engine.cpp` still has a null fallback path in device creation. The current code path returns `Backends::Null::CreateNullDevice()` when the promoted Vulkan path is not both compiled and enabled by configuration.

`src/runtime/Runtime.Engine.cppm` sets the reference render backend to Vulkan, but Vulkan execution is still separately gated by promoted-device configuration and build options. A Vulkan backend enum value alone is insufficient to make the runtime use an operational Vulkan device.

`src/graphics/vulkan/README.md` documents that the promoted Vulkan backend keeps `IsOperational() == false` until canonical renderer pass command recording, synchronization/barrier validation, queue-family ownership, and service fallback reconciliation are completed.

### The renderer factory still returns the null renderer

`src/graphics/renderer/Graphics.Renderer.cpp` currently implements `Graphics::CreateRenderer()` by returning `NullRenderer`.

The null renderer is useful for CPU-testable contracts: frame lifecycle, render graph construction, extraction snapshots, pass status accounting, and non-operational-device behavior. It is not currently a complete visible rendering backend.

### Runtime extraction observes renderable candidates but does not bind geometry

`src/runtime/Runtime.RenderExtraction.cppm` currently identifies renderable entities by requiring:

- `ECS::Components::Transform::WorldMatrix`; and
- at least one render hint from `Graphics::Components::RenderSurface`, `RenderLines`, or `RenderPoints`.

For candidates, extraction can allocate a render sidecar and submit transform records. It also observes `ECS::Components::AssetInstance::Source` through the GPU asset cache path.

However, there is no call to `GpuWorld::SetInstanceGeometry()` in `Runtime.RenderExtraction.cppm`, and no runtime bridge currently uploads decoded mesh data into `GpuWorld` for renderable ECS entities.

### Low-level geometry upload exists but is not integrated into runtime extraction

`src/graphics/renderer/Graphics.GpuWorld.cppm` exposes the low-level primitives needed by a bridge:

- `GpuWorld::UploadGeometry(const GeometryUploadDesc&)`
- `GpuWorld::FreeGeometry(GpuGeometryHandle)`
- `GpuWorld::SetInstanceGeometry(GpuInstanceHandle, GpuGeometryHandle)`

`tests/contract/graphics/Test.MinimalTriangleAcceptance.cpp` demonstrates the current contract-test path: manually upload triangle geometry, allocate/bind instance state, and set the instance geometry handle. That proves the CPU-side renderer world model has the concept needed for a minimal triangle, but the path is not wired from sandbox or runtime ECS extraction.

### Asset loading is not yet a renderable geometry residency bridge

`src/assets/Asset.Service.*` and `src/graphics/assets/Graphics.GpuAssetCache.cppm` provide pieces of an asset and GPU asset cache system, but the current promoted runtime does not yet provide a full ECS asset-source to renderable geometry residency bridge.

The architecture documentation in `docs/architecture/graphics.md` already notes that the full ECS renderable residency bridge is follow-up work: immutable asset geometry should flow from `AssetInstance::Source` to normalized `Assets::AssetId`, through GPU asset cache/world upload, and then into instance geometry binding.

## Exact missing pieces

### 1. Runtime-owned reference scene bootstrap

Missing behavior:

- Create or load at least one renderable entity in the promoted ECS registry.
- Ensure the entity has a stable ID, `Transform::WorldMatrix`, and a render hint component.
- Create a camera/view configuration and at least one light or a material/debug path that does not require lighting.
- Keep this wiring in `runtime` or an explicit runtime sample/bootstrap seam, not in `Sandbox::App`.

Why it matters:

- Current sandbox lifecycle hooks create no scene content.
- Runtime extraction will not see candidates unless ECS entities already have the required transform and render hint components.

Recommended first contract:

- Add a runtime-owned opt-in reference scene bootstrap that creates one deterministic triangle/surface entity and one camera using promoted ECS components.
- Cover it with an integration test that verifies extraction sees one candidate renderable.

### 2. Renderable geometry authoring contract

Missing behavior:

- A promoted, layer-safe way to express “this entity should render this mesh/primitive/asset”.
- A decision on whether the first reference geometry comes from:
  - a built-in procedural triangle descriptor;
  - an `AssetInstance::Source` pointing at an asset file; or
  - a small runtime test fixture asset.

Why it matters:

- Render hints currently say an entity is renderable, but they do not by themselves provide vertex/index buffers or geometry residency.
- ECS components should remain CPU-side authoring/scene data and must not store live GPU handles.

Recommended first contract:

- For the minimal visible path, use a runtime-owned procedural triangle or simple mesh descriptor to avoid coupling the first rendering milestone to glTF/material loading.
- Keep GPU handles in runtime/graphics sidecars, not ECS components.

### 3. Runtime ECS-to-GpuWorld geometry residency bridge

Missing behavior:

- Detect a renderable entity’s geometry source.
- Upload geometry once, or reuse an existing resident geometry allocation.
- Store geometry residency in a runtime/graphics sidecar keyed by stable entity ID or asset ID.
- Call `GpuWorld::SetInstanceGeometry(instance, geometry)` when the entity’s render instance is allocated or its geometry changes.
- Free geometry when no longer referenced, or use explicit cache lifetime policy for shared assets.

Why it matters:

- The renderer world can represent geometry, but runtime extraction currently only submits transform/light/visualization records.
- Without instance-to-geometry binding, even a visible pass body would not know what mesh to draw for sandbox entities.

Recommended first contract:

- Extend runtime render extraction tests to prove that a minimal triangle entity produces:
  - one renderable sidecar;
  - one valid `GpuInstanceHandle`;
  - one valid `GpuGeometryHandle`; and
  - a bound instance/geometry relationship in `GpuWorld`.

### 4. Material and shader/default surface policy

Missing behavior:

- A deterministic default material or debug material for the first surface draw.
- A shader/pipeline selection path for the reference triangle.
- Clear fallback behavior when material assets are absent.

Why it matters:

- Geometry alone is not enough for a surface pass; the renderer needs pipeline state, material bindings, and shader inputs.
- The first visible milestone should avoid silent skips caused by missing material state.

Recommended first contract:

- Add a default untextured/debug surface material owned by graphics/runtime configuration.
- Make missing material fallback explicit and testable.

### 5. Operational renderer pass command bodies

Missing behavior:

- Command recording for at least the minimal visible path:
  - surface or debug triangle pass;
  - render target setup;
  - present/swapchain path;
  - required barriers/synchronization.

Why it matters:

- Current renderer lifecycle and pass accounting are CPU-testable, but visible pass bodies are not complete.
- Existing renderer lifecycle tests intentionally expect some passes, including surface/present, to be skipped under current conditions.

Recommended first contract:

- Add a narrowly scoped “minimal surface triangle” renderer acceptance test behind appropriate non-GPU/GPU labels depending on whether it uses mock command recording or real Vulkan.
- Keep the null renderer contract intact for non-operational-device tests.

### 6. Operational Vulkan device path

Missing behavior:

- Build/configuration route that enables promoted Vulkan intentionally.
- `IDevice::IsOperational()` becoming true only after required device, allocator, swapchain, command, synchronization, and validation contracts are satisfied.
- Runtime reconciliation between reference config, CMake options, and device fallback policy.

Why it matters:

- `GraphicsBackend::Vulkan` in config does not currently guarantee operational Vulkan execution.
- The promoted Vulkan README explicitly documents the backend as fail-closed/non-operational today.

Recommended first contract:

- Preserve fail-closed behavior until renderer command recording and synchronization contracts are complete.
- Add explicit diagnostics when runtime selects null fallback despite Vulkan being requested.

## Minimal milestone plan

The safest incremental path is:

1. **CPU/null contract milestone**
   - Add a runtime-owned reference scene bootstrap that creates one triangle-like renderable candidate.
   - Add a runtime extraction test proving the candidate is observed and assigned an instance sidecar.

2. **Geometry residency milestone**
   - Add procedural triangle upload in runtime or graphics test seam.
   - Bind the uploaded geometry with `GpuWorld::SetInstanceGeometry()`.
   - Test that extraction creates a valid instance/geometry binding without requiring real Vulkan.

3. **Renderer command milestone**
   - Implement the smallest surface/debug pass command body needed for a triangle.
   - Keep skipped-pass statuses for unimplemented passes explicit.

4. **Vulkan operational milestone**
   - Turn on promoted Vulkan only after command recording, synchronization, swapchain, and diagnostics satisfy contract tests.
   - Add a GPU-labeled smoke test for one visible triangle when the host supports Vulkan.

5. **Asset-backed geometry milestone**
   - Bridge `AssetInstance::Source` through asset loading/GPU cache into `GpuWorld` geometry residency.
   - Add tests for asset ID normalization, cache reuse, invalidation, and cleanup.

## Suggested backlog follow-ups

These should be tracked as separate tasks rather than implemented as one large rendering patch:

- `GRAPHICS/RUNTIME`: reference scene bootstrap and minimal renderable extraction contract.
- `GRAPHICS/RUNTIME`: ECS renderable geometry residency bridge with procedural triangle test seam.
- `GRAPHICS`: default material/debug surface policy for missing material assets.
- `GRAPHICS`: minimal surface/present command recording path.
- `GRAPHICS/VULKAN`: promoted Vulkan operational readiness and diagnostics.
- `ASSETS/GRAPHICS`: asset-backed mesh residency from `AssetInstance::Source` to `GpuWorld` geometry.

## Review conclusion

The current sandbox state is architecturally appropriate for a policy-light reference application, but the engine is not yet wired to render real geometry from it. The critical missing bridge is in runtime/graphics, not in `Sandbox::App`: runtime must create or load renderable content, upload/bind geometry into `GpuWorld`, and drive a renderer/device path with real command recording.

Until those pieces land, `ExtrinsicSandbox` should be treated as a buildable integration executable and lifecycle harness, not as evidence of visible geometry rendering.

## Validation notes

- Code was not changed for this review note.
- Existing focused build evidence from this session: `cmake --build --preset ci --target ExtrinsicSandbox` succeeded after prior build-blocker fixes.
- Documentation link validation should be run after adding this file.
