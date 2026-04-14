set(target_name ExtrinsicRHI)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        RHI.BackendFactory.cppm
        RHI.Device.cppm
        RHI.CommandContext.cppm
        RHI.FrameHandle.cppm
        PRIVATE
        RHI.BackendFactory.cpp
)

target_include_directories(${target_name} PUBLIC .)