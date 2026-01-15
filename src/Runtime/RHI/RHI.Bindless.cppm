module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <queue>
#include <mutex>

export module RHI:Bindless;

import :Device;
import :Texture;
import Core;

export namespace RHI
{
    class BindlessDescriptorSystem
    {
    public:
        explicit BindlessDescriptorSystem(VulkanDevice& device);

        ~BindlessDescriptorSystem();

        // Returns the index in the global array
        uint32_t RegisterTexture(const Texture& texture);

        // Return index to free pool
        void UnregisterTexture(uint32_t index);

        // Update an existing slot (e.g., when async load finishes)
        void UpdateTexture(uint32_t index, const Texture& texture);

        [[nodiscard]] VkDescriptorSet GetGlobalSet() const { return m_GlobalSet; }
        [[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }

    private:
        struct PendingDeletion
        {
            uint32_t SlotIndex;
            uint32_t FrameIndex; // Frame when deletion was requested
        };

        std::vector<PendingDeletion> m_DeletionQueue;
        std::mutex m_DeletionMutex; // Need mutex as materials might die on any thread

        VulkanDevice& m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;

        std::mutex m_Mutex; // Protects access to m_FreeSlots and m_HighWaterMark
        std::queue<uint32_t> m_FreeSlots;
        uint32_t m_HighWaterMark = 0; // Current max index used
        uint32_t m_MaxDescriptors = 0;

        void CreateLayout();
        void CreatePoolAndSet();
    };
}
