add_subdirectory(RHI)

set(target_name ExtrinsicGraphics)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.Renderer.cppm
        Graphics.RenderFrameInput.cppm
        Graphics.RenderWorld.cppm
        Graphics.Material.cppm
        Graphics.MaterialSystem.cppm
        Graphics.CullingSystem.cppm
        Graphics.LightSystem.cppm
        Graphics.SelectionSystem.cppm
        Graphics.ForwardSystem.cppm
        Graphics.DeferredSystem.cppm
        Graphics.PostProcessSystem.cppm
        Graphics.ShadowSystem.cppm
        PRIVATE
        Graphics.Renderer.cpp
        Graphics.MaterialSystem.cpp
        Graphics.CullingSystem.cpp
        Graphics.LightSystem.cpp
        Graphics.SelectionSystem.cpp
        Graphics.ForwardSystem.cpp
        Graphics.DeferredSystem.cpp
        Graphics.PostProcessSystem.cpp
        Graphics.ShadowSystem.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        ExtrinsicCore
)

add_subdirectory(Backends)
add_subdirectory(Passes)
