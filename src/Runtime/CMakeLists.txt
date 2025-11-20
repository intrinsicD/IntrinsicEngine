set(target_name IntrinsicRuntime)
add_library(${target_name})

set_target_properties(${target_name} PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_options(${target_name} PRIVATE ${INTRINSIC_COMPILE_OPTIONS})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES
        FILES
        RHI/RHI.Context.cppm
        RHI/RHI.Device.cppm
        RHI/RHI.Swapchain.cppm
        RHI/RHI.Renderer.cppm
        RHI/RHI.Buffer.cppm
        RHI/RHI.CommandUtils.cppm
        RHI/RHI.Types.cppm
        RHI/RHI.Shader.cppm
        RHI/RHI.Pipeline.cppm
        RHI/RHI.Descriptors.cppm
        RHI/RHI.Image.cppm
        PRIVATE
        RHI/RHI.Context.cpp
        RHI/RHI.Device.cpp
        RHI/RHI.Swapchain.cpp
        RHI/RHI.Renderer.cpp
        RHI/RHI.Buffer.cpp
        RHI/RHI.Shader.cpp
        RHI/RHI.Pipeline.cpp
        RHI/RHI.Vma.cpp
        RHI/RHI.Descriptors.cpp
        RHI/RHI.Image.cpp
)

target_include_directories(${target_name} PUBLIC .)

target_include_directories(IntrinsicRuntime SYSTEM PUBLIC ${vma_SOURCE_DIR}/include)

target_link_libraries(${target_name}
        PUBLIC
        IntrinsicCore
        volk
        Vulkan::Vulkan
)

target_compile_features(${target_name} PUBLIC cxx_std_23)