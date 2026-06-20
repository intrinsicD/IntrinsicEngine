---
id: BUG-049
theme: G
depends_on: []
---
# BUG-049 — GpuWorld geometry rebind lacks upload-to-read barriers

## Goal
- Ensure CPU-authored geometry replacements and instance geometry rebinds are visible to renderer consumers in the next recorded frame.

## Non-goals
- No change to mesh normal recomputation algorithms.
- No immediate GPU work from sandbox editor UI commands.
- No framegraph resource-model expansion for managed vertex/index buffers in this slice.
- No reintroduction of fragment normal-texture sampling for surface shading.

## Context
- Symptom: after recomputing mesh vertex normals from the sandbox editor, shading could appear to keep using the old GPU data until a large camera movement changed culling/history behavior.
- Runtime extraction already consumes `DirtyVertexAttributes`, uploads replacement `GpuWorld` geometry, and calls `SetInstanceGeometry` during the frame after the UI command.
- The gap was downstream: `GpuWorld::UploadGeometry` and `GpuWorld::SyncFrame` write device buffers through `IDevice::WriteBuffer`, while the renderer's next culling/depth/surface passes read the scene-table, instance, geometry-record, managed vertex, and managed index buffers without an explicit upload-to-read barrier owned by that direct-write path.
- Owner: `src/graphics/renderer`; runtime remains responsible only for dirty tagging and render extraction.

## Required changes
- [x] Track one-shot pending upload barriers for `GpuWorld` direct buffer writes.
- [x] Submit pending `TransferWrite -> ShaderRead` barriers for scene-table, instance, entity-config, geometry-record, bounds, lights, and managed vertex buffers before culling.
- [x] Submit a pending managed-index-buffer barrier before later indexed draws consume the replacement geometry.
- [x] Keep unchanged frames from resubmitting stale upload barriers.

## Tests
- [x] Add a `GpuWorld` regression for replacement geometry upload, instance rebind, frame sync, and one-shot barrier submission.
- [x] Run adjacent renderer lifecycle coverage that records the default culling/depth/surface route.
- [x] Run mesh extraction dirty-tag coverage for `DirtyVertexAttributes`.

## Docs
- [x] Record the bug and barrier ownership in this task file.
- [x] Regenerate module inventory for the `GpuWorld` public API addition.

## Acceptance criteria
- [x] A replacement geometry upload followed by `SetInstanceGeometry` produces upload-to-read barriers before the next consumers.
- [x] The pending barrier state is consumed once and does not emit repeatedly on unchanged frames.
- [x] The UI/editor normal recompute path remains dirty-tag/deferred-extraction based.
- [x] No new layer dependency violations are introduced.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests -- -j$(nproc)
ctest --test-dir build/ci --output-on-failure -R "GraphicsGpuWorld\\.GeometryRebindSubmitsUploadBarriersBeforeNextConsumers|RendererFrameLifecycle\\.UsesDeviceFrameLifecycleBackbufferAndCommandContext|MeshGeometryExtraction\\.DirtyTagTriggersReupload.*DirtyVertexAttributes" --timeout 60
ctest --test-dir build/ci --output-on-failure -R "DirtyVertexAttributes|MeshGeometryExtraction\\.MultipleDirtyTagsCoalesceIntoSingleReupload|MeshGeometryExtraction\\.RepeatedExtractionReusesMeshHandleWithoutReupload" --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Blocking the UI thread on renderer/GPU completion from the editor command.
- Mixing mechanical file moves with semantic fixes.
- Adding Vulkan-specific knowledge to runtime or geometry layers.

## Maturity
- Target: `CPUContracted`.
- No `Operational` follow-up is owed for this barrier seam: the fix is backend-neutral RHI command recording owned by `GpuWorld`/renderer, and Vulkan-specific command execution remains covered by the existing opt-in `gpu;vulkan` smoke gates.
