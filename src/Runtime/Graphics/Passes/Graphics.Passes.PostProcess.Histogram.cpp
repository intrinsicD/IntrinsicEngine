module;

#include <memory>
#include <span>
#include <string>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics.Passes.PostProcess.Histogram;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.ShaderRegistry;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

import RHI.Buffer;
import RHI.ComputePipeline;
import RHI.Descriptors;
import RHI.Device;
import RHI.Image;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    void HistogramSubPass::Initialize(RHI::VulkanDevice& device,
                                      RHI::DescriptorAllocator& descriptorPool,
                                      VkSampler linearSampler,
                                      VkImageView dummyView)
    {
        m_Device = &device;
        VkDevice dev = device.GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // Histogram: descriptor layout with 1 combined-image-sampler + 1 SSBO.
        {
            VkDescriptorSetLayoutBinding bindings[2] = {};
            bindings[0].binding = 0;
            bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[0].descriptorCount = 1;
            bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            bindings[1].binding = 1;
            bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[1].descriptorCount = 1;
            bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layoutInfo.bindingCount = 2;
            layoutInfo.pBindings = bindings;

            CheckVkResult(vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_SetLayout),
                          "PostProcess.Histogram", "vkCreateDescriptorSetLayout");
        }

        constexpr size_t kHistogramBytes = kHistogramBinCount * sizeof(uint32_t);
        for (uint32_t fi = 0; fi < 3; ++fi)
        {
            m_Sets[fi] = descriptorPool.Allocate(m_SetLayout);

            m_Buffers[fi] = std::make_unique<RHI::VulkanBuffer>(
                device, kHistogramBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_TO_CPU);

            UpdateSSBODescriptor(dev, m_Sets[fi], 1,
                                 m_Buffers[fi]->GetHandle(), kHistogramBytes);

            UpdateImageDescriptor(dev, m_Sets[fi], 0, linearSampler, dummyView, readLayout);
        }
    }

    std::unique_ptr<RHI::ComputePipeline> HistogramSubPass::BuildPipeline()
    {
        auto compPathOpt = m_ShaderRegistry->Get("Post.Histogram.Comp"_id);
        if (!compPathOpt)
        {
            Core::Log::Error("PostProcess: histogram compute shader not registered.");
            return nullptr;
        }

        std::string compPath = *compPathOpt;

        RHI::ShaderModule comp(*m_Device, compPath, RHI::ShaderStage::Compute);

        auto deviceAlias = MakeDeviceAlias(m_Device);
        RHI::ComputePipelineBuilder cb(deviceAlias);
        cb.SetShader(&comp);
        cb.AddDescriptorSetLayout(m_SetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 4;
        cb.AddPushConstantRange(pcr);

        auto built = cb.Build();
        if (!built)
        {
            Core::Log::Error("PostProcess: failed to build Histogram pipeline (VkResult={})", (int)built.error());
            return nullptr;
        }
        return std::move(*built);
    }

    void HistogramSubPass::AddPass(RenderPassContext& ctx,
                                   RGResourceHandle sceneColor,
                                   const PostProcessSettings& settings)
    {
        if (!m_Pipeline)
            m_Pipeline = BuildPipeline();
        if (!m_Pipeline)
            return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        const uint32_t fi = ctx.FrameIndex % kFrames;
        const VkExtent2D resolution = ctx.Resolution;
        const float minEV = settings.HistogramMinEV;
        const float maxEV = settings.HistogramMaxEV;
        const float rangeEV = (maxEV - minEV);
        const float invRange = (rangeEV > 1e-6f) ? (1.0f / rangeEV) : 1.0f;

        // Read back the previous frame's histogram before we overwrite it.
        if (m_Buffers[fi])
        {
            const auto* mappedData = static_cast<const uint32_t*>(m_Buffers[fi]->GetMappedData());
            if (mappedData)
            {
                std::memcpy(m_Readback.Bins, mappedData,
                            kHistogramBinCount * sizeof(uint32_t));

                uint64_t totalPixels = 0;
                double weightedSum = 0.0;
                for (uint32_t i = 0; i < kHistogramBinCount; ++i)
                {
                    if (m_Readback.Bins[i] > 0)
                    {
                        totalPixels += m_Readback.Bins[i];
                        float binCenter = minEV + (static_cast<float>(i) + 0.5f) / 254.0f * rangeEV;
                        weightedSum += static_cast<double>(m_Readback.Bins[i]) * binCenter;
                    }
                }
                if (totalPixels > 0)
                    m_Readback.AverageLuminance = std::exp2(
                        static_cast<float>(weightedSum / static_cast<double>(totalPixels)));
                else
                    m_Readback.AverageLuminance = 0.0f;

                m_Readback.Valid = true;
            }
        }

        struct HistogramData { RGResourceHandle Src; RGResourceHandle Buf; };

        ctx.Graph.AddPass<HistogramData>("Post.Histogram",
            [&, sceneColor, fi](HistogramData& data, RGBuilder& builder)
            {
                data.Src = builder.Read(sceneColor,
                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

                data.Buf = builder.ImportBuffer("HistogramSSBO"_id, *m_Buffers[fi]);
                builder.Write(data.Buf,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            },
            [this, fi, resolution, minEV, invRange]
            (const HistogramData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                constexpr size_t kHistogramBytes = kHistogramBinCount * sizeof(uint32_t);

                vkCmdFillBuffer(cmd, m_Buffers[fi]->GetHandle(),
                                0, kHistogramBytes, 0);

                VkBufferMemoryBarrier2 clearBarrier{};
                clearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                clearBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                clearBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                clearBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                clearBarrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                clearBarrier.buffer = m_Buffers[fi]->GetHandle();
                clearBarrier.offset = 0;
                clearBarrier.size = kHistogramBytes;

                VkDependencyInfo dep{};
                dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dep.bufferMemoryBarrierCount = 1;
                dep.pBufferMemoryBarriers = &clearBarrier;
                vkCmdPipelineBarrier2(cmd, &dep);

                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                  m_Pipeline->GetHandle());
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        m_Pipeline->GetLayout(),
                                        0, 1, &m_Sets[fi], 0, nullptr);

                struct HistogramPC {
                    uint32_t Width;
                    uint32_t Height;
                    float    MinLogLum;
                    float    RangeLogLum;
                };
                static_assert(sizeof(HistogramPC) == 16, "HistogramPC must be 16 bytes");
                HistogramPC pc{
                    resolution.width,
                    resolution.height,
                    minEV,
                    invRange
                };
                vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

                const uint32_t groupsX = (resolution.width + 15) / 16;
                const uint32_t groupsY = (resolution.height + 15) / 16;
                vkCmdDispatch(cmd, groupsX, groupsY, 1);
            }
        );
    }

    void HistogramSubPass::PostCompile(uint32_t frameIndex,
                                       std::span<const RenderGraphDebugImage> debugImages,
                                       VkSampler linearSampler,
                                       RGResourceHandle sceneColorHandle)
    {
        if (!m_Device) return;

        constexpr uint32_t kFrames = RHI::VulkanDevice::GetFramesInFlight();
        frameIndex %= kFrames;
        VkDevice dev = m_Device->GetLogicalDevice();
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        auto findView = [&](RGResourceHandle handle) -> VkImageView
        {
            if (!handle.IsValid()) return VK_NULL_HANDLE;
            for (const auto& img : debugImages)
            {
                if (img.Resource == handle.ID && img.View != VK_NULL_HANDLE)
                    return img.View;
            }
            return VK_NULL_HANDLE;
        };

        if (VkImageView sceneView = findView(sceneColorHandle))
            UpdateImageDescriptor(dev, m_Sets[frameIndex], 0, linearSampler, sceneView, readLayout);
    }

    void HistogramSubPass::Shutdown()
    {
        if (!m_Device) return;
        VkDevice dev = m_Device->GetLogicalDevice();

        m_Pipeline.reset();
        for (auto& buf : m_Buffers)
            buf.reset();

        if (m_SetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
    }

    void HistogramSubPass::OnResize()
    {
        m_Pipeline.reset();
    }
}
