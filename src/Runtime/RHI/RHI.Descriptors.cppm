module;
#include "RHI.Vulkan.hpp"
#include <vector>

export module RHI:Descriptors;

import :Device;

export namespace RHI
{
    class DescriptorLayout
    {
    public:
        DescriptorLayout(VulkanDevice& device);
        ~DescriptorLayout();

        [[nodiscard]] VkDescriptorSetLayout GetHandle() const { return m_Layout; }
        [[nodiscard]] bool IsValid() const { return m_IsValid; }

    private:
        VulkanDevice& m_Device;
        bool m_IsValid = true;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    };

    // Replaces DescriptorPool: grows by chaining VkDescriptorPools.
    // Intended usage: allocate many transient sets (materials, per-draw) and call Reset() at frame start.
    class DescriptorAllocator
    {
    public:
        explicit DescriptorAllocator(VulkanDevice& device);
        ~DescriptorAllocator();

        // Allocate a single set using the given layout.
        // Returns VK_NULL_HANDLE on failure.
        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

        // Call at start of frame to reset all pools used since last Reset().
        void Reset();

        [[nodiscard]] bool IsValid() const { return m_IsValid; }

    private:
        [[nodiscard]] VkDescriptorPool GrabPool();

        VulkanDevice& m_Device;
        bool m_IsValid = true;

        VkDescriptorPool m_CurrentPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_UsedPools;
        std::vector<VkDescriptorPool> m_FreePools;
    };
}
