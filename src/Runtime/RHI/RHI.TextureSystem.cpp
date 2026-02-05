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
            m_Bindless.EnqueueUpdate(slot, m_DefaultView, m_DefaultSampler);
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
            // Queue descriptor update at the stable slot.
            m_Bindless.EnqueueUpdate(data->BindlessSlot, data->Image->GetView(), data->Sampler);
        }

        return handle;
    }

    TextureHandle TextureSystem::CreatePending(uint32_t width, uint32_t height, VkFormat format)
    {
        // Allocate a pool entry with a bindless slot, but keep it bound to default.
        auto gpu = std::make_unique<TextureGpuData>();

        auto indices = m_Device.GetQueueIndices();
        bool distinctQueues = false;
        if (indices.GraphicsFamily.has_value() && indices.TransferFamily.has_value())
            distinctQueues = (indices.GraphicsFamily.value() != indices.TransferFamily.value());

        VkSharingMode sharingMode = distinctQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

        gpu->Image = std::make_unique<VulkanImage>(
            m_Device,
            width,
            height,
            /*mips*/ 1,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            sharingMode);

        // Sampler will be created later when real data is published; but we can create one now for consistency.
        // Use a default sampler so tools can sample this immediately.
        {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerInfo.anisotropyEnable = VK_TRUE;

            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &properties);
            samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
            samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerInfo.unnormalizedCoordinates = VK_FALSE;
            samplerInfo.minLod = 0.0f;
            samplerInfo.maxLod = 1.0f;

            VK_CHECK(vkCreateSampler(m_Device.GetLogicalDevice(), &samplerInfo, nullptr, &gpu->Sampler));
        }

        // Allocate stable slot.
        gpu->BindlessSlot = AllocateBindlessSlot();

        if (gpu->BindlessSlot >= m_Bindless.GetCapacity())
        {
            Core::Log::Error("Bindless texture capacity exceeded (slot {} >= {}). Texture will not be visible.",
                             gpu->BindlessSlot, m_Bindless.GetCapacity());
        }

        // Insert into pool.
        TextureHandle handle = m_Pool.Add(std::move(gpu));

        // Immediately bind to default so sampling is safe until publish.
        if (handle.IsValid())
        {
            const TextureGpuData* data = m_Pool.GetUnchecked(handle);
            if (m_DefaultView != VK_NULL_HANDLE && m_DefaultSampler != VK_NULL_HANDLE)
            {
                m_Bindless.EnqueueUpdate(data->BindlessSlot, m_DefaultView, m_DefaultSampler);
            }
            else
            {
                // Engine init order: CreatePending may be called before SetDefaultDescriptor().
                // In that case, bind the real view/sampler now so this texture isn't permanently stuck sampling an uninitialized/default slot.
                m_Bindless.EnqueueUpdate(data->BindlessSlot, data->Image->GetView(), data->Sampler);
            }
        }

        return handle;
    }

    void TextureSystem::Publish(TextureHandle handle, std::unique_ptr<TextureGpuData> gpuData)
    {
        if (!handle.IsValid() || !gpuData || !gpuData->Image)
            return;

        auto slot = m_Pool.Get(handle);
        if (!slot)
            return;

        TextureGpuData* dst = *slot;
        if (!dst)
            return;

        // Preserve stable bindless slot assigned at creation.
        const uint32_t bindlessSlot = dst->BindlessSlot;
        gpuData->BindlessSlot = bindlessSlot;

        // Schedule destruction of any old sampler on this slot.
        if (dst->Sampler)
        {
            VkDevice logicalDevice = m_Device.GetLogicalDevice();
            VkSampler oldSampler = dst->Sampler;
            m_Device.SafeDestroy([logicalDevice, oldSampler]()
            {
                vkDestroySampler(logicalDevice, oldSampler, nullptr);
            });
        }

        // Overwrite contents in-place; pool owns the allocation.
        *dst = std::move(*gpuData);
        dst->BindlessSlot = bindlessSlot;

        // Update bindless descriptor to point to the real image/sampler.
        m_Bindless.EnqueueUpdate(dst->BindlessSlot, dst->Image->GetView(), dst->Sampler);
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
