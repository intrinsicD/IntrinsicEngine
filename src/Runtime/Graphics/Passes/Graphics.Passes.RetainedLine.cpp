module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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

    // Push constants layout (must match line_retained.vert / line_retained.frag).
    struct RetainedLinePushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset  0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrEdges;        //  8 bytes, offset 72
        float     LineWidth;       //  4 bytes, offset 80
        float     ViewportWidth;   //  4 bytes, offset 84
        float     ViewportHeight;  //  4 bytes, offset 88
        uint32_t  Color;           //  4 bytes, offset 92
    };
    static_assert(sizeof(RetainedLinePushConstants) == 96);

    // Use EdgePair from the component — identical layout, eliminates type mismatch.
    using EdgePair = ECS::RenderVisualization::EdgePair;

    // =========================================================================
    // EnsureEdgeBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable edge buffer for an entity.
    // Returns the buffer device address, or 0 on failure.

    uint64_t RetainedLineRenderPass::EnsureEdgeBuffer(
        uint32_t entityKey,
        const void* edgeData,
        uint32_t edgeCount,
        uint32_t sourceGeoIdx)
    {
        auto it = m_EdgeBuffers.find(entityKey);

        // Buffer exists, edge count matches, and source geometry hasn't changed — reuse.
        if (it != m_EdgeBuffers.end()
            && it->second.EdgeCount == edgeCount
            && it->second.SourceGeometryIndex == sourceGeoIdx)
        {
            return it->second.Buffer->GetDeviceAddress();
        }

        // Need to create or recreate (count changed or source geometry was re-uploaded).
        if (it != m_EdgeBuffers.end() && it->second.Buffer)
        {
            // Deferred-destroy old buffer — may still be referenced by in-flight frames.
            m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
            it->second.EdgeCount = 0;
        }

        const VkDeviceSize size = static_cast<VkDeviceSize>(edgeCount) * sizeof(EdgePair);
        auto buf = std::make_unique<RHI::VulkanBuffer>(
            *m_Device, size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        if (!buf->GetMappedData())
        {
            Core::Log::Error("RetainedLineRenderPass: Failed to allocate edge buffer ({} bytes)", size);
            return 0;
        }

        buf->Write(edgeData, static_cast<size_t>(size));
        const uint64_t addr = buf->GetDeviceAddress();

        m_EdgeBuffers[entityKey] = { std::move(buf), edgeCount, sourceGeoIdx };
        return addr;
    }

    // =========================================================================
    // Initialize
    // =========================================================================

    void RetainedLineRenderPass::Initialize(RHI::VulkanDevice& device,
                                            RHI::DescriptorAllocator&,
                                            RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_GlobalSetLayout = globalLayout.GetHandle();
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void RetainedLineRenderPass::Shutdown()
    {
        if (!m_Device) return;

        // Deferred-destroy all persistent edge buffers.
        for (auto& [key, entry] : m_EdgeBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_EdgeBuffers.clear();

        m_Pipeline.reset();
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

        // Set 0: global camera layout (only descriptor set — edges use BDA).
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);

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

        auto& registry = ctx.Scene.GetRegistry();

        // -----------------------------------------------------------------
        // Frustum extraction — CPU-side culling for retained-mode draws.
        // Uses the same plane-vs-sphere test as instance_cull.comp on the GPU.
        // -----------------------------------------------------------------
        const bool cullingEnabled = !ctx.Debug.DisableCulling;
        const Geometry::Frustum frustum = cullingEnabled
            ? Geometry::Frustum::CreateFromMatrix(ctx.CameraProj * ctx.CameraView)
            : Geometry::Frustum{};

        // Per-entity draw command — each entity has its own persistent edge buffer.
        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint64_t PtrEdges;
            uint32_t EdgeCount;
            uint32_t Color;
            float    LineWidth;
        };

        std::vector<DrawInfo> draws;

        // Track which entity keys are active this frame (for orphan cleanup).
        std::vector<uint32_t> activeKeys;

        // -----------------------------------------------------------------
        // Mesh entities — persistent edge buffers from CachedEdges.
        // -----------------------------------------------------------------
        auto meshView = registry.view<ECS::MeshRenderer::Component,
                                      ECS::RenderVisualization::Component>();

        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowWireframe)
                continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(mr.Geometry);
            if (!geo || !geo->GetVertexBuffer())
                continue;

            if (vis.CachedEdges.empty())
                continue;

            const uint32_t entityKey = static_cast<uint32_t>(entity);
            activeKeys.push_back(entityKey);

            // Frustum cull: skip draw if the entity's bounding sphere is outside the camera frustum.
            // Edge buffers are still maintained (activeKeys tracks them) — only the draw is skipped.
            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                continue;

            // Ensure persistent edge buffer exists (create on first frame, reuse after).
            // Mesh geometry handle is stable — edges only change when EdgeCacheDirty is set.
            const uint32_t edgeCount = static_cast<uint32_t>(vis.CachedEdges.size());
            const uint64_t edgeAddr = EnsureEdgeBuffer(
                entityKey, vis.CachedEdges.data(), edgeCount, mr.Geometry.Index);
            if (edgeAddr == 0)
                continue;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const uint64_t posAddr = baseAddr + geo->GetLayout().PositionsOffset;

            const uint32_t wireColor = GpuColor::PackColorF(
                vis.WireframeColor.r, vis.WireframeColor.g,
                vis.WireframeColor.b, vis.WireframeColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.PtrEdges = edgeAddr;
            di.EdgeCount = edgeCount;
            di.Color = wireColor;
            di.LineWidth = vis.WireframeWidth;
            draws.push_back(di);

            // Update wireframe edge count on the GeometryViewRenderer component.
            if (auto* vr = registry.try_get<ECS::GeometryViewRenderer::Component>(entity))
                vr->WireframeEdgeCount = edgeCount;
        }

        // -----------------------------------------------------------------
        // Graph entities — persistent edge buffers from CachedEdgePairs.
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

            // Use a separate key namespace for graphs to avoid collision with mesh entity IDs.
            // Offset by a large constant (entt entities are typically small uint32_t values).
            const uint32_t entityKey = static_cast<uint32_t>(entity) | 0x80000000u;
            activeKeys.push_back(entityKey);

            glm::mat4 worldMatrix(1.0f);
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                worldMatrix = wm->Matrix;

            // Frustum cull: skip draw if graph geometry is outside the camera frustum.
            if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                continue;

            // Graph edges may change when layout algorithms run (GpuDirty resets CachedEdgePairs).
            // EnsureEdgeBuffer detects geometry handle changes (re-uploaded by GraphGeometrySyncSystem)
            // and recreates the buffer even if the edge count stays the same.
            const uint32_t edgeCount = static_cast<uint32_t>(graphData.CachedEdgePairs.size());
            const uint64_t edgeAddr = EnsureEdgeBuffer(
                entityKey, graphData.CachedEdgePairs.data(), edgeCount, graphData.GpuGeometry.Index);
            if (edgeAddr == 0)
                continue;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const uint64_t posAddr = baseAddr + geo->GetLayout().PositionsOffset;

            const uint32_t edgeColor = GpuColor::PackColorF(
                graphData.DefaultEdgeColor.r, graphData.DefaultEdgeColor.g,
                graphData.DefaultEdgeColor.b, graphData.DefaultEdgeColor.a);

            DrawInfo di{};
            di.Model = worldMatrix;
            di.PtrPositions = posAddr;
            di.PtrEdges = edgeAddr;
            di.EdgeCount = edgeCount;
            di.Color = edgeColor;
            di.LineWidth = graphData.EdgeWidth;
            draws.push_back(di);
        }

        // -----------------------------------------------------------------
        // Cleanup orphaned edge buffers (entities destroyed or wireframe disabled).
        // -----------------------------------------------------------------
        if (!m_EdgeBuffers.empty())
        {
            // Sort active keys for fast lookup.
            std::sort(activeKeys.begin(), activeKeys.end());

            for (auto it = m_EdgeBuffers.begin(); it != m_EdgeBuffers.end(); )
            {
                if (!std::binary_search(activeKeys.begin(), activeKeys.end(), it->first))
                {
                    if (it->second.Buffer)
                        m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
                    it = m_EdgeBuffers.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (draws.empty())
            return;

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
            return;

        // Capture values for the lambda.
        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;

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
            [this, globalSet, dynamicOffset, resolution, capturedDraws]
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
                    RetainedLinePushConstants push{};
                    push.Model = di.Model;
                    push.PtrPositions = di.PtrPositions;
                    push.PtrEdges = di.PtrEdges;
                    push.LineWidth = di.LineWidth;
                    push.ViewportWidth = static_cast<float>(resolution.width);
                    push.ViewportHeight = static_cast<float>(resolution.height);
                    push.Color = di.Color;

                    vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(push), &push);

                    // Each edge becomes 6 vertices (2 triangles).
                    // firstVertex=0 — each entity has its own edge buffer starting at offset 0.
                    vkCmdDraw(cmd, di.EdgeCount * 6, 1, 0, 0);
                }
            });
    }

} // namespace Graphics::Passes
