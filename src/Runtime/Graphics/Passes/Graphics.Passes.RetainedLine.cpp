module;

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.RetainedLine.Impl;

import :Passes.RetainedLine;
import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :Components;
import :ShaderRegistry;
import :GpuColor;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import ECS;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

    // Push constants layout (must match line_retained.vert / line_retained.frag).
    struct RetainedLinePushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset 0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        float     LineWidth;       //  4 bytes, offset 72
        float     ViewportWidth;   //  4 bytes, offset 76
        float     ViewportHeight;  //  4 bytes, offset 80
        uint32_t  Color;           //  4 bytes, offset 84
    };
    static_assert(sizeof(RetainedLinePushConstants) == 88);

    // Use EdgePair from the component — identical layout, eliminates type mismatch.
    using EdgePair = ECS::RenderVisualization::EdgePair;

    // =========================================================================
    // Initialize
    // =========================================================================

    void RetainedLineRenderPass::Initialize(RHI::VulkanDevice& device,
                                            RHI::DescriptorAllocator& descriptorPool,
                                            RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_DescriptorPool = &descriptorPool;
        m_GlobalSetLayout = globalLayout.GetHandle();

        // Create descriptor set layout for edge SSBO (1 binding: SSBO at binding 0).
        m_EdgeSetLayout = CreateSSBODescriptorSetLayout(
            m_Device->GetLogicalDevice(), VK_SHADER_STAGE_VERTEX_BIT, "RetainedLineRenderPass");

        // Allocate per-frame descriptor sets.
        AllocatePerFrameSets<FRAMES>(descriptorPool, m_EdgeSetLayout, m_EdgeDescSets);
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void RetainedLineRenderPass::Shutdown()
    {
        if (!m_Device) return;

        for (auto& buf : m_EdgeBuffers) buf.reset();
        m_Pipeline.reset();

        if (m_EdgeSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_EdgeSetLayout, nullptr);
            m_EdgeSetLayout = VK_NULL_HANDLE;
        }
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> RetainedLineRenderPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("RetainedLineRenderPass: ShaderRegistry not configured.");
            return nullptr;
        }

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                       "RetainedLine.Vert"_id,
                                                       "RetainedLine.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        RHI::PipelineBuilder pb(MakeDeviceAlias(m_Device));
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.EnableAlphaBlending();
        pb.SetColorFormats({colorFormat});

        // Depth test enabled, depth write disabled (lines overlay geometry).
        pb.SetDepthFormat(depthFormat);
        pb.EnableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);

        // Set 0: global camera layout. Set 1: edge SSBO layout.
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);
        pb.AddDescriptorSetLayout(m_EdgeSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(RetainedLinePushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("RetainedLineRenderPass: Failed to build pipeline (VkResult={})", (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void RetainedLineRenderPass::AddPasses(RenderPassContext& ctx)
    {
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;
        if (!m_GeometryStorage) return;

        const uint32_t frameIndex = ctx.FrameIndex;

        // Lazy pipeline creation.
        if (!m_Pipeline)
        {
            m_Pipeline = BuildPipeline(ctx.SwapchainFormat, ctx.DepthFormat);
            if (!m_Pipeline)
            {
                static bool s_Logged = false;
                if (!s_Logged)
                {
                    s_Logged = true;
                    Core::Log::Error("RetainedLineRenderPass: pipeline creation failed. Retained wireframe will be skipped.");
                }
                return;
            }
        }

        // Collect all entities that need retained wireframe rendering.
        auto& registry = ctx.Scene.GetRegistry();
        auto meshView = registry.view<ECS::MeshRenderer::Component,
                                      ECS::RenderVisualization::Component>();

        // Accumulate all edge pairs for this frame + per-entity draw info.
        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint32_t EdgeCount;
            uint32_t EdgeOffset; // offset in edge pairs within the SSBO
            uint32_t Color;
            float    LineWidth;  // per-entity wireframe width (pixels)
        };

        std::vector<EdgePair> allEdges;
        std::vector<DrawInfo> draws;

        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowWireframe)
                continue;

            // Need valid GPU geometry for BDA position access.
            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(mr.Geometry);
            if (!geo || !geo->GetVertexBuffer())
                continue;

            // Need cached edges.
            if (vis.CachedEdges.empty())
                continue;

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const uint64_t posAddr = baseAddr + geo->GetLayout().PositionsOffset;

            const uint32_t wireColor = GpuColor::PackColorF(
                vis.WireframeColor.r, vis.WireframeColor.g,
                vis.WireframeColor.b, vis.WireframeColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.EdgeCount = static_cast<uint32_t>(vis.CachedEdges.size());
            di.EdgeOffset = static_cast<uint32_t>(allEdges.size());
            di.Color = wireColor;
            di.LineWidth = vis.WireframeWidth;
            draws.push_back(di);

            // CachedEdges uses the same EdgePair type — insert directly.
            allEdges.insert(allEdges.end(), vis.CachedEdges.begin(), vis.CachedEdges.end());
        }

        // -----------------------------------------------------------------
        // Graph entities — retained-mode edge rendering via BDA.
        // Same pattern as mesh wireframe: read positions via BDA from the
        // shared vertex buffer, upload edge pairs to the shared SSBO.
        // -----------------------------------------------------------------
        auto graphView = registry.view<ECS::Graph::Data>();
        for (auto [entity, graphData] : graphView.each())
        {
            if (!graphData.Visible || !graphData.GpuGeometry.IsValid())
                continue;

            if (graphData.CachedEdgePairs.empty())
                continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(graphData.GpuGeometry);
            if (!geo || !geo->GetVertexBuffer())
                continue;

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const uint64_t posAddr = baseAddr + geo->GetLayout().PositionsOffset;

            const uint32_t edgeColor = GpuColor::PackColorF(
                graphData.DefaultEdgeColor.r, graphData.DefaultEdgeColor.g,
                graphData.DefaultEdgeColor.b, graphData.DefaultEdgeColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.EdgeCount = static_cast<uint32_t>(graphData.CachedEdgePairs.size());
            di.EdgeOffset = static_cast<uint32_t>(allEdges.size());
            di.Color = edgeColor;
            di.LineWidth = graphData.EdgeWidth;
            draws.push_back(di);

            allEdges.insert(allEdges.end(),
                            graphData.CachedEdgePairs.begin(),
                            graphData.CachedEdgePairs.end());
        }

        if (draws.empty())
            return;

        // Ensure SSBO capacity.
        const uint32_t totalEdges = static_cast<uint32_t>(allEdges.size());
        if (!EnsurePerFrameBuffer<EdgePair, FRAMES>(
                *m_Device, m_EdgeBuffers, m_EdgeBufferCapacity, totalEdges, 256, "RetainedLineRenderPass"))
            return;

        // Upload edge data.
        m_EdgeBuffers[frameIndex]->Write(allEdges.data(), allEdges.size() * sizeof(EdgePair));
        UpdateSSBODescriptor(m_Device->GetLogicalDevice(), m_EdgeDescSets[frameIndex],
                             0, m_EdgeBuffers[frameIndex]->GetHandle(),
                             allEdges.size() * sizeof(EdgePair));

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
            return;

        // Capture values for the lambda.
        const auto capturedEdgeSet = m_EdgeDescSets[frameIndex];
        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;

        // Move draws into shared storage for lambda capture.
        auto capturedDraws = std::make_shared<std::vector<DrawInfo>>(std::move(draws));

        ctx.Graph.AddPass<PassData>("RetainedLines",
            [&](PassData& data, RGBuilder& builder)
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
            [this, capturedEdgeSet, globalSet, dynamicOffset, resolution, capturedDraws]
            (const PassData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                SetViewportScissor(cmd, resolution);

                // Bind set 0: global camera (with dynamic offset).
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_Pipeline->GetLayout(),
                                        0, 1, &globalSet,
                                        1, &dynamicOffset);

                // Bind set 1: edge SSBO (no dynamic offset).
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_Pipeline->GetLayout(),
                                        1, 1, &capturedEdgeSet,
                                        0, nullptr);

                for (const auto& di : *capturedDraws)
                {
                    RetainedLinePushConstants push{};
                    push.Model = di.Model;
                    push.PtrPositions = di.PtrPositions;
                    push.LineWidth = di.LineWidth;
                    push.ViewportWidth = static_cast<float>(resolution.width);
                    push.ViewportHeight = static_cast<float>(resolution.height);
                    push.Color = di.Color;

                    vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(push), &push);

                    // Each edge becomes 6 vertices (2 triangles).
                    // Use firstVertex to offset into the edge SSBO for this entity.
                    vkCmdDraw(cmd, di.EdgeCount * 6, 1, di.EdgeOffset * 6, 0);
                }
            });
    }

} // namespace Graphics::Passes
