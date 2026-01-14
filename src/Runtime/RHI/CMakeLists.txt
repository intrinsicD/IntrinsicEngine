set(target_name IntrinsicRHI)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_FLAGS})
target_compile_features(${target_name} PUBLIC cxx_std_23)
target_link_options(${target_name} PRIVATE ${INTRINSIC_LINK_FLAGS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        RHI.cppm
        RHI.Bindless.cppm
        RHI.Context.cppm
        RHI.Device.cppm
        RHI.Swapchain.cppm
        RHI.Renderer.cppm
        RHI.Buffer.cppm
        RHI.CommandUtils.cppm
        RHI.Types.cppm
        RHI.Shader.cppm
        RHI.Pipeline.cppm
        RHI.Descriptors.cppm
        RHI.Image.cppm
        RHI.Texture.cppm
        RHI.Transfer.cppm
        RHI.StagingBelt.cppm
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
        RHI.Buffer.cpp
        RHI.Shader.cpp
        RHI.Pipeline.cpp
        RHI.Descriptors.cpp
        RHI.Image.cpp
        RHI.Texture.cpp
        RHI.Transfer.cpp
        RHI.StagingBelt.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        volk::volk
        stb
)