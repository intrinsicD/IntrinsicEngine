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
        VulkanDevice& m_Device;
        BindlessDescriptorSystem& m_Bindless;
        Core::ResourcePool<TextureGpuData, TextureHandle> m_Pool;
    };
}