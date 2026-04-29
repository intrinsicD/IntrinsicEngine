
set(target_name ExtrinsicGraphicsRenderGraph)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Graphics.RenderGraph.cppm
        Graphics.RenderGraph.Resources.cppm
        Graphics.RenderGraph.Pass.cppm
        Graphics.RenderGraph.Compiler.cppm
        Graphics.RenderGraph.Barriers.cppm
        Graphics.RenderGraph.TransientAllocator.cppm
        Graphics.RenderGraph.Executor.cppm
        PRIVATE
        Graphics.RenderGraph.cpp
        Graphics.RenderGraph.Compiler.cpp
        Graphics.RenderGraph.TransientAllocator.cpp
        Graphics.RenderGraph.Executor.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        #ExtrinsicCore
        #ExtrinsicECS
        #PRIVATE
        #EnTT::EnTT
        #glm::glm
)
