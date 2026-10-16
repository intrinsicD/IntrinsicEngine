# Graphics Renderer

This directory owns render orchestration, graphics scene state, frame recipes,
and concrete passes. It consumes immutable snapshots supplied by Runtime;
Runtime owns live ECS, asset-service access, extraction and presentation.
[Graphics architecture](../../../docs/architecture/graphics.md) defines those
boundaries. [RHI](../rhi/), [Vulkan](../vulkan/) and
[framegraph](../framegraph/) are sibling layers.

## Entry points

| Owner | Purpose |
| --- | --- |
| [Renderer](Graphics.Renderer.cppm) | Frame lifecycle, pipeline queries and renderer services. |
| [RenderWorld](Graphics.RenderWorld.cppm) / [RenderFrameInput](Graphics.RenderFrameInput.cppm) | Immutable scene and per-frame inputs. |
| [FrameRecipe](Graphics.FrameRecipe.cppm) | Typed live pass/resource composition and introspection. |
| [RenderingContract](Graphics.RenderingContract.cppm) / [RenderRecipeConfig](Graphics.RenderRecipeConfig.cppm) | Renderer-neutral contracts and validated recipe overlays. |
| [RenderDiagnostics](Graphics.RenderDiagnostics.cppm) | Compile, execution, command and lifecycle diagnostics. |
| [Passes](Passes/) / [Null backend](Backends/Null/) | Concrete pass implementations and the inert renderer backend. |

The generated [module inventory](../../../docs/api/generated/module_inventory.md)
is the full module list.

## Frame and resource ownership

Runtime drives `BeginFrame`, extraction, `PrepareFrame`, `ExecuteFrame` and
`EndFrame`, then presents through RHI. The renderer projects validated config
onto `FrameRecipeFeatures`, compiles or reuses the graph, binds current resources,
and records commands. [Frame graph architecture](../../../docs/architecture/frame-graph.md)
owns recipe rules, contract validation, barrier ordering, transient placement,
parallel recording and GPU timestamp behavior.

`GpuWorld` owns packed GPU scene residency. Runtime supplies copied upload plans
and keeps entity/asset sidecars outside canonical ECS components. See
[property coherence](../../../docs/architecture/property-coherence.md) for
revision, upload and publication rules, and
[graphics readback](../../../docs/architecture/graphics.md#gpu-to-cpu-readback-ring)
for transfer lifetime and backbuffer capture.

### Shader push-constant compatibility policy

Every pass must pair its pushed C++ struct with a shader push block that matches
byte for byte and with compatible descriptor expectations. Retained scene passes
use the `RHI::GpuScenePushConstants` layout (`SceneTableBDA`, `FrameIndex`,
`DrawBucket`, `DebugMode`, padding) or their explicitly defined pass-specific
layout. Deferred lighting pushes its own 16-byte block containing
`SceneTableBDA`, `ShadowAtlasBindlessIndex` and padding. Postprocess stages each
push their own static-asserted packet, not the CPU-side `PostProcessPushConstants`
value. Camera data comes from `GpuSceneTable`.

The legacy root shaders `surface.vert`, `surface.frag`, `surface_gbuffer.frag`,
`shadow_depth.vert` and `deferred_lighting.frag` expect obsolete push blocks and
descriptors. New promoted pipeline builders must not select them; use matching
GpuScene-aware shaders or a `default_debug_*` variant instead. The
[pass contracts](../../../docs/architecture/rendering-three-pass.md) own each
pass's bindings, including the shadow-atlas bindless index.

Clip space is Y-up with a negative-height Vulkan viewport and counter-clockwise
front faces for culled triangle lists. Fullscreen shaders use
`vUV.y = 0.5 * (1 - ndc.y)` so image orientation stays the same through any number
of fullscreen passes. Runtime projections must not flip Y again.

A CPU/null `Recorded` status does not establish native ABI correctness. Contract
tests must assert the intended SPV path or be paired with a corresponding
`gpu;vulkan` smoke that runs the pass.

## Further contracts

- [Pass families and render targets](../../../docs/architecture/rendering-three-pass.md)
  cover surfaces, lines, points, lighting, postprocess, selection, debug and UI.
- [Picking and primitive selection](../../../docs/architecture/primitive-selection.md)
  cover GPU hit identity and Runtime selection ownership.
- [Renderer target architecture](../../../docs/architecture/rendering-target-architecture.md)
  covers service boundaries and the typed pipeline-query API.
- [Shared compute primitives](../../../docs/architecture/compute-parallel-primitives.md)
  and [spatial indices](../../../docs/architecture/spatial-indices.md) cover reusable
  compute and point-tree contracts.
- [Graphics assets](../assets/) own asset-id-based GPU cache residency; the
  [architecture index](../../../docs/architecture/index.md) links the related ADRs.
