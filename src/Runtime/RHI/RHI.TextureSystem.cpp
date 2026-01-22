module;
#include <memory>
#include "RHI.Vulkan.hpp"

module RHI:TextureSystem.Impl;

import :TextureSystem;
import Core;

namespace RHI
{
    TextureSystem::TextureSystem(VulkanDevice& device, BindlessDescriptorSystem& bindless)
        : m_Device(device)
        , m_Bindless(bindless)
    {
        m_Pool.Initialize(device.GetFramesInFlight());
    }

    TextureSystem::~TextureSystem()
    {
        m_Pool.Clear();
    }

    TextureHandle TextureSystem::CreateFromData(std::unique_ptr<TextureGpuData> gpuData)
    {
        if (!gpuData || !gpuData->Image)
            return {};

        TextureHandle handle = m_Pool.Add(std::move(gpuData));

        if (handle.IsValid())
        {
            if (handle.Index >= m_Bindless.GetCapacity())
            {
                Core::Log::Error("TexturePool grew beyond Bindless Capacity! Texture will not be visible.");
                return handle;
            }

            const TextureGpuData* data = m_Pool.GetUnchecked(handle);
            m_Bindless.UpdateTexture(handle.Index, data->Image->GetView(), data->Sampler);
        }

        return handle;
    }

    void TextureSystem::Destroy(TextureHandle handle)
    {
        if (!handle.IsValid())
            return;

        m_Pool.Remove(handle, m_Device.GetGlobalFrameNumber());
    }

    void TextureSystem::ProcessDeletions()
    {
        m_Pool.ProcessDeletions(m_Device.GetGlobalFrameNumber());
    }

    const TextureGpuData* TextureSystem::Get(TextureHandle handle) const
    {
        auto res = m_Pool.Get(handle);
        if (res) return *res;
        return nullptr;
    }

    const TextureGpuData* TextureSystem::GetUnchecked(TextureHandle handle) const
    {
        return m_Pool.GetUnchecked(handle);
    }

    void TextureSystem::Clear()
    {
        // NOTE: This must only be called when the GPU is idle (vkDeviceWaitIdle already executed).
        // Clears pending kills and immediately releases all TextureGpuData heap objects (which free Vulkan images).
        m_Pool.Clear();
    }
}
