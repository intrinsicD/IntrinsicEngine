// src/Runtime/RHI/RHI.TextureSystem.cppm
module;
#include <memory>
#include <vector>
#include "RHI.Vulkan.hpp"

export module RHI:TextureSystem;

import :Texture;
import :Bindless;
import Core;

export namespace RHI
{
    class TextureSystem
    {
    public:
        TextureSystem(VulkanDevice& device, BindlessDescriptorSystem& bindless);
        ~TextureSystem();

        TextureHandle CreateFromData(std::unique_ptr<TextureGpuData> gpuData);
        void Destroy(TextureHandle handle);
        void ProcessDeletions();

        // Force-release all textures immediately (must be called only when GPU is idle).
        void Clear();

        [[nodiscard]] const TextureGpuData* Get(TextureHandle handle) const;
        [[nodiscard]] const TextureGpuData* GetUnchecked(TextureHandle handle) const;

        // Default/fallback texture descriptor (bindless slot 0 contract).
        // Must be called once during engine init after bindless is created.
        void SetDefaultDescriptor(VkImageView view, VkSampler sampler);

        // Create a "pending" texture entry.
        // - Allocates a pool slot + stable bindless slot.
        // - Immediately binds the bindless slot to the default descriptor.
        // - Returns a handle that can be wrapped by RHI::Texture and referenced by materials.
        [[nodiscard]] TextureHandle CreatePending(uint32_t width, uint32_t height, VkFormat format);

        // Publish final GPU image + sampler into the bindless slot owned by 'handle'.
        // Contract: handle must be valid and belong to this system.
        // This is intended to be called when the TransferToken completes.
        void Publish(TextureHandle handle, std::unique_ptr<TextureGpuData> gpuData);

    private:
        [[nodiscard]] uint32_t AllocateBindlessSlot();
        void FreeBindlessSlot(uint32_t slot);

        VulkanDevice& m_Device;
        BindlessDescriptorSystem& m_Bindless;
        Core::ResourcePool<TextureGpuData, TextureHandle> m_Pool;

        // Bindless slot allocator.
        // Slot 0 is reserved for the engine default/error texture and is never freed.
        // NOTE: We currently allocate monotonically and do not reuse slots during a run.
        std::vector<uint32_t> m_FreeBindlessSlots;
        uint32_t m_NextBindlessSlot = 1;

        VkImageView m_DefaultView = VK_NULL_HANDLE;
        VkSampler m_DefaultSampler = VK_NULL_HANDLE;
    };
}