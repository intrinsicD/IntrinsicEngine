module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.PointCloud.Impl;

import :Passes.PointCloud;
import :RenderPipeline;
import :RenderGraph;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;

using namespace Core::Hash;

namespace Graphics::Passes
{
    static void CheckVk(VkResult r, const char* what)
    {
        if (r != VK_SUCCESS)
            Core::Log::Error("PointCloudRenderPass: {} failed (VkResult={})", what, (int)r);
    }

    // Push constants layout (must match point.vert/point.frag).
    struct PointCloudPushConstants
    {
        float SizeMultiplier;
        float ViewportWidth;
        float ViewportHeight;
        uint32_t RenderMode;
    };
    static_assert(sizeof(PointCloudPushConstants) == 16);

    // =========================================================================
    // PackPoint
    // =========================================================================

    PointCloudRenderPass::GpuPointData PointCloudRenderPass::PackPoint(
        float x, float y, float z,
        float nx, float ny, float nz,
        float size, uint32_t color)
    {
        return GpuPointData{x, y, z, size, nx, ny, nz, color};
    }

    // =========================================================================
    // SubmitPoints
    // =========================================================================

    void PointCloudRenderPass::SubmitPoints(const GpuPointData* data, uint32_t count)
    {
        m_StagingPoints.insert(m_StagingPoints.end(), data, data + count);
    }

    // =========================================================================
    // Initialize
    // =========================================================================

    void PointCloudRenderPass::Initialize(RHI::VulkanDevice& device,
                                          RHI::DescriptorAllocator& descriptorPool,
                                          RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_DescriptorPool = &descriptorPool;
        m_GlobalSetLayout = globalLayout.GetHandle();

        // Create descriptor set layout for point cloud SSBO (1 binding: SSBO at binding 0).
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        CheckVk(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_PointSetLayout),
                "vkCreateDescriptorSetLayout (point cloud SSBO)");

        // Allocate per-frame descriptor sets.
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            m_PointDescSets[i] = descriptorPool.Allocate(m_PointSetLayout);
        }
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void PointCloudRenderPass::Shutdown()
    {
        if (!m_Device) return;

        for (auto& buf : m_PointBuffers) buf.reset();
        m_Pipeline.reset();

        if (m_PointSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_PointSetLayout, nullptr);
            m_PointSetLayout = VK_NULL_HANDLE;
        }
    }

    // =========================================================================
    // EnsureBuffer — grow SSBO capacity if needed
    // =========================================================================

    bool PointCloudRenderPass::EnsureBuffer(uint32_t requiredPoints)
    {
        if (requiredPoints <= m_BufferCapacity && m_PointBuffers[0] != nullptr)
            return true;

        // Grow with headroom: next power of 2, minimum 1024 points.
        uint32_t newCapacity = 1024;
        while (newCapacity < requiredPoints)
            newCapacity *= 2;

        const size_t byteSize = static_cast<size_t>(newCapacity) * sizeof(GpuPointData);

        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            m_PointBuffers[i] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                byteSize,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            if (!m_PointBuffers[i]->GetMappedData())
            {
                Core::Log::Error("PointCloudRenderPass: Failed to allocate point SSBO ({} bytes)", byteSize);
                return false;
            }
        }

        m_BufferCapacity = newCapacity;
        return true;
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> PointCloudRenderPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("PointCloudRenderPass: ShaderRegistry not configured.");
            return nullptr;
        }

        const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
            [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
            "PointCloud.Vert"_id);
        const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
            [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
            "PointCloud.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        std::shared_ptr<RHI::VulkanDevice> deviceAlias(m_Device, [](RHI::VulkanDevice*) {});
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.EnableAlphaBlending();
        pb.SetColorFormats({colorFormat});

        // Depth test enabled, depth write enabled (points occlude each other).
        pb.SetDepthFormat(depthFormat);
        pb.EnableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

        // Set 0: global camera layout. Set 1: point cloud SSBO layout.
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);
        pb.AddDescriptorSetLayout(m_PointSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(PointCloudPushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("PointCloudRenderPass: Failed to build pipeline (VkResult={})", (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // RecordDraw
    // =========================================================================

    void PointCloudRenderPass::RecordDraw(VkCommandBuffer cmd,
                                          VkDescriptorSet pointSet,
                                          VkDescriptorSet globalSet,
                                          uint32_t dynamicOffset,
                                          VkExtent2D extent,
                                          uint32_t pointCount)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());

        vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

        VkViewport vp{};
        vp.x = 0.0f;
        vp.y = 0.0f;
        vp.width = static_cast<float>(extent.width);
        vp.height = static_cast<float>(extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        VkRect2D sc{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);

        // Bind set 0: global camera (with dynamic offset).
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(),
                                0, 1, &globalSet,
                                1, &dynamicOffset);

        // Bind set 1: point cloud SSBO (no dynamic offset).
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_Pipeline->GetLayout(),
                                1, 1, &pointSet,
                                0, nullptr);

        // Push constants.
        PointCloudPushConstants push{};
        push.SizeMultiplier = SizeMultiplier;
        push.ViewportWidth = static_cast<float>(extent.width);
        push.ViewportHeight = static_cast<float>(extent.height);
        push.RenderMode = static_cast<uint32_t>(RenderMode);

        vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        // Draw: 6 vertices per point (2 triangles forming a billboard quad).
        vkCmdDraw(cmd, pointCount * 6, 1, 0, 0);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void PointCloudRenderPass::AddPasses(RenderPassContext& ctx)
    {
        if (m_StagingPoints.empty()) return;
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;

        const uint32_t pointCount = static_cast<uint32_t>(m_StagingPoints.size());
        const uint32_t frameIndex = ctx.FrameIndex;

        // Lazy pipeline creation (need swapchain format and depth format).
        if (!m_Pipeline)
        {
            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
            m_Pipeline = BuildPipeline(ctx.SwapchainFormat, depthFormat);

            if (!m_Pipeline)
            {
                static bool s_Logged = false;
                if (!s_Logged)
                {
                    s_Logged = true;
                    Core::Log::Error("PointCloudRenderPass: pipeline creation failed. Point clouds will be skipped.");
                }
                return;
            }
        }

        // Ensure SSBO capacity.
        if (!EnsureBuffer(pointCount))
            return;

        // Upload point data to SSBO.
        m_PointBuffers[frameIndex]->Write(m_StagingPoints.data(),
                                           m_StagingPoints.size() * sizeof(GpuPointData));

        // Update descriptor set.
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_PointBuffers[frameIndex]->GetHandle();
        bufInfo.offset = 0;
        bufInfo.range = m_StagingPoints.size() * sizeof(GpuPointData);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_PointDescSets[frameIndex];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &write, 0, nullptr);

        // Fetch resource handles from blackboard.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid()) return;

        // Add render graph pass.
        ctx.Graph.AddPass<PointCloudPassData>("PointCloud",
            [&](PointCloudPassData& data, RGBuilder& builder)
            {
                RGAttachmentInfo colorInfo{};
                colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Color = builder.WriteColor(backbuffer, colorInfo);

                RGAttachmentInfo depthInfo{};
                depthInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                depthInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                data.Depth = builder.WriteDepth(depth, depthInfo);
            },
            [this, pointCount, frameIndex,
             globalSet = ctx.GlobalDescriptorSet,
             dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset),
             extent = ctx.Resolution]
            (const PointCloudPassData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                if (!m_Pipeline) return;

                RecordDraw(cmd, m_PointDescSets[frameIndex], globalSet,
                           dynamicOffset, extent, pointCount);
            });
    }
}
