# GRAPHICS-018R — Renderer/Vulkan operational transition reset

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
- Reinitialize culling GPU buffers/pipeline and depth-prepass pipeline state through existing RHI managers.
- Reallocate/synchronize the material GPU buffer and refresh `GpuWorld` material-buffer binding.
- Reset or reconcile bindless slot accounting for material/texture fallback paths.
- Wire the hook from runtime composition, not from lower graphics layers.
- Preserve CPU/null behavior when devices are operational from initial renderer construction.

## Tests

- Add CPU/mock contract coverage for a renderer initialized with a non-operational device and then reset against an operational device.
- Assert culling/depth-prepass command stats move from skipped-unavailable/non-operational to recorded after reset.
- Add material-system diagnostics/capacity assertions showing CPU mirror state is uploaded/rebound after reset.

## Docs

- Update `docs/architecture/graphics.md` with the operational-transition ownership and call order.
- Update `src/graphics/vulkan/README.md` when Vulkan can actually transition to operational state.
- Update `tasks/active/GRAPHICS-018-vulkan-renderer-integration.md` status when the blocker is resolved.

## Acceptance criteria

- `GRAPHICS-018` can mark Vulkan operational only after this reset path exists and is tested.
- Renderer code still depends only on RHI/backend-neutral seams; no Vulkan special cases are introduced.
- CPU/null default gate remains Vulkan-free.
- Temporary fail-closed material/culling/bindless shims have a concrete removal/reconciliation path.

## Verification

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
