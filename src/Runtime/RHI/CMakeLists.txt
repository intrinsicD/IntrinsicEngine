intrinsic_add_module_library(IntrinsicRHI)
set(target_name IntrinsicRHI)

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        RHI.cppm
        RHI.Bindless.cppm
        RHI.Context.cppm
        RHI.Device.cppm
        RHI.Swapchain.cppm
        RHI.Renderer.cppm
        RHI.Profiler.cppm
        RHI.Buffer.cppm
        RHI.CommandUtils.cppm
        RHI.CommandContext.cppm
        RHI.Types.cppm
        RHI.Shader.cppm
        RHI.Pipeline.cppm
        RHI.Descriptors.cppm
        RHI.Image.cppm
        RHI.Texture.cppm
        RHI.TextureSystem.cppm
        RHI.Transfer.cppm
        RHI.StagingBelt.cppm
        RHI.TransientAllocator.cppm
        RHI.SceneInstances.cppm
        RHI.PersistentDescriptors.cppm
        RHI.ComputePipeline.cppm
        PRIVATE
        RHI.Vma.cpp
        RHI.Transfer.cpp
        PRIVATE
        FILE_SET rhi_impl TYPE CXX_MODULES FILES
        RHI.Bindless.cpp
        RHI.Context.cpp
        RHI.Device.cpp
        RHI.Swapchain.cpp
        RHI.Renderer.cpp
        RHI.Profiler.cpp
        RHI.Buffer.cpp
        RHI.Shader.cpp
        RHI.Pipeline.cpp
        RHI.Descriptors.cpp
        RHI.Image.cpp
        RHI.Texture.cpp
        RHI.TextureSystem.cpp
        RHI.Transfer.cpp
        RHI.StagingBelt.cpp
        RHI.TransientAllocator.cpp
        RHI.PersistentDescriptors.cpp
        RHI.ComputePipeline.cpp
        RHI.CommandContext.cpp
)

target_include_directories(${target_name} PUBLIC .)
target_include_directories(${target_name} SYSTEM PUBLIC
        ${vma_SOURCE_DIR}/include
)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        volk::volk
        stb
        PRIVATE
        glfw
)