module;
#include <vector>
#include <mutex>
#include <cstdint>
#include "RHI.Vulkan.hpp"

export module RHI.TransientAllocator;

import RHI.Device;

export namespace RHI
{
    class TransientAllocator
    {
    public:
        struct Allocation
        {
            VkDeviceMemory Memory = VK_NULL_HANDLE;
            VkDeviceSize Offset = 0;
            VkDeviceSize Size = 0;

            [[nodiscard]] bool IsValid() const { return Memory != VK_NULL_HANDLE; }
        };

        explicit TransientAllocator(VulkanDevice& device, VkDeviceSize pageSizeBytes = 256ull * 1024ull * 1024ull);
        ~TransientAllocator();

        TransientAllocator(const TransientAllocator&) = delete;
        TransientAllocator& operator=(const TransientAllocator&) = delete;

        // Reset only the allocator state for the frame slot being recorded.
        void Reset(uint32_t frameIndex);

        // O(1) bump allocation (amortized O(1) including occasionally growing pages).
        [[nodiscard]] Allocation Allocate(uint32_t frameIndex,
                                          const VkMemoryRequirements& reqs,
                                          VkMemoryPropertyFlags preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    private:
        struct Page
        {
            VkDeviceMemory Memory = VK_NULL_HANDLE;
            VkDeviceSize Size = 0;
            VkDeviceSize UsedOffset = 0;
            void* MappedPtr = nullptr; // reserved for HOST_VISIBLE pages
        };

        struct PageBucket
        {
            uint32_t MemoryTypeIndex = 0;
            std::vector<Page> Pages{};
            size_t ActivePageIndex = 0;
        };

        [[nodiscard]] uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
        [[nodiscard]] Page CreatePage(uint32_t memoryTypeIndex, VkDeviceSize sizeBytes) const;
        [[nodiscard]] uint32_t NormalizeFrameIndex(uint32_t frameIndex) const;

        VulkanDevice& m_Device;
        VkDeviceSize m_PageSize = 0;
        uint32_t m_FramesInFlight = 0;

        // One bucket set per frame-in-flight so transient memory is never reused
        // while an older submitted frame can still reference it on the GPU.
        std::vector<std::vector<PageBucket>> m_FrameBuckets;

        std::mutex m_Mutex;
    };
}
