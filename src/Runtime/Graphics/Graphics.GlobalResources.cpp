module;
#include <cstring>
#include <memory>
#include <vector>
#include "RHI.Vulkan.hpp"

module Graphics:GlobalResources.Impl;

import :GlobalResources;
import Core;
import RHI;

namespace Graphics
{
    static size_t PadUniformBufferSize(size_t originalSize, size_t minAlignment)
    {
        if (minAlignment > 0)
        {
            return (originalSize + minAlignment - 1) & ~(minAlignment - 1);
        }
        return originalSize;
    }

    GlobalResources::GlobalResources(std::shared_ptr<RHI::VulkanDevice> device,
                                     RHI::DescriptorAllocator& descriptorPool,
                                     RHI::DescriptorLayout& descriptorLayout,
                                     RHI::BindlessDescriptorSystem& bindlessSystem,
                                     const ShaderRegistry& shaderRegistry,
                                     PipelineLibrary& pipelineLibrary,
                                     uint32_t framesInFlight)
        : m_Device(std::move(device)),
          m_DescriptorPool(descriptorPool),
          m_DescriptorLayout(descriptorLayout),
          m_BindlessSystem(bindlessSystem),
          m_ShaderRegistry(shaderRegistry),
          m_PipelineLibrary(pipelineLibrary)
    {
        // 1. Calculate UBO alignment
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);
        m_MinUboAlignment = props.limits.minUniformBufferOffsetAlignment;

        m_CameraDataSize = sizeof(RHI::CameraBufferObject);
        m_CameraAlignedSize = PadUniformBufferSize(m_CameraDataSize, m_MinUboAlignment);

        // 2. Create Camera UBO
        m_CameraUBO = std::make_unique<RHI::VulkanBuffer>(
            *m_Device,
            m_CameraAlignedSize * framesInFlight,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            static_cast<VmaMemoryUsage>(VMA_MEMORY_USAGE_CPU_TO_GPU));

        // 3. Create Global Descriptor Set (Set=0)
        m_GlobalDescriptorSet = m_DescriptorPool.Allocate(m_DescriptorLayout.GetHandle());

        if (m_GlobalDescriptorSet != VK_NULL_HANDLE && m_CameraUBO && m_CameraUBO->GetHandle() != VK_NULL_HANDLE)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_CameraUBO->GetHandle();
            bufferInfo.offset = 0;
            bufferInfo.range = m_CameraDataSize; // Bind only one struct size (dynamic offset handles the rest)

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_GlobalDescriptorSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
        }
        else
        {
            Core::Log::Error("GlobalResources: Failed to initialize Global UBO or Descriptor Set");
        }

        // 4. Create Transient Allocator
        m_TransientAllocator = std::make_unique<RHI::TransientAllocator>(*m_Device);
    }

    GlobalResources::~GlobalResources()
    {
        // Allocator and Buffer destructors handle cleanup.
        // Descriptor Set is freed by pool reset in Engine.
    }

    void GlobalResources::BeginFrame(uint32_t frameIndex)
    {
        // Transient allocator logic could be per-frame here if we had multi-buffered pages.
        // Currently TransientAllocator manages its own lifetimes via Trim().
        (void)frameIndex;
    }

    void GlobalResources::Update(const CameraComponent& camera, uint32_t frameIndex)
    {
        RHI::CameraBufferObject ubo{};
        ubo.View = camera.ViewMatrix;
        ubo.Proj = camera.ProjectionMatrix;

        const size_t dynamicOffset = frameIndex * m_CameraAlignedSize;

        // Map, Copy, Unmap (handled by VulkanBuffer internal mapping if persistent)
        // Note: Map() returns the base pointer.
        void* ptr = m_CameraUBO->Map();
        if (ptr)
        {
            std::memcpy(static_cast<char*>(ptr) + dynamicOffset, &ubo, m_CameraDataSize);
            m_CameraUBO->Flush(dynamicOffset, m_CameraDataSize); // If non-coherent
            m_CameraUBO->Unmap();
        }
    }

    uint32_t GlobalResources::GetDynamicUBOOffset(uint32_t frameIndex) const
    {
        return static_cast<uint32_t>(frameIndex * m_CameraAlignedSize);
    }
}
