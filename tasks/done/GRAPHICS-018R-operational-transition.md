# GRAPHICS-018R — Renderer/Vulkan operational transition reset

## Status

- State: done.
- Owner/agent: local agent workflow.
- Commit / PR: pending local agent workflow handoff.
- Completed: 2026-05-04.
- Completion note: implemented `IRenderer::RebuildOperationalResources()` plus runtime-owned operational-transition detection; renderer rebuilds material GPU buffers, `GpuWorld` buffers/scene-table bindings, culling output resources, and the depth-prepass pipeline through RHI seams. Vulkan remains fail-closed/non-operational; swapchain/surface/presentation bring-up is still owned by `GRAPHICS-018`.

## Goal

Add the renderer/runtime reset path required when a promoted Vulkan device transitions from fail-closed non-operational state to operational GPU execution.

## Non-goals

- No Vulkan instance, surface, swapchain, or presentation bring-up beyond the reset seam.
- No renderer feature expansion or new passes.
- No mandatory GPU/Vulkan tests in the default CPU gate.

## Context

Owned by `runtime`, `graphics/renderer`, and `graphics/vulkan` integration. `GRAPHICS-026` documented that `CullingSystem` output availability, material GPU capacity, and bindless slot accounting are initialized while the current Vulkan backend is non-operational. Before `GRAPHICS-018` marks Vulkan operational, runtime must be able to notify the renderer to rebuild GPU-only state deterministically.

This is a blocking follow-up for the next `GRAPHICS-018` slice that can make `VulkanDevice::IsOperational()` true. Timeline: complete before any PR/slice that enables Vulkan device/swapchain operational execution.

## Required changes

- Add a documented renderer reset/rebind hook for device-becomes-operational transitions.
- Done 2026-05-04: reinitialize culling GPU buffers/pipeline and depth-prepass pipeline state through existing RHI managers.
- Done 2026-05-04: reallocate/synchronize the material GPU buffer and refresh `GpuWorld` material-buffer binding.
- Reconcile bindless slot accounting for material/texture fallback paths in the future `GRAPHICS-018` Vulkan operational bring-up slice that replaces fail-closed fallback services.
- Done 2026-05-04: wire the hook from runtime composition, not from lower graphics layers.
- Done 2026-05-04: preserve CPU/null behavior when devices are operational from initial renderer construction.

## Tests

- Done 2026-05-04: `RendererFrameLifecycle.OperationalRebuildAfterNonOperationalStartupRecordsRoutedCommands` covers a renderer initialized with a non-operational device and then reset against an operational mock device.
- Done 2026-05-04: the same test asserts culling/depth-prepass command stats move from skipped non-operational to recorded after reset.
- Done 2026-05-04: the same test asserts material/GpuWorld GPU handles and material capacity are rebound after reset.

## Docs

- Done 2026-05-04: updated `docs/architecture/graphics.md` with operational-transition ownership and call order.
- Done 2026-05-04: updated `src/graphics/vulkan/README.md` to note the reset seam is available while Vulkan itself remains non-operational.
- Done 2026-05-04: updated `tasks/active/GRAPHICS-018-vulkan-renderer-integration.md` status when the blocker was resolved.

## Acceptance criteria

- `GRAPHICS-018` can mark Vulkan operational only after this reset path exists and is tested.
- Renderer code still depends only on RHI/backend-neutral seams; no Vulkan special cases are introduced.
- CPU/null default gate remains Vulkan-free.
- Temporary fail-closed material/culling/bindless shims have a concrete removal/reconciliation path.

## Verification

Final evidence (2026-05-04):

- `cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests -j2`
- `ctest --test-dir build/ci --output-on-failure -R '^RendererFrameLifecycle\.' --timeout 60`
- `cmake --build --preset ci --target IntrinsicRuntimeGraphicsCpuTests -j2`
- `ctest --test-dir build/ci --output-on-failure -R '^RuntimeFrameLoopContract\.' --timeout 60`
- `cmake --build --preset ci --target IntrinsicTests -j2`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60 --quiet`

Planned full/touched-scope evidence before handoff:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
ctest --test-dir build/ci --output-on-failure -R '^RendererFrameLifecycle\.' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Do not bring up Vulkan swapchain/presentation in this task.
- Do not make Vulkan/GPU tests mandatory in the default CPU gate.
- Do not import Vulkan backend modules into renderer code.
