set(target_name ExtrinsicBackendsVulkan)
intrinsic_add_module_library(${target_name})

target_sources(${target_name}
        PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
        Backends.Vulkan.cppm
        Backends.Vulkan.Internal.cppm   # non-exported :Internal partition
        PRIVATE
        Backends.Vulkan.Mappings.cpp        # §2  RHI enum → VkEnum tables
        Backends.Vulkan.Staging.cpp         # §5  StagingBelt ring-buffer
        Backends.Vulkan.Profiler.cpp        # §6  VulkanProfiler
        Backends.Vulkan.Bindless.cpp        # §7  VulkanBindlessHeap
        Backends.Vulkan.Transfer.cpp        # §8  VulkanTransferQueue
        Backends.Vulkan.CommandContext.cpp  # §9  VulkanCommandContext
        Backends.Vulkan.Device.cpp          # §11 VulkanDevice + §12 factory
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
