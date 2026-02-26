module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

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

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

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
        SubmitPoints(RenderMode, data, count);
    }

    void PointCloudRenderPass::SubmitPoints(Geometry::PointCloud::RenderMode mode, const GpuPointData* data, uint32_t count)
    {
        if (!data || count == 0)
            return;

        if (mode != Geometry::PointCloud::RenderMode::FlatDisc)
            return;

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
        m_PointSetLayout = CreateSSBODescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_VERTEX_BIT, "PointCloudRenderPass");

        // Allocate per-frame descriptor sets (legacy path).
        AllocatePerFrameSets<FRAMES>(descriptorPool, m_PointSetLayout, m_PointDescSets);

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
        return EnsurePerFrameBuffer<GpuPointData, FRAMES>(
            *m_Device, m_PointBuffers, m_BufferCapacity, requiredPoints, 1024, "PointCloudRenderPass");
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

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                      "PointCloud.Vert"_id,
                                                      "PointCloud.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        RHI::PipelineBuilder pb(MakeDeviceAlias(m_Device));
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
                                          uint32_t pointCount,
                                          Geometry::PointCloud::RenderMode mode)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        SetViewportScissor(cmd, extent);

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

        // Push constants — use the explicit mode, not the member variable,
        // because this runs deferred during render graph execution.
        PointCloudPushConstants push{};
        push.SizeMultiplier = SizeMultiplier;
        push.ViewportWidth = static_cast<float>(extent.width);
        push.ViewportHeight = static_cast<float>(extent.height);
        push.RenderMode = static_cast<uint32_t>(mode);

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
        if (!HasContent()) return;
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;


        const uint32_t frameIndex = ctx.FrameIndex;

        // Lazy pipeline creation (need swapchain format and depth format).
        if (!m_Pipeline)
        {
            const VkFormat depthFormat = ctx.DepthFormat;
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

        // Helper: record one point draw batch.
        auto recordBatch = [&](std::span<const GpuPointData> pts)
        {
            if (pts.empty())
                return;

            // Ensure SSBO capacity for this mode-batch.
            const uint32_t batchCount = static_cast<uint32_t>(pts.size());


            if (!EnsureBuffer(batchCount))
                return;

            m_PointBuffers[frameIndex]->Write(pts.data(), pts.size_bytes());
            VkDescriptorSet batchDescSet = m_PointDescSets[frameIndex];
            UpdateSSBODescriptor(m_Device->GetLogicalDevice(), batchDescSet,
                                 0, m_PointBuffers[frameIndex]->GetHandle(), pts.size_bytes());

            // Fetch resource handles.
            const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
            const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
            if (!backbuffer.IsValid() || !depth.IsValid())
                return;

            // Capture mode and descriptor set by value — the execute lambda runs
            // deferred during render graph execution, so reading from member
            // variables at that point would see stale/wrong values.
            const uint32_t localCount = batchCount;
            const auto capturedDescSet = batchDescSet;

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
                [this, &ctx, localCount, capturedDescSet](const PointCloudPassData&, const RGRegistry&, VkCommandBuffer cmd)

                {
                    const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                    RecordDraw(cmd,
                               capturedDescSet,
                               ctx.GlobalDescriptorSet,
                               dynamicOffset,
                               ctx.Resolution,
                               localCount,
                               Geometry::PointCloud::RenderMode::FlatDisc);
                });
        };

        if (!m_StagingPoints.empty())
            recordBatch(std::span<const GpuPointData>(m_StagingPoints.data(), m_StagingPoints.size()));
    }
}
