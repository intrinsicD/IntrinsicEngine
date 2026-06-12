---
id: BUG-032
theme: F
depends_on: [BUG-028, RUNTIME-106]
maturity_target: Operational
---
# BUG-032 — Triangle edge and point rendering is invisible on Vulkan

## Goal
- Make the promoted sandbox/reference triangle render visible edge and vertex lanes end-to-end on an operational Vulkan backend when `RenderEdges` and `RenderPoints` are enabled.

## Non-goals
- No rollback to legacy mesh-view ECS components.
- No graphics-layer live ECS reads or mesh-topology traversal.
- No broad renderer rewrite, new frame recipe, or unrelated graph/point-cloud behavior changes.
- No dependency/build-system changes beyond respecting the active `INFRA-001` vcpkg preset flow.

## Context
- Owner/layer: `runtime` owns ECS-to-render snapshot composition and mesh edge/point sidecar residency; `graphics/renderer`, `graphics/rhi`, and `graphics/vulkan` own retained line/point draw recording and backend execution; shaders under `assets/shaders/forward/` own the GPU expansion/visibility of those lanes.
- `BUG-028` proved the mesh primitive-view path under the CPU/null gate and updated point shaders, but explicitly retired at `CPUContracted`.
- `RUNTIME-106` made `RenderEdges` / `RenderPoints` component presence the authoritative user-facing control and also retired at `CPUContracted`.
- Reported symptom: the triangle surface renders, but its points/vertices and edges are not visible in the sandbox/reference triangle path.

## Required changes
- [x] Reproduce the missing edge/point lanes with a deterministic Vulkan-facing loop.
- [x] Inspect runtime extraction, `GpuWorld` lane population, retained forward line/point passes, RHI draw command state, Vulkan pipeline/primitive topology mapping, and the forward line/point shaders.
- [x] Fix the root cause while preserving runtime-owned lane composition and graphics-layer snapshot ownership.
- [x] Remove any temporary debug instrumentation used during diagnosis.

## Tests
- [x] Add or update focused regression coverage at the narrowest correct seam.
- [x] Run focused CPU/null tests covering runtime extraction and retained line/point pass contracts.
- [x] Run a Vulkan `gpu;vulkan` smoke/readback or command-stream proof that actually exercises triangle edge and point lanes.

## Docs
- [x] Update this task with root cause, fix summary, and verification.
- [x] Update renderer/runtime/Vulkan docs for the point-size and readback contract changes.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening or retiring the task.

## Diagnosis
- Repro loop: a new Vulkan-labeled runtime smoke enables `RenderEdges` and `RenderPoints` on the reference triangle, reads back the GpuScene/culling buffers, and samples the backbuffer near a triangle edge and vertex.
- CPU/runtime state was valid: extraction submitted mesh edge and vertex sidecars and populated line/point `GpuWorld` records.
- GPU-side failure was the `GpuGeometryRecord` ABI. C++ kept the record at 64 bytes (`alignas(16)`), while the GLSL scalar-layout record compiled to an array stride of 56 bytes. Surface geometry in slot 0 survived, but sidecar geometry slots 1 and 2 were read at the wrong stride, so culling saw zero line/point counts and emitted no draw commands.
- A second shader issue double-applied geometry vertex offsets: Vulkan indirect `firstVertex` / indexed `vertexOffset` already contribute to `gl_VertexIndex`, but the GpuScene shaders added `geo.VertexOffset` again.
- A visibility issue made points easy to miss even after draw emission: the default `RenderPoints::SizeSource` was still a world-radius-style `0.008f`, while the promoted point shader treats `GpuEntityConfig::PointSize` as a screen-space pixel size.

## Fix summary
- `GpuGeometryRecord` now carries explicit padding in C++ and GLSL so SPIR-V uses a 64-byte stride matching the RHI type.
- GpuScene forward/depth/deferred/selection vertex shaders now index managed vertex buffers directly from `gl_VertexIndex`, because culling supplies `firstVertex` / `vertexOffset` through the indirect commands.
- Runtime mesh edge/vertex sidecars now receive explicit uniform dark overlay config, and mesh vertex sidecars forward `RenderPoints` point size/type into `GpuEntityConfig`.
- The default point-size path is screen-space pixels (`6.0f`) and point shaders clamp to `[0.5, 32.0]`.
- Vulkan `ReadBuffer` now supports device-local buffers via a synchronous staging copy, keeping GPU smoke diagnostics able to read culling and GpuWorld state without changing production allocation policy.

## Acceptance criteria
- [x] A reference/sandbox triangle with `RenderEdges` produces a renderer-visible line lane and Vulkan records/draws it with non-zero indexed draw input.
- [x] A reference/sandbox triangle with `RenderPoints` produces a renderer-visible point lane and Vulkan records/draws it with non-zero non-indexed draw input.
- [x] The edge and point lanes are visually or readback-proven on an operational Vulkan device.
- [x] Existing CPU/null render-component coverage remains green.
- [x] No new layering violations are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'LinePoint|MeshPrimitiveView|RuntimeRenderExtraction|RuntimeSandboxAcceptance|PointCloudGeometryExtraction|SandboxEditorUi|RenderPassContract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests
ctest --test-dir build/ci-vulkan --output-on-failure -R 'RuntimeSandboxAcceptanceGpuSmoke\.ReferenceTriangleMeshEdgeAndPointLanesPresentBrightOverlay' -L 'gpu' -L 'vulkan' -LE 'slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' -R 'DefaultRecipeSurfaceGpuSmoke|RuntimeSandboxAcceptanceGpuSmoke' -LE 'slow|flaky-quarantine' --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

2026-06-12 results:
- Commit: pending local BUG loop closure commit.
- `cmake --build --preset ci --target IntrinsicTests` passed.
- Default CPU-supported CTest gate passed 2976/2976.
- Focused CPU/null ctest pattern passed 120/120.
- `cmake --build --preset ci-vulkan --target IntrinsicGraphicsVulkanSmokeTests IntrinsicRuntimeSandboxAcceptanceGpuSmokeTests` passed.
- Focused triangle edge/point Vulkan regression passed 1/1.
- Broader default-recipe/sandbox `gpu;vulkan` smoke passed 10/11 with one host-capability async-compute skip.
- SPIR-V disassembly reports `GpuGeometryRecord` `ArrayStride 64`.
- Task policy, doc links, layering, test layout, and `git diff --check` passed.

## Forbidden changes
- Mixing mechanical moves with semantic refactors.
- Introducing unrelated feature work.
- Reintroducing legacy mesh primitive-view components as authority.
- Letting `src/graphics/*` import live ECS state.

## Maturity
- Target: `Operational` on Vulkan-capable hosts; `CPUContracted` everywhere else.
