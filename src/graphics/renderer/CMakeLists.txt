
set(target_name ExtrinsicGraphics)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.Colormap.cppm
        Graphics.FrameRecipe.cppm
        Graphics.Renderer.cppm
        Graphics.RenderFrameInput.cppm
        Graphics.RenderWorld.cppm
        Graphics.GpuWorld.cppm
        Graphics.Material.cppm
        Graphics.MaterialSystem.cppm
        Graphics.ColormapSystem.cppm
        Graphics.VisualizationPackets.cppm
        Graphics.VisualizationSyncSystem.cppm
        Graphics.CullingSystem.cppm
        Graphics.DebugViewSystem.cppm
        Graphics.ImGuiOverlaySystem.cppm
        Graphics.LightSystem.cppm
        Graphics.SelectionSystem.cppm
        Graphics.ForwardSystem.cppm
        Graphics.SpatialDebugVisualizers.cppm
        Graphics.GpuScene.cppm
        Graphics.DeferredSystem.cppm
        Graphics.PostProcessSystem.cppm
        Graphics.ShadowSystem.cppm
        Graphics.TransformSyncSystem.cppm
        PRIVATE
        Graphics.Renderer.cpp
        Graphics.GpuWorld.cpp
        Graphics.MaterialSystem.cpp
        Graphics.ColormapSystem.cpp
        Graphics.VisualizationPackets.cpp
        Graphics.VisualizationSyncSystem.cpp
        Graphics.GpuScene.cpp
        Graphics.CullingSystem.cpp
        Graphics.DebugViewSystem.cpp
        Graphics.ImGuiOverlaySystem.cpp
        Graphics.LightSystem.cpp
        Graphics.SelectionSystem.cpp
        Graphics.ForwardSystem.cpp
        Graphics.DeferredSystem.cpp
        Graphics.PostProcessSystem.cpp
        Graphics.ShadowSystem.cpp
        Graphics.TransformSyncSystem.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        ExtrinsicCore
        ExtrinsicGraphicsRenderGraph
        PRIVATE
        glm::glm
)

add_subdirectory(Components)
add_subdirectory(Backends)
add_subdirectory(Passes)
