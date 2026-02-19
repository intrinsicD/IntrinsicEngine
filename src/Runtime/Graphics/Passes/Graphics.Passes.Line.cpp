module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.Line.Impl;

import :Passes.Line;
import :RenderPipeline;
import :RenderGraph;
import :DebugDraw;
import :ShaderRegistry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

    // Push constants layout (must match line.vert/line.frag).
    struct LinePushConstants
    {
        float LineWidth;
        float ViewportWidth;
        float ViewportHeight;
        float _pad;
    };
    static_assert(sizeof(LinePushConstants) == 16);

    // =========================================================================
    // Initialize
    // =========================================================================

    void LineRenderPass::Initialize(RHI::VulkanDevice& device,
                                   RHI::DescriptorAllocator& descriptorPool,
                                   RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_DescriptorPool = &descriptorPool;
        m_GlobalSetLayout = globalLayout.GetHandle();

        // Create descriptor set layout for the line SSBO (1 binding: SSBO at binding 0).
        m_LineSetLayout = CreateSSBODescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_VERTEX_BIT, "LineRenderPass");

        // Allocate per-frame descriptor sets for depth-tested and overlay passes.
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            m_DepthLineSet[i] = descriptorPool.Allocate(m_LineSetLayout);
            m_OverlayLineSet[i] = descriptorPool.Allocate(m_LineSetLayout);
        }
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void LineRenderPass::Shutdown()
    {
        if (!m_Device) return;

        // Destroy SSBOs first (they reference the device).
        for (auto& buf : m_DepthLineBuffer) buf.reset();
        for (auto& buf : m_OverlayLineBuffer) buf.reset();

        m_DepthPipeline.reset();
        m_OverlayPipeline.reset();

        if (m_LineSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_LineSetLayout, nullptr);
            m_LineSetLayout = VK_NULL_HANDLE;
        }
    }

    // =========================================================================
    // EnsureBuffer — grow SSBO capacity if needed
    // =========================================================================

    bool LineRenderPass::EnsureBuffer(std::unique_ptr<RHI::VulkanBuffer> buffers[FRAMES],
                                     uint32_t& capacity, uint32_t requiredSegments)
    {
        return EnsurePerFrameBuffer<DebugDraw::LineSegment, FRAMES>(
            *m_Device, buffers, capacity, requiredSegments, 256, "LineRenderPass");
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> LineRenderPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat, bool enableDepthTest)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("LineRenderPass: ShaderRegistry not configured.");
            return nullptr;
        }

        const std::string vertPath = Core::Filesystem::ResolveShaderPathOrExit(
            [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
            "Line.Vert"_id);
        const std::string fragPath = Core::Filesystem::ResolveShaderPathOrExit(
            [&](Core::Hash::StringID id) { return m_ShaderRegistry->Get(id); },
            "Line.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        std::shared_ptr<RHI::VulkanDevice> deviceAlias(m_Device, [](RHI::VulkanDevice*) {});
        RHI::PipelineBuilder pb(deviceAlias);
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.EnableAlphaBlending();
        pb.SetColorFormats({colorFormat});

        if (enableDepthTest)
        {
            pb.SetDepthFormat(depthFormat);
            // Depth test enabled, depth write disabled (lines overlay geometry).
            pb.EnableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
        }
        else
        {
            pb.DisableDepthTest();
        }

        // Set 0: global camera layout. Set 1: line SSBO layout.
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);
        pb.AddDescriptorSetLayout(m_LineSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(LinePushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("LineRenderPass: Failed to build pipeline (VkResult={})", (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // RecordDraw
    // =========================================================================

    void LineRenderPass::RecordDraw(VkCommandBuffer cmd, RHI::GraphicsPipeline* pipeline,
                                   VkDescriptorSet lineSet, VkDescriptorSet globalSet,
                                   uint32_t dynamicOffset, VkExtent2D extent,
                                   uint32_t lineCount)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());
        vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        SetViewportScissor(cmd, extent);

        // Bind set 0: global camera (with dynamic offset).
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->GetLayout(),
                                0, 1, &globalSet,
                                1, &dynamicOffset);

        // Bind set 1: line SSBO (no dynamic offset).
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeline->GetLayout(),
                                1, 1, &lineSet,
                                0, nullptr);

        // Push constants
        LinePushConstants push{};
        push.LineWidth = LineWidth;
        push.ViewportWidth = static_cast<float>(extent.width);
        push.ViewportHeight = static_cast<float>(extent.height);
        push._pad = 0.0f;

        vkCmdPushConstants(cmd, pipeline->GetLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        // Draw: 6 vertices per line segment (2 triangles).
        vkCmdDraw(cmd, lineCount * 6, 1, 0, 0);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void LineRenderPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_DebugDraw || !m_DebugDraw->HasContent()) return;
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;

        const uint32_t depthLineCount = m_DebugDraw->GetLineCount();
        const uint32_t overlayLineCount = m_DebugDraw->GetOverlayLineCount();

        if (depthLineCount == 0 && overlayLineCount == 0) return;

        const uint32_t frameIndex = ctx.FrameIndex;

        // Lazy pipeline creation (need swapchain format and depth format).
        if (!m_DepthPipeline || !m_OverlayPipeline)
        {
            VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // standard depth format
            m_DepthPipeline = BuildPipeline(ctx.SwapchainFormat, depthFormat, true);
            m_OverlayPipeline = BuildPipeline(ctx.SwapchainFormat, depthFormat, false);

            if (!m_DepthPipeline || !m_OverlayPipeline)
            {
                static bool s_Logged = false;
                if (!s_Logged)
                {
                    s_Logged = true;
                    Core::Log::Error("LineRenderPass: pipeline creation failed (missing/invalid line shaders?). DebugDraw will be skipped.");
                }
                return;
            }
        }

        // Upload depth-tested lines.
        if (depthLineCount > 0)
        {
            if (!EnsureBuffer(m_DepthLineBuffer, m_DepthLineBufferCapacity, depthLineCount))
                return;

            auto lines = m_DebugDraw->GetLines();
            m_DepthLineBuffer[frameIndex]->Write(lines.data(), lines.size_bytes());
            UpdateSSBODescriptor(m_Device->GetLogicalDevice(), m_DepthLineSet[frameIndex],
                                 0, m_DepthLineBuffer[frameIndex]->GetHandle(), lines.size_bytes());
        }

        // Upload overlay lines.
        if (overlayLineCount > 0)
        {
            if (!EnsureBuffer(m_OverlayLineBuffer, m_OverlayLineBufferCapacity, overlayLineCount))
                return;

            auto lines = m_DebugDraw->GetOverlayLines();
            m_OverlayLineBuffer[frameIndex]->Write(lines.data(), lines.size_bytes());
            UpdateSSBODescriptor(m_Device->GetLogicalDevice(), m_OverlayLineSet[frameIndex],
                                 0, m_OverlayLineBuffer[frameIndex]->GetHandle(), lines.size_bytes());
        }

        // Fetch resource handles from blackboard.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid()) return;

        // ----------------------------------------------------------------
        // Pass 1: Depth-tested lines
        // ----------------------------------------------------------------
        if (depthLineCount > 0 && depth.IsValid())
        {
            ctx.Graph.AddPass<LinePassData>("DebugLines_Depth",
                [&](LinePassData& data, RGBuilder& builder)
                {
                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Color = builder.WriteColor(backbuffer, colorInfo);

                    // Read depth for depth testing (don't write — overlay only).
                    RGAttachmentInfo depthInfo{};
                    depthInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    depthInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Depth = builder.WriteDepth(depth, depthInfo);
                },
                [this, depthLineCount, frameIndex,
                 pipeline = m_DepthPipeline.get(),
                 globalSet = ctx.GlobalDescriptorSet,
                 dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset),
                 extent = ctx.Resolution]
                (const LinePassData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    if (!pipeline) return;

                    RecordDraw(cmd, pipeline,
                               m_DepthLineSet[frameIndex], globalSet,
                               dynamicOffset, extent, depthLineCount);
                });
        }

        // ----------------------------------------------------------------
        // Pass 2: Overlay lines (no depth test — always on top)
        // ----------------------------------------------------------------
        if (overlayLineCount > 0)
        {
            ctx.Graph.AddPass<LinePassData>("DebugLines_Overlay",
                [&](LinePassData& data, RGBuilder& builder)
                {
                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Color = builder.WriteColor(backbuffer, colorInfo);
                    data.Depth = {}; // No depth attachment.
                },
                [this, overlayLineCount, frameIndex,
                 pipeline = m_OverlayPipeline.get(),
                 globalSet = ctx.GlobalDescriptorSet,
                 dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset),
                 extent = ctx.Resolution]
                (const LinePassData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    if (!pipeline) return;

                    RecordDraw(cmd, pipeline,
                               m_OverlayLineSet[frameIndex], globalSet,
                               dynamicOffset, extent, overlayLineCount);
                });
        }
    }
}
