module;

#include <cstdio>
#include <mutex>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Internal;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §7  VulkanBindlessHeap
// =============================================================================

VulkanBindlessHeap::VulkanBindlessHeap(VkDevice device, uint32_t capacity)
    : m_Device(device), m_Capacity(capacity)
{
    // Descriptor pool — combined image sampler, partially bound.
    const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, capacity};
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets       = 1;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    VK_CHECK_FATAL(vkCreateDescriptorPool(m_Device, &poolCI, nullptr, &m_Pool));

    // Binding flags — partially bound + update-after-bind.
    const VkDescriptorBindingFlags bindFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindFlagsCI{};
    bindFlagsCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindFlagsCI.bindingCount  = 1;
    bindFlagsCI.pBindingFlags = &bindFlags;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = capacity;
    binding.stageFlags      = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.pNext        = &bindFlagsCI;
    layoutCI.bindingCount = 1;
    layoutCI.pBindings    = &binding;
    layoutCI.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    VK_CHECK_FATAL(vkCreateDescriptorSetLayout(m_Device, &layoutCI, nullptr, &m_Layout));

    VkDescriptorSetAllocateInfo allocCI{};
    allocCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocCI.descriptorPool     = m_Pool;
    allocCI.descriptorSetCount = 1;
    allocCI.pSetLayouts        = &m_Layout;
    VK_CHECK_FATAL(vkAllocateDescriptorSets(m_Device, &allocCI, &m_Set));
}

VulkanBindlessHeap::~VulkanBindlessHeap()
{
    if (m_Layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_Device, m_Layout, nullptr);
    if (m_Pool   != VK_NULL_HANDLE) vkDestroyDescriptorPool(m_Device, m_Pool, nullptr);
}

void VulkanBindlessHeap::SetDefault(VkImageView view, VkSampler sampler)
{
    // Slot 0 — written immediately, not via pending queue.
    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView   = view;
    imgInfo.sampler     = sampler;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_Set;
    write.dstBinding      = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
}

RHI::BindlessIndex VulkanBindlessHeap::AllocateTextureSlot(RHI::TextureHandle, RHI::SamplerHandle)
{
    std::scoped_lock lock{m_Mutex};
    RHI::BindlessIndex slot = RHI::kInvalidBindlessIndex;
    if (!m_FreeSlots.empty())
    {
        slot = m_FreeSlots.back();
        m_FreeSlots.pop_back();
    }
    else
    {
        if (m_NextSlot >= m_Capacity) return RHI::kInvalidBindlessIndex;
        slot = m_NextSlot++;
    }
    m_Pending.push_back({OpType::Allocate, slot});
    return slot;
}

void VulkanBindlessHeap::UpdateTextureSlot(RHI::BindlessIndex slot,
                                            RHI::TextureHandle,
                                            RHI::SamplerHandle)
{
    // Raw Vk handles are injected via EnqueueRawUpdate called by VulkanDevice.
    (void)slot;
}

void VulkanBindlessHeap::FreeSlot(RHI::BindlessIndex slot)
{
    std::scoped_lock lock{m_Mutex};
    if (slot == RHI::kInvalidBindlessIndex || slot >= m_NextSlot) return;
    m_Pending.push_back({OpType::Free, slot});
}

void VulkanBindlessHeap::EnqueueRawUpdate(RHI::BindlessIndex slot,
                                           VkImageView view, VkSampler sampler,
                                           VkImageLayout layout)
{
    std::scoped_lock lock{m_Mutex};
    m_Pending.push_back({OpType::UpdateRaw, slot, view, sampler, layout});
}

void VulkanBindlessHeap::FlushPending()
{
    std::scoped_lock lock{m_Mutex};
    std::vector<VkWriteDescriptorSet>  writes;
    std::vector<VkDescriptorImageInfo> imgInfos;
    imgInfos.reserve(m_Pending.size());

    for (const auto& op : m_Pending)
    {
        if (op.Type == OpType::Allocate)
        {
            ++m_AllocCount;
        }
        else if (op.Type == OpType::Free)
        {
            m_FreeSlots.push_back(op.Slot);
            if (m_AllocCount > 0) --m_AllocCount;
        }
        else // UpdateRaw
        {
            auto& info = imgInfos.emplace_back();
            info.imageView   = op.View;
            info.sampler     = op.Sampler;
            info.imageLayout = op.Layout;
            VkWriteDescriptorSet w{};
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = m_Set;
            w.dstBinding      = 0;
            w.dstArrayElement = op.Slot;
            w.descriptorCount = 1;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo      = &imgInfos.back();
            writes.push_back(w);
        }
    }
    m_Pending.clear();
    if (!writes.empty())
        vkUpdateDescriptorSets(m_Device,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
}

uint32_t VulkanBindlessHeap::GetAllocatedSlotCount() const
{
    std::scoped_lock lock{m_Mutex};
    return m_AllocCount;
}

} // namespace Extrinsic::Backends::Vulkan

