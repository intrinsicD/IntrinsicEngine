# Module Inventory (auto-generated)

_Generated on 2026-04-28 by `tools/repo/generate_module_inventory.py`._

Root scanned: `src_new`

## Layer Summary

| Layer | Module Count |
|---|---:|
| `app` | 1 |
| `ecs` | 18 |
| `graphics` | 67 |
| `platform` | 2 |
| `runtime` | 3 |

## Modules

| Module | File | Layer |
|---|---|---|
| `Extrinsic.Sandbox` | `src_new/App/Sandbox/Sandbox.cppm` | `app` |
| `Extrinsic.ECS.Components.AssetInstance` | `src_new/ECS/Components/ECS.Component.AssetInstance.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Collider` | `src_new/ECS/Components/ECS.Component.Collider.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Culling.Local` | `src_new/ECS/Components/ECS.Component.Culling.Local.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Culling.Proxy` | `src_new/ECS/Components/ECS.Component.Culling.Proxy.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Culling.World` | `src_new/ECS/Components/ECS.Component.Culling.World.cppm` | `ecs` |
| `Extrinsic.ECS.Component.DirtyTags` | `src_new/ECS/Components/ECS.Component.DirtyTags.cppm` | `ecs` |
| `Extrinsic.ECS.Components.GeometrySources` | `src_new/ECS/Components/ECS.Component.GeometrySources.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Hierarchy` | `src_new/ECS/Components/ECS.Component.Hierarchy.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Light` | `src_new/ECS/Components/ECS.Component.Light.cppm` | `ecs` |
| `Extrinsic.ECS.Component.MetaData` | `src_new/ECS/Components/ECS.Component.MetaData.cppm` | `ecs` |
| `Extrinsic.ECS.Components.Selection` | `src_new/ECS/Components/ECS.Component.Selection.cppm` | `ecs` |
| `Extrinsic.ECS.Component.ShadowCaster` | `src_new/ECS/Components/ECS.Component.ShadowCaster.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Transform` | `src_new/ECS/Components/ECS.Component.Transform.Local.cppm` | `ecs` |
| `Extrinsic.ECS.Component.Transform.WorldMatrix` | `src_new/ECS/Components/ECS.Component.Transform.World.cppm` | `ecs` |
| `Extrinsic.ECS.Scene.Handle` | `src_new/ECS/ECS.Scene.Handle.cppm` | `ecs` |
| `Extrinsic.ECS.Scene.Registry` | `src_new/ECS/ECS.Scene.Registry.cppm` | `ecs` |
| `Extrinsic.ECS.System.RenderSync` | `src_new/ECS/Systems/ECS.System.RenderSync.cppm` | `ecs` |
| `Extrinsic.ECS.System.TransformHierarchy` | `src_new/ECS/Systems/ECS.System.TransformHierarchy.cppm` | `ecs` |
| `Extrinsic.Backends.Null` | `src_new/Graphics/Backends/Null/Backends.Null.cppm` | `graphics` |
| `Extrinsic.Backends.Vulkan:Internal` | `src_new/Graphics/Backends/Vulkan/Backends.Vulkan.Internal.cppm` | `graphics` |
| `Extrinsic.Backends.Vulkan` | `src_new/Graphics/Backends/Vulkan/Backends.Vulkan.cppm` | `graphics` |
| `Extrinsic.Graphics.Component.GpuSceneSlot` | `src_new/Graphics/Components/Graphics.Component.GpuSceneSlot.cppm` | `graphics` |
| `Extrinsic.Graphics.Component.Material` | `src_new/Graphics/Components/Graphics.Component.Material.cppm` | `graphics` |
| `Extrinsic.Graphics.Component.RenderGeometry` | `src_new/Graphics/Components/Graphics.Component.RenderGeometry.cppm` | `graphics` |
| `Extrinsic.Graphics.Component.VisualizationConfig` | `src_new/Graphics/Components/Graphics.Component.VisualizationConfig.cppm` | `graphics` |
| `Extrinsic.Graphics.Colormap` | `src_new/Graphics/Graphics.Colormap.cppm` | `graphics` |
| `Extrinsic.Graphics.ColormapSystem` | `src_new/Graphics/Graphics.ColormapSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.CullingSystem` | `src_new/Graphics/Graphics.CullingSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.DeferredSystem` | `src_new/Graphics/Graphics.DeferredSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.ForwardSystem` | `src_new/Graphics/Graphics.ForwardSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.GpuScene` | `src_new/Graphics/Graphics.GpuScene.cppm` | `graphics` |
| `Extrinsic.Graphics.GpuWorld` | `src_new/Graphics/Graphics.GpuWorld.cppm` | `graphics` |
| `Extrinsic.Graphics.LightSystem` | `src_new/Graphics/Graphics.LightSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.Material` | `src_new/Graphics/Graphics.Material.cppm` | `graphics` |
| `Extrinsic.Graphics.MaterialSystem` | `src_new/Graphics/Graphics.MaterialSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.PostProcessSystem` | `src_new/Graphics/Graphics.PostProcessSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderFrameInput` | `src_new/Graphics/Graphics.RenderFrameInput.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:Barriers` | `src_new/Graphics/Graphics.RenderGraph.Barriers.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:Compiler` | `src_new/Graphics/Graphics.RenderGraph.Compiler.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:Executor` | `src_new/Graphics/Graphics.RenderGraph.Executor.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:Pass` | `src_new/Graphics/Graphics.RenderGraph.Pass.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:Resources` | `src_new/Graphics/Graphics.RenderGraph.Resources.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph:TransientAllocator` | `src_new/Graphics/Graphics.RenderGraph.TransientAllocator.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderGraph` | `src_new/Graphics/Graphics.RenderGraph.cppm` | `graphics` |
| `Extrinsic.Graphics.RenderWorld` | `src_new/Graphics/Graphics.RenderWorld.cppm` | `graphics` |
| `Extrinsic.Graphics.Renderer` | `src_new/Graphics/Graphics.Renderer.cppm` | `graphics` |
| `Extrinsic.Graphics.SelectionSystem` | `src_new/Graphics/Graphics.SelectionSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.ShadowSystem` | `src_new/Graphics/Graphics.ShadowSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.TransformSyncSystem` | `src_new/Graphics/Graphics.TransformSyncSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.VisualizationSyncSystem` | `src_new/Graphics/Graphics.VisualizationSyncSystem.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Culling` | `src_new/Graphics/Passes/Pass.Culling.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Deferred.GBuffers` | `src_new/Graphics/Passes/Pass.Deferred.GBuffers.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Deferred.Lighting` | `src_new/Graphics/Passes/Pass.Deferred.Lighting.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.DepthPrepass` | `src_new/Graphics/Passes/Pass.DepthPrepass.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Forward.Line` | `src_new/Graphics/Passes/Pass.Forward.Line.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Forward.Point` | `src_new/Graphics/Passes/Pass.Forward.Point.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Forward.Surface` | `src_new/Graphics/Passes/Pass.Forward.Surface.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.ImGui` | `src_new/Graphics/Passes/Pass.ImGui.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.PostProcess.Bloom` | `src_new/Graphics/Passes/Pass.PostProcess.Bloom.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.PostProcess.FXAA` | `src_new/Graphics/Passes/Pass.PostProcess.FXAA.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.PostProcess.Histogram` | `src_new/Graphics/Passes/Pass.PostProcess.Histogram.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.PostProcess.SMAA` | `src_new/Graphics/Passes/Pass.PostProcess.SMAA.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.PostProcess.ToneMap` | `src_new/Graphics/Passes/Pass.PostProcess.ToneMap.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Present` | `src_new/Graphics/Passes/Pass.Present.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Selection.EdgeId` | `src_new/Graphics/Passes/Pass.Selection.EdgeId.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Selection.EntityId` | `src_new/Graphics/Passes/Pass.Selection.EntityId.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Selection.FaceId` | `src_new/Graphics/Passes/Pass.Selection.FaceId.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Selection.Outline` | `src_new/Graphics/Passes/Pass.Selection.Outline.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Selection.PointId` | `src_new/Graphics/Passes/Pass.Selection.PointId.cppm` | `graphics` |
| `Extrinsic.Graphics.Pass.Shadows` | `src_new/Graphics/Passes/Pass.Shadows.cppm` | `graphics` |
| `Extrinsic.RHI.Bindless` | `src_new/Graphics/RHI/RHI.Bindless.cppm` | `graphics` |
| `Extrinsic.RHI.BufferManager` | `src_new/Graphics/RHI/RHI.BufferManager.cppm` | `graphics` |
| `Extrinsic.RHI.BufferView` | `src_new/Graphics/RHI/RHI.BufferView.cppm` | `graphics` |
| `Extrinsic.RHI.CommandContext` | `src_new/Graphics/RHI/RHI.CommandContext.cppm` | `graphics` |
| `Extrinsic.RHI.Descriptors` | `src_new/Graphics/RHI/RHI.Descriptors.cppm` | `graphics` |
| `Extrinsic.RHI.Device` | `src_new/Graphics/RHI/RHI.Device.cppm` | `graphics` |
| `Extrinsic.RHI.FrameHandle` | `src_new/Graphics/RHI/RHI.FrameHandle.cppm` | `graphics` |
| `Extrinsic.RHI.Handles` | `src_new/Graphics/RHI/RHI.Handles.cppm` | `graphics` |
| `Extrinsic.RHI.PipelineManager` | `src_new/Graphics/RHI/RHI.PipelineManager.cppm` | `graphics` |
| `Extrinsic.RHI.Profiler` | `src_new/Graphics/RHI/RHI.Profiler.cppm` | `graphics` |
| `Extrinsic.RHI.SamplerManager` | `src_new/Graphics/RHI/RHI.SamplerManager.cppm` | `graphics` |
| `Extrinsic.RHI.TextureManager` | `src_new/Graphics/RHI/RHI.TextureManager.cppm` | `graphics` |
| `Extrinsic.RHI.Transfer` | `src_new/Graphics/RHI/RHI.Transfer.cppm` | `graphics` |
| `Extrinsic.RHI.TransferQueue` | `src_new/Graphics/RHI/RHI.TransferQueue.cppm` | `graphics` |
| `Extrinsic.RHI.Types` | `src_new/Graphics/RHI/RHI.Types.cppm` | `graphics` |
| `Extrinsic.Platform.Window` | `src_new/Platform/Platform.IWindow.cppm` | `platform` |
| `Extrinsic.Platform.Input` | `src_new/Platform/Platform.Input.cppm` | `platform` |
| `Extrinsic.Runtime.Engine` | `src_new/Runtime/Runtime.Engine.cppm` | `runtime` |
| `Extrinsic.Runtime.FrameClock` | `src_new/Runtime/Runtime.FrameClock.cppm` | `runtime` |
| `Extrinsic.Runtime.StreamingExecutor` | `src_new/Runtime/Runtime.StreamingExecutor.cppm` | `runtime` |

Total modules: **91**
