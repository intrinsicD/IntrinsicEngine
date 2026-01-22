module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <queue>
#include <mutex>

export module RHI:Bindless;

import :Device;
import :Texture;

export namespace RHI
{
    class BindlessDescriptorSystem
    {
    public:
        explicit BindlessDescriptorSystem(VulkanDevice& device);

        ~BindlessDescriptorSystem();

        // Legacy convenience: keep for existing call sites.
        void SetTexture(uint32_t index, const Texture& texture);

        // Preferred API: update slot using raw Vulkan handles (decouples from Texture class).
        void UpdateTexture(uint32_t index, VkImageView view, VkSampler sampler,
                           VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Reset a slot to "null" (view/sampler = VK_NULL_HANDLE).
        // With PARTIALLY_BOUND, shaders must not sample unbound slots.
        void UnregisterTexture(uint32_t index);

        [[nodiscard]] VkDescriptorSet GetGlobalSet() const { return m_GlobalSet; }
        [[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }
        [[nodiscard]] uint32_t GetCapacity() const { return m_MaxDescriptors; }

    private:
        VulkanDevice& m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;
        uint32_t m_MaxDescriptors = 0;
        std::mutex m_UpdateMutex;

        void CreateLayout();
        void CreatePoolAndSet();
    };
}
