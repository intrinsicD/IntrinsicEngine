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

        // Reserve a modest freelist capacity up-front to avoid reallocs.
        m_FreeBindlessSlots.reserve(1024);
    }

    TextureSystem::~TextureSystem()
    {
        m_Pool.Clear();
    }

    uint32_t TextureSystem::AllocateBindlessSlot()
    {
        // Prefer reuse to avoid exhausting the global bindless capacity.
        if (!m_FreeBindlessSlots.empty())
        {
            const uint32_t slot = m_FreeBindlessSlots.back();
            m_FreeBindlessSlots.pop_back();
            return slot;
        }

        return m_NextBindlessSlot++;
    }

    void TextureSystem::SetDefaultDescriptor(VkImageView view, VkSampler sampler)
    {
        m_DefaultView = view;
        m_DefaultSampler = sampler;
    }

    void TextureSystem::FreeBindlessSlot(uint32_t slot)
    {
        // Slot 0 is reserved for the engine default/error texture.
        if (slot == 0)
            return;

        // Make stale indices safe-by-construction: immediately rebind this slot to the default descriptor.
        if (m_DefaultView != VK_NULL_HANDLE && m_DefaultSampler != VK_NULL_HANDLE)
        {
            m_Bindless.UpdateTexture(slot, m_DefaultView, m_DefaultSampler);
        }
        else
        {
            Core::Log::Warn("TextureSystem::FreeBindlessSlot({}) called before default descriptor was set. Slot will be recycled with stale descriptor content.", slot);
        }

        m_FreeBindlessSlots.push_back(slot);
    }

    TextureHandle TextureSystem::CreateFromData(std::unique_ptr<TextureGpuData> gpuData)
    {
        if (!gpuData || !gpuData->Image)
            return {};

        // Allocate a stable shader-visible slot.
        gpuData->BindlessSlot = AllocateBindlessSlot();
        if (gpuData->BindlessSlot >= m_Bindless.GetCapacity())
        {
            Core::Log::Error("Bindless texture capacity exceeded (slot {} >= {}). Texture will not be visible.",
                             gpuData->BindlessSlot, m_Bindless.GetCapacity());
            // Keep slot reserved for now; alternatively we could FreeBindlessSlot here.
        }

        TextureHandle handle = m_Pool.Add(std::move(gpuData));

        if (handle.IsValid())
        {
            const TextureGpuData* data = m_Pool.GetUnchecked(handle);
            // Write descriptor at the stable slot.
            m_Bindless.UpdateTexture(data->BindlessSlot, data->Image->GetView(), data->Sampler);
        }

        return handle;
    }

    void TextureSystem::Destroy(TextureHandle handle)
    {
        if (!handle.IsValid())
            return;

        // Free bindless slot immediately (descriptor content may remain stale but should never be sampled once
        // higher-level code stops referencing the slot).
        if (auto data = m_Pool.Get(handle); data)
        {
            FreeBindlessSlot((*data)->BindlessSlot);
        }

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

        // Reclaim all bindless slots except 0.
        // We can't iterate ResourcePool entries directly here without knowing its API, so we just reset allocator state.
        m_FreeBindlessSlots.clear();
        m_NextBindlessSlot = 1;

        m_Pool.Clear();
    }
}
