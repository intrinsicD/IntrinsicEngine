intrinsic_add_module_library(IntrinsicRHI)
set(target_name IntrinsicRHI)

# --- Collect CUDA sources conditionally ---
set(RHI_CUDA_MODULE_IFACES "")
set(RHI_CUDA_MODULE_IMPLS "")
if(INTRINSIC_ENABLE_CUDA)
    list(APPEND RHI_CUDA_MODULE_IFACES RHI.CudaDevice.cppm)
    list(APPEND RHI_CUDA_MODULE_IMPLS  RHI.CudaDevice.cpp)
endif()

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
        RHI.TextureManager.cppm
        RHI.TextureFwd.cppm
        RHI.TextureHandle.cppm
        RHI.Transfer.cppm
        RHI.StagingBelt.cppm
        RHI.TransientAllocator.cppm
        RHI.SceneInstances.cppm
        RHI.PersistentDescriptors.cppm
        RHI.ComputePipeline.cppm
        RHI.CudaError.cppm
        ${RHI_CUDA_MODULE_IFACES}
        PRIVATE
        RHI.Vma.cpp
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
        RHI.TextureManager.cpp
        RHI.Transfer.cpp
        RHI.StagingBelt.cpp
        RHI.TransientAllocator.cpp
        RHI.PersistentDescriptors.cpp
        RHI.ComputePipeline.cpp
        RHI.CommandContext.cpp
        ${RHI_CUDA_MODULE_IMPLS}
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        volk::volk
        stb
        VulkanMemoryAllocator
        PRIVATE
        glfw
)

# --- CUDA dependencies (conditional) ---
if(INTRINSIC_ENABLE_CUDA)
    target_link_libraries(${target_name} PUBLIC CUDA::cuda_driver)
    target_include_directories(${target_name} PUBLIC ${CUDAToolkit_INCLUDE_DIRS})
endif()
