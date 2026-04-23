set(target_name ExtrinsicBackendsVulkan)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Backends.Vulkan.cppm
        PRIVATE
        Backends.Vulkan.cpp
        Vma.cpp              # VMA_IMPLEMENTATION TU — must compile without modules
)

target_include_directories(${target_name} PRIVATE .)

target_link_libraries(${target_name}
        PUBLIC
        ExtrinsicRHI
        PRIVATE
        ExtrinsicCore
        Vulkan::Vulkan
        volk
        VulkanMemoryAllocator
        glfw
)

