module;
#include "RHI.Vulkan.hpp"
#include <vector>
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

        // Legacy convenience: queues an update.
        void SetTexture(uint32_t index, const Texture& texture);

        // Queue a descriptor update. Thread-safe.
        // The update is applied on the next call to FlushPending().
        void EnqueueUpdate(uint32_t index, VkImageView view, VkSampler sampler,
                           VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Apply all queued updates to the GPU. Call once per frame on the main thread.
        void FlushPending();

        // Reset a slot to "null" (view/sampler = VK_NULL_HANDLE).
        // With PARTIALLY_BOUND, shaders must not sample unbound slots.
        void UnregisterTexture(uint32_t index);

        [[nodiscard]] VkDescriptorSet GetGlobalSet() const { return m_GlobalSet; }
        [[nodiscard]] VkDescriptorSetLayout GetLayout() const { return m_Layout; }
        [[nodiscard]] uint32_t GetCapacity() const { return m_MaxDescriptors; }

    private:
        struct PendingUpdate
        {
            uint32_t Index;
            VkImageView View;
            VkSampler Sampler;
            VkImageLayout Layout;
        };

        VulkanDevice& m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
        VkDescriptorSet m_GlobalSet = VK_NULL_HANDLE;
        uint32_t m_MaxDescriptors = 0;
        std::mutex m_UpdateMutex;
        std::vector<PendingUpdate> m_PendingUpdates;

        void CreateLayout();
        void CreatePoolAndSet();
    };
}
