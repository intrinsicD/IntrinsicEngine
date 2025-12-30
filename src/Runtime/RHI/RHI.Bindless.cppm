module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <queue>
#include <memory>

export module RHI:Bindless;

import :Device;
import :Texture;
import Core;

export namespace RHI {
    class BindlessDescriptorSystem {
    public:
        explicit BindlessDescriptorSystem(std::shared_ptr<VulkanDevice> device);

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
        std::shared_ptr<VulkanDevice> m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;

        std::queue<uint32_t> m_FreeSlots;
        uint32_t m_HighWaterMark = 0; // Current max index used
        uint32_t m_MaxDescriptors = 0;

        void CreateLayout();
        void CreatePoolAndSet();
    };
}