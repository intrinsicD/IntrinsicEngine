module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.RetainedPointCloud.Impl;

import :Passes.RetainedPointCloud;
import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :Components;
import :ShaderRegistry;
import :GpuColor;
import Geometry;
import Core.Hash;
import Core.Logging;
import Core.Filesystem;
import ECS;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

    // Push constants layout (must match point_retained.vert / point_retained.frag).
    struct RetainedPointPushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset 0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrNormals;      //  8 bytes, offset 72
        float     PointSize;       //  4 bytes, offset 80
        float     SizeMultiplier;  //  4 bytes, offset 84
        float     ViewportWidth;   //  4 bytes, offset 88
        float     ViewportHeight;  //  4 bytes, offset 92
        uint32_t  RenderMode;      //  4 bytes, offset 96
        uint32_t  Color;           //  4 bytes, offset 100
    };
    static_assert(sizeof(RetainedPointPushConstants) == 104);

    // =========================================================================
    // Initialize
    // =========================================================================

    void RetainedPointCloudRenderPass::Initialize(RHI::VulkanDevice& device,
                                                  RHI::DescriptorAllocator& descriptorPool,
                                                  RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_DescriptorPool = &descriptorPool;
        m_GlobalSetLayout = globalLayout.GetHandle();
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void RetainedPointCloudRenderPass::Shutdown()
    {
        if (!m_Device) return;
        m_Pipeline.reset();
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> RetainedPointCloudRenderPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("RetainedPointCloudRenderPass: ShaderRegistry not configured.");
            return nullptr;
        }

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                       "RetainedPoint.Vert"_id,
                                                       "RetainedPoint.Frag"_id);

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

        // Set 0: global camera layout. No set 1 — all data via BDA push constants.
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(RetainedPointPushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("RetainedPointCloudRenderPass: Failed to build pipeline (VkResult={})", (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void RetainedPointCloudRenderPass::AddPasses(RenderPassContext& ctx)
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
                    Core::Log::Error("RetainedPointCloudRenderPass: pipeline creation failed. Retained point cloud will be skipped.");
                }
                return;
            }
        }

        // Collect entities that need retained vertex point rendering.
        auto& registry = ctx.Scene.GetRegistry();
        auto meshView = registry.view<ECS::MeshRenderer::Component,
                                      ECS::RenderVisualization::Component>();

        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint64_t PtrNormals;
            uint32_t VertexCount;
            float PointSize;
            uint32_t RenderMode;
            uint32_t Color;
        };

        std::vector<DrawInfo> draws;

        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowVertices)
                continue;

            // Need valid GPU geometry for BDA position access.
            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(mr.Geometry);
            if (!geo || !geo->GetVertexBuffer())
                continue;

            // Compute vertex count from position data size.
            const auto& layout = geo->GetLayout();
            if (layout.PositionsSize == 0)
                continue;

            const uint32_t vertexCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
            if (vertexCount == 0)
                continue;

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const uint64_t posAddr = baseAddr + layout.PositionsOffset;
            const uint64_t normAddr = (layout.NormalsSize > 0) ? (baseAddr + layout.NormalsOffset) : 0;

            const uint32_t vtxColor = GpuColor::PackColorF(
                vis.VertexColor.r, vis.VertexColor.g,
                vis.VertexColor.b, vis.VertexColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.PtrNormals = normAddr;
            di.VertexCount = vertexCount;
            di.PointSize = vis.VertexSize;
            di.RenderMode = static_cast<uint32_t>(vis.VertexRenderMode);
            di.Color = vtxColor;
            draws.push_back(di);
        }

        // -----------------------------------------------------------------
        // Graph entities — retained-mode node rendering via BDA.
        // Same pattern as mesh vertex visualization: read positions/normals
        // via BDA from the shared vertex buffer, render as billboard quads.
        // -----------------------------------------------------------------
        auto graphView = registry.view<ECS::Graph::Data>();
        for (auto [entity, graphData] : graphView.each())
        {
            if (!graphData.Visible || !graphData.GpuGeometry.IsValid())
                continue;

            if (graphData.GpuVertexCount == 0)
                continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(graphData.GpuGeometry);
            if (!geo || !geo->GetVertexBuffer())
                continue;

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const auto& layout = geo->GetLayout();
            const uint64_t posAddr = baseAddr + layout.PositionsOffset;
            const uint64_t normAddr = (layout.NormalsSize > 0) ? (baseAddr + layout.NormalsOffset) : 0;

            const uint32_t nodeColor = GpuColor::PackColorF(
                graphData.DefaultNodeColor.r, graphData.DefaultNodeColor.g,
                graphData.DefaultNodeColor.b, graphData.DefaultNodeColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.PtrNormals = normAddr;
            di.VertexCount = graphData.GpuVertexCount;
            di.PointSize = graphData.DefaultNodeRadius;
            di.RenderMode = static_cast<uint32_t>(graphData.NodeRenderMode);
            di.Color = nodeColor;
            draws.push_back(di);
        }

        if (draws.empty())
            return;

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
            return;

        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;
        const float sizeMultiplier = SizeMultiplier;

        auto capturedDraws = std::make_shared<std::vector<DrawInfo>>(std::move(draws));

        ctx.Graph.AddPass<PassData>("RetainedPoints",
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
            [this, globalSet, dynamicOffset, resolution, sizeMultiplier, capturedDraws]
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

                for (const auto& di : *capturedDraws)
                {
                    RetainedPointPushConstants push{};
                    push.Model = di.Model;
                    push.PtrPositions = di.PtrPositions;
                    push.PtrNormals = di.PtrNormals;
                    push.PointSize = di.PointSize;
                    push.SizeMultiplier = sizeMultiplier;
                    push.ViewportWidth = static_cast<float>(resolution.width);
                    push.ViewportHeight = static_cast<float>(resolution.height);
                    push.RenderMode = di.RenderMode;
                    push.Color = di.Color;

                    vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(push), &push);

                    // Each vertex becomes 6 GPU vertices (billboard quad).
                    vkCmdDraw(cmd, di.VertexCount * 6, 1, 0, 0);
                }
            });
    }

} // namespace Graphics::Passes
