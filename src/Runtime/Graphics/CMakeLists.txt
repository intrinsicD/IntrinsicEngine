intrinsic_add_module_library(IntrinsicGraphics)
set(target_name IntrinsicGraphics)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.cppm
        Graphics.Camera.cppm
        Graphics.Geometry.cppm
        Graphics.Material.cppm
        Graphics.MaterialSystem.cppm
        Graphics.Model.cppm
        Graphics.RenderSystem.cppm
        Graphics.RenderGraph.cppm
        Graphics.Components.cppm
        Graphics.ModelLoader.cppm
        Graphics.TextureLoader.cppm
        Graphics.AssetErrors.cppm
        Graphics.ShaderRegistry.cppm
        Graphics.PipelineLibrary.cppm
        Graphics.RenderPipeline.cppm
        Graphics.GPUScene.cppm
        Graphics.Systems.GPUSceneSync.cppm
        Graphics.Systems.MeshRendererLifecycle.cppm
        # New Modules
        Graphics.RenderPath.cppm
        Graphics.GpuColor.cppm
        Graphics.DebugDraw.cppm
        Graphics.OctreeDebugDraw.cppm
        Graphics.BoundingDebugDraw.cppm
        Graphics.KDTreeDebugDraw.cppm
        Graphics.Interaction.cppm
        Graphics.Presentation.cppm
        Graphics.GlobalResources.cppm

        Pipelines/Graphics.Pipelines.cppm
        Passes/Graphics.Passes.Picking.cppm
        Passes/Graphics.Passes.Forward.cppm
        Passes/Graphics.Passes.SelectionOutlineSettings.cppm
        Passes/Graphics.Passes.SelectionOutline.cppm
        Passes/Graphics.Passes.Line.cppm
        Passes/Graphics.Passes.PointCloud.cppm
        Passes/Graphics.Passes.DebugView.cppm
        Passes/Graphics.Passes.ImGui.cppm
        Graphics.IORegistry.cppm
        Importers/Graphics.Importers.OBJ.cppm
        Importers/Graphics.Importers.PLY.cppm
        Importers/Graphics.Importers.XYZ.cppm
        Importers/Graphics.Importers.TGF.cppm
        Importers/Graphics.Importers.GLTF.cppm
        Importers/Graphics.Importers.STL.cppm
        Importers/Graphics.Importers.OFF.cppm
        Exporters/Graphics.Exporters.OBJ.cppm
        Exporters/Graphics.Exporters.PLY.cppm
        Exporters/Graphics.Exporters.STL.cppm
        PRIVATE
        Graphics.TinyGltf.cpp
        Graphics.ShaderCompiler.cpp
        PRIVATE
        FILE_SET graphics_impl TYPE CXX_MODULES FILES
        Graphics.Camera.cpp
        Graphics.Geometry.cpp
        Graphics.Material.cpp
        Graphics.MaterialSystem.cpp
        Graphics.RenderSystem.cpp
        Graphics.RenderGraph.cpp
        Graphics.ModelLoader.cpp
        Graphics.TextureLoader.cpp
        Graphics.PipelineLibrary.cpp
        Graphics.GPUScene.cpp
        Graphics.Systems.GPUSceneSync.cpp
        Graphics.Systems.MeshRendererLifecycle.cpp
        # New Implementations
        Graphics.DebugDraw.cpp
        Graphics.OctreeDebugDraw.cpp
        Graphics.BoundingDebugDraw.cpp
        Graphics.KDTreeDebugDraw.cpp
        Graphics.Interaction.cpp
        Graphics.Presentation.cpp
        Graphics.GlobalResources.cpp

        Pipelines/Graphics.Pipelines.cpp
        Passes/Graphics.Passes.Picking.cpp
        Passes/Graphics.Passes.Forward.cpp
        Passes/Graphics.Passes.SelectionOutline.cpp
        Passes/Graphics.Passes.Line.cpp
        Passes/Graphics.Passes.PointCloud.cpp
        Passes/Graphics.Passes.DebugView.cpp
        Graphics.IORegistry.cpp
        Importers/Graphics.Importers.OBJ.cpp
        Importers/Graphics.Importers.PLY.cpp
        Importers/Graphics.Importers.XYZ.cpp
        Importers/Graphics.Importers.TGF.cpp
        Importers/Graphics.Importers.GLTF.cpp
        Importers/Graphics.Importers.STL.cpp
        Importers/Graphics.Importers.OFF.cpp
        Exporters/Graphics.Exporters.OBJ.cpp
        Exporters/Graphics.Exporters.PLY.cpp
        Exporters/Graphics.Exporters.STL.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(${target_name} SYSTEM PUBLIC
        ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        IntrinsicGeometry
        IntrinsicRHI
        IntrinsicECS
        IntrinsicInterface
)
