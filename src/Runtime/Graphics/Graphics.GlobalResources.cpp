module;
#include <cstring>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.GlobalResources;

import Graphics.ShaderRegistry;
import Graphics.PipelineLibrary;
import Graphics.Camera;
import Core.Logging;
import RHI.Bindless;
import RHI.Buffer;
import RHI.CommandUtils;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;
import RHI.TransientAllocator;
import RHI.Types;
import Graphics.RenderPipeline;

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

        // 4. Create shadow atlas comparison sampler (hardware-accelerated PCF).
        {
            VkSamplerCreateInfo samp{};
            samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samp.magFilter = VK_FILTER_LINEAR;
            samp.minFilter = VK_FILTER_LINEAR;
            samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samp.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // outside = lit (depth 1.0)
            samp.compareEnable = VK_TRUE;
            samp.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            samp.maxAnisotropy = 1.0f;
            samp.minLod = 0.0f;
            samp.maxLod = 0.0f;
            if (vkCreateSampler(m_Device->GetLogicalDevice(), &samp, nullptr, &m_ShadowComparisonSampler) != VK_SUCCESS)
                Core::Log::Error("GlobalResources: Failed to create shadow comparison sampler");
        }

        // 5. Create dummy 1x1 depth image for initial shadow atlas binding.
        m_DummyShadowImage = std::make_unique<RHI::VulkanImage>(
            *m_Device, 1, 1, 1,
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);

        // Transition dummy to shader read and bind to global set.
        {
            VkCommandBuffer cmd = RHI::CommandUtils::BeginSingleTimeCommands(*m_Device);
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_DummyShadowImage->GetHandle();
            barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            barrier.srcAccessMask = 0;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(cmd, &dep);
            RHI::CommandUtils::EndSingleTimeCommands(*m_Device, cmd);
        }

        // Write initial dummy shadow atlas to global set binding 1.
        if (m_GlobalDescriptorSet != VK_NULL_HANDLE && m_DummyShadowImage && m_ShadowComparisonSampler)
        {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = m_ShadowComparisonSampler;
            imageInfo.imageView = m_DummyShadowImage->GetView();
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_GlobalDescriptorSet;
            write.dstBinding = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
        }

        // 6. Create Transient Allocator
        m_TransientAllocator = std::make_unique<RHI::TransientAllocator>(*m_Device);
    }

    GlobalResources::~GlobalResources()
    {
        if (m_ShadowComparisonSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device->GetLogicalDevice(), m_ShadowComparisonSampler, nullptr);
            m_ShadowComparisonSampler = VK_NULL_HANDLE;
        }
        m_DummyShadowImage.reset();
        // Allocator and Buffer destructors handle cleanup.
        // Descriptor Set is freed by pool reset in Engine.
    }

    void GlobalResources::BeginFrame(uint32_t frameIndex)
    {
        // Transient allocator logic could be per-frame here if we had multi-buffered pages.
        // Currently TransientAllocator manages its own lifetimes via Trim().
        (void)frameIndex;
    }

    void GlobalResources::Update(const CameraComponent& camera, const LightEnvironmentPacket& lighting, uint32_t frameIndex)
    {
        RHI::CameraBufferObject ubo{};
        const ShadowCascadeData packedShadowCascades =
            PackShadowCascadeData(lighting.Shadows, lighting.ShadowCascades.LightViewProjection);

        ubo.View = camera.ViewMatrix;
        ubo.Proj = camera.ProjectionMatrix;
        ubo.LightDirAndIntensity = glm::vec4(lighting.LightDirection, lighting.LightIntensity);
        ubo.LightColor = glm::vec4(lighting.LightColor, 0.0f);
        ubo.AmbientColorAndIntensity = glm::vec4(lighting.AmbientColor, lighting.AmbientIntensity);
        for (uint32_t i = 0; i < ShadowParams::MaxCascades; ++i)
            ubo.ShadowCascadeMatrices[i] = packedShadowCascades.LightViewProjection[i];
        ubo.ShadowCascadeSplitsAndCount = glm::vec4(
            packedShadowCascades.SplitDistances[0],
            packedShadowCascades.SplitDistances[1],
            packedShadowCascades.SplitDistances[2],
            packedShadowCascades.SplitDistances[3]);
        ubo.ShadowBiasAndFilter = glm::vec4(
            packedShadowCascades.DepthBias,
            packedShadowCascades.NormalBias,
            packedShadowCascades.PcfFilterRadius,
            static_cast<float>(packedShadowCascades.CascadeCount));

        const size_t dynamicOffset = frameIndex * m_CameraAlignedSize;

        void* ptr = m_CameraUBO->Map();
        if (ptr)
        {
            std::memcpy(static_cast<char*>(ptr) + dynamicOffset, &ubo, m_CameraDataSize);
            m_CameraUBO->Flush(dynamicOffset, m_CameraDataSize);
            m_CameraUBO->Unmap();
        }
    }

    uint32_t GlobalResources::GetDynamicUBOOffset(uint32_t frameIndex) const
    {
        return static_cast<uint32_t>(frameIndex * m_CameraAlignedSize);
    }

    void GlobalResources::UpdateShadowAtlasBinding(VkImageView atlasView)
    {
        if (m_GlobalDescriptorSet == VK_NULL_HANDLE || m_ShadowComparisonSampler == VK_NULL_HANDLE)
            return;

        VkImageView view = atlasView;
        if (view == VK_NULL_HANDLE && m_DummyShadowImage)
            view = m_DummyShadowImage->GetView();
        if (view == VK_NULL_HANDLE)
            return;

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_ShadowComparisonSampler;
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_GlobalDescriptorSet;
        write.dstBinding = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);
    }
}
