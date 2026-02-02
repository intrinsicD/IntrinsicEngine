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
    };
}