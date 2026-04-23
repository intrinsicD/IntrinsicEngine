set(target_name ExtrinsicRHI)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        RHI.BufferView.cppm
        RHI.Descriptors.cppm
        RHI.Handles.cppm
        RHI.Transfer.cppm
        RHI.TransferQueue.cppm
        RHI.Profiler.cppm
        RHI.Bindless.cppm
        RHI.Types.cppm
        RHI.CommandContext.cppm
        RHI.FrameHandle.cppm
        RHI.Device.cppm
        RHI.BufferManager.cppm
        RHI.TextureManager.cppm
        RHI.SamplerManager.cppm
        RHI.PipelineManager.cppm
        PRIVATE
        RHI.BufferManager.cpp
        RHI.TextureManager.cpp
        RHI.SamplerManager.cpp
        RHI.PipelineManager.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicPlatform
        glm)
