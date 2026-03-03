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

module Graphics:Passes.Line.Impl;

import :Passes.Line;
import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :Components;
import :DebugDraw;
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

    // Push constants layout (must match line.vert / line.frag).
    struct LinePushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset  0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrEdges;        //  8 bytes, offset 72
        float     LineWidth;       //  4 bytes, offset 80
        float     ViewportWidth;   //  4 bytes, offset 84
        float     ViewportHeight;  //  4 bytes, offset 88
        uint32_t  Color;           //  4 bytes, offset 92
        uint64_t  PtrEdgeAux;      //  8 bytes, offset 96 — BDA to per-edge packed ABGR colors (0 = uniform Color)
    };
    static_assert(sizeof(LinePushConstants) == 104);

    // Use EdgePair from the component — identical layout, eliminates type mismatch.
    using EdgePair = ECS::RenderVisualization::EdgePair;

    // =========================================================================
    // EnsureEdgeAuxBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable per-edge attribute buffer.
    // Data: array of packed ABGR uint32_t, one per edge.
    // Returns the buffer device address, or 0 on failure.

    uint64_t LinePass::EnsureEdgeAuxBuffer(
        uint32_t entityKey,
        const uint32_t* colorData,
        uint32_t edgeCount)
    {
        auto it = m_EdgeAuxBuffers.find(entityKey);

        // Buffer exists and edge count matches — update data in-place.
        if (it != m_EdgeAuxBuffers.end() && it->second.EdgeCount == edgeCount && it->second.Buffer)
        {
            it->second.Buffer->Write(colorData, static_cast<size_t>(edgeCount) * sizeof(uint32_t));
            return it->second.Buffer->GetDeviceAddress();
        }

        // Need to create or recreate (count changed).
        if (it != m_EdgeAuxBuffers.end() && it->second.Buffer)
        {
            m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
            it->second.EdgeCount = 0;
        }

        const VkDeviceSize size = static_cast<VkDeviceSize>(edgeCount) * sizeof(uint32_t);
        auto buf = std::make_unique<RHI::VulkanBuffer>(
            *m_Device, size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        if (!buf->GetMappedData())
        {
            Core::Log::Error("LinePass: Failed to allocate edge aux buffer ({} bytes)", size);
            return 0;
        }

        buf->Write(colorData, static_cast<size_t>(size));
        const uint64_t addr = buf->GetDeviceAddress();

        m_EdgeAuxBuffers[entityKey] = { std::move(buf), edgeCount };
        return addr;
    }

    // =========================================================================
    // EnsureTransientBuffers
    // =========================================================================
    // Ensures per-frame transient buffers (positions, identity edges, colors)
    // have enough capacity for the given segment count.

    bool LinePass::EnsureTransientBuffers(uint32_t segmentCount, uint32_t frameIndex)
    {
        if (segmentCount == 0)
            return true;

        const uint32_t vertexCount = segmentCount * 2; // 2 vec3 per segment

        // --- Position buffer (per-frame) ---
        if (vertexCount > m_TransientPosCapacity)
        {
            // Power-of-2 growth.
            uint32_t newCap = std::max(256u, m_TransientPosCapacity);
            while (newCap < vertexCount) newCap *= 2;

            const VkDeviceSize size = static_cast<VkDeviceSize>(newCap) * sizeof(glm::vec3);
            for (uint32_t i = 0; i < FRAMES; ++i)
            {
                if (m_TransientPosBuffer[i])
                    m_Device->SafeDestroy([old = std::move(m_TransientPosBuffer[i])]() {});

                m_TransientPosBuffer[i] = std::make_unique<RHI::VulkanBuffer>(
                    *m_Device, size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!m_TransientPosBuffer[i]->GetMappedData())
                {
                    Core::Log::Error("LinePass: Failed to allocate transient position buffer ({} bytes)", size);
                    return false;
                }
            }
            m_TransientPosCapacity = newCap;
        }

        // --- Identity edge pair buffer (shared across frames — data is constant) ---
        if (segmentCount > m_TransientEdgeCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientEdgeCapacity);
            while (newCap < segmentCount) newCap *= 2;

            if (m_TransientEdgeBuffer)
                m_Device->SafeDestroy([old = std::move(m_TransientEdgeBuffer)]() {});

            const VkDeviceSize size = static_cast<VkDeviceSize>(newCap) * sizeof(EdgePair);
            m_TransientEdgeBuffer = std::make_unique<RHI::VulkanBuffer>(
                *m_Device, size,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);

            if (!m_TransientEdgeBuffer->GetMappedData())
            {
                Core::Log::Error("LinePass: Failed to allocate transient edge buffer ({} bytes)", size);
                return false;
            }

            // Fill with identity edge pairs: {0,1}, {2,3}, {4,5}, ...
            std::vector<EdgePair> identityEdges(newCap);
            for (uint32_t i = 0; i < newCap; ++i)
                identityEdges[i] = { i * 2, i * 2 + 1 };
            m_TransientEdgeBuffer->Write(identityEdges.data(), newCap * sizeof(EdgePair));

            m_TransientEdgeCapacity = newCap;
        }

        // --- Per-edge color buffer (per-frame) ---
        if (segmentCount > m_TransientColorCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientColorCapacity);
            while (newCap < segmentCount) newCap *= 2;

            const VkDeviceSize size = static_cast<VkDeviceSize>(newCap) * sizeof(uint32_t);
            for (uint32_t i = 0; i < FRAMES; ++i)
            {
                if (m_TransientColorBuffer[i])
                    m_Device->SafeDestroy([old = std::move(m_TransientColorBuffer[i])]() {});

                m_TransientColorBuffer[i] = std::make_unique<RHI::VulkanBuffer>(
                    *m_Device, size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!m_TransientColorBuffer[i]->GetMappedData())
                {
                    Core::Log::Error("LinePass: Failed to allocate transient color buffer ({} bytes)", size);
                    return false;
                }
            }
            m_TransientColorCapacity = newCap;
        }

        return true;
    }

    // =========================================================================
    // Initialize
    // =========================================================================

    void LinePass::Initialize(RHI::VulkanDevice& device,
                              RHI::DescriptorAllocator&,
                              RHI::DescriptorLayout& globalLayout)
    {
        m_Device = &device;
        m_GlobalSetLayout = globalLayout.GetHandle();
    }

    // =========================================================================
    // Shutdown
    // =========================================================================

    void LinePass::Shutdown()
    {
        if (!m_Device) return;

        // Deferred-destroy all persistent edge attribute buffers.
        for (auto& [key, entry] : m_EdgeAuxBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_EdgeAuxBuffers.clear();

        // Destroy transient buffers.
        for (auto& buf : m_TransientPosBuffer) buf.reset();
        for (auto& buf : m_TransientColorBuffer) buf.reset();
        m_TransientEdgeBuffer.reset();

        m_Pipeline.reset();
        m_OverlayPipeline.reset();
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> LinePass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat, bool enableDepthTest)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("LinePass: ShaderRegistry not configured.");
            return nullptr;
        }

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                       "Line.Vert"_id,
                                                       "Line.Frag"_id);

        RHI::ShaderModule vert(*m_Device, vertPath, RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, fragPath, RHI::ShaderStage::Fragment);

        RHI::PipelineBuilder pb(MakeDeviceAlias(m_Device));
        pb.SetShaders(&vert, &frag);
        pb.SetTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pb.SetCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        pb.EnableAlphaBlending();
        pb.SetColorFormats({colorFormat});

        if (enableDepthTest)
        {
            // Depth test enabled, depth write disabled (lines overlay geometry).
            pb.SetDepthFormat(depthFormat);
            pb.EnableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
        }
        else
        {
            pb.DisableDepthTest();
        }

        // Set 0: global camera layout (only descriptor set — edges use BDA).
        pb.AddDescriptorSetLayout(m_GlobalSetLayout);

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(LinePushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("LinePass: Failed to build pipeline (VkResult={})", (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void LinePass::AddPasses(RenderPassContext& ctx)
    {
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;

        // Lazy pipeline creation.
        if (!m_Pipeline || !m_OverlayPipeline)
        {
            m_Pipeline = BuildPipeline(ctx.SwapchainFormat, ctx.DepthFormat, true);
            m_OverlayPipeline = BuildPipeline(ctx.SwapchainFormat, ctx.DepthFormat, false);
            if (!m_Pipeline || !m_OverlayPipeline)
            {
                static bool s_Logged = false;
                if (!s_Logged)
                {
                    s_Logged = true;
                    Core::Log::Error("LinePass: pipeline creation failed. Line rendering will be skipped.");
                }
                return;
            }
        }

        // Per-entity draw command.
        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint64_t PtrEdges;
            uint32_t EdgeCount;
            uint32_t Color;
            float    LineWidth;
            uint64_t PtrEdgeAux;   // 0 = use uniform Color; non-zero = per-edge BDA colors
        };

        std::vector<DrawInfo> depthDraws;    // depth-tested draws (retained + transient)
        std::vector<DrawInfo> overlayDraws;  // no-depth-test draws (retained overlay + transient overlay)

        // Track which entity keys have active aux buffers this frame.
        std::vector<uint32_t> activeAuxKeys;

        // =================================================================
        // Retained-mode draws via ECS::Line::Component
        // =================================================================
        // All retained edge sources provide a valid EdgeView geometry
        // handle with a BDA-addressable index buffer (created by
        // MeshViewLifecycleSystem or GraphGeometrySyncSystem).
        if (m_GeometryStorage)
        {
            auto& registry = ctx.Scene.GetRegistry();

            // Frustum extraction — CPU-side culling for retained-mode draws.
            const bool cullingEnabled = !ctx.Debug.DisableCulling;
            const Geometry::Frustum frustum = cullingEnabled
                ? Geometry::Frustum::CreateFromMatrix(ctx.CameraProj * ctx.CameraView)
                : Geometry::Frustum{};

            auto lineView = registry.view<ECS::Line::Component>();

            for (auto [entity, line] : lineView.each())
            {
                // Skip entities without valid geometry or edges.
                if (!line.Geometry.IsValid() || line.EdgeCount == 0)
                    continue;

                // EdgeView must be valid — all edge sources create proper
                // BDA index buffers via ReuseVertexBuffersFrom.
                if (!line.EdgeView.IsValid())
                    continue;

                GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(line.Geometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                GeometryGpuData* edgeGeo = m_GeometryStorage->GetUnchecked(line.EdgeView);
                if (!edgeGeo || !edgeGeo->GetIndexBuffer() || !edgeGeo->GetVertexBuffer())
                    continue;

                glm::mat4 worldMatrix(1.0f);
                if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                    worldMatrix = wm->Matrix;

                if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                    continue;

                const uint32_t entityKey = static_cast<uint32_t>(entity);
                const uint32_t edgeCount = line.EdgeCount;

                const uint64_t edgeAddr = edgeGeo->GetIndexBuffer()->GetDeviceAddress();
                const uint64_t baseAddr = edgeGeo->GetVertexBuffer()->GetDeviceAddress();
                const uint64_t posAddr = baseAddr + edgeGeo->GetLayout().PositionsOffset;

                const uint32_t wireColor = GpuColor::PackColorF(
                    line.Color.r, line.Color.g, line.Color.b, line.Color.a);

                // Per-edge color aux buffer (sourced from legacy components).
                uint64_t edgeAuxAddr = 0;
                if (line.HasPerEdgeColors)
                {
                    const uint32_t* colorData = nullptr;
                    uint32_t colorCount = 0;

                    if (const auto* gd = registry.try_get<ECS::Graph::Data>(entity))
                    {
                        if (!gd->CachedEdgeColors.empty() &&
                            gd->CachedEdgeColors.size() == edgeCount)
                        {
                            colorData = gd->CachedEdgeColors.data();
                            colorCount = edgeCount;
                        }
                    }

                    if (!colorData)
                    {
                        if (const auto* vis = registry.try_get<ECS::RenderVisualization::Component>(entity))
                        {
                            if (!vis->CachedEdgeColors.empty() &&
                                vis->CachedEdgeColors.size() == edgeCount)
                            {
                                colorData = vis->CachedEdgeColors.data();
                                colorCount = edgeCount;
                            }
                        }
                    }

                    if (colorData)
                    {
                        activeAuxKeys.push_back(entityKey);
                        edgeAuxAddr = EnsureEdgeAuxBuffer(entityKey, colorData, colorCount);
                    }
                }

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrEdges = edgeAddr;
                di.EdgeCount = edgeCount;
                di.Color = wireColor;
                di.LineWidth = line.Width;
                di.PtrEdgeAux = edgeAuxAddr;

                if (line.Overlay)
                    overlayDraws.push_back(di);
                else
                    depthDraws.push_back(di);

                // Write back edge count for diagnostics.
                if (auto* vr = registry.try_get<ECS::GeometryViewRenderer::Component>(entity))
                    vr->WireframeEdgeCount = edgeCount;
            }

            // Cleanup orphaned edge aux buffers.
            if (!m_EdgeAuxBuffers.empty())
            {
                std::sort(activeAuxKeys.begin(), activeAuxKeys.end());

                for (auto it = m_EdgeAuxBuffers.begin(); it != m_EdgeAuxBuffers.end(); )
                {
                    if (!std::binary_search(activeAuxKeys.begin(), activeAuxKeys.end(), it->first))
                    {
                        if (it->second.Buffer)
                            m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
                        it = m_EdgeAuxBuffers.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }

        // =================================================================
        // Transient DebugDraw lines (uploaded per-frame via BDA)
        // =================================================================
        if (m_DebugDraw && m_DebugDraw->HasContent())
        {
            const uint32_t depthLineCount = m_DebugDraw->GetLineCount();
            const uint32_t overlayLineCount = m_DebugDraw->GetOverlayLineCount();
            const uint32_t totalTransient = depthLineCount + overlayLineCount;

            if (totalTransient > 0)
            {
                const uint32_t frameIndex = ctx.FrameIndex;

                if (!EnsureTransientBuffers(totalTransient, frameIndex))
                {
                    Core::Log::Error("LinePass: Failed to ensure transient buffers.");
                }
                else
                {
                    // Upload all transient lines into a single position + color buffer.
                    // Layout: [depth-tested lines..., overlay lines...]
                    auto depthLines = m_DebugDraw->GetLines();
                    auto overlayLines = m_DebugDraw->GetOverlayLines();

                    // Build flat position array and color array from LineSegments.
                    std::vector<glm::vec3> positions;
                    std::vector<uint32_t> colors;
                    positions.reserve(static_cast<size_t>(totalTransient) * 2);
                    colors.reserve(totalTransient);

                    // Depth-tested lines first.
                    for (const auto& seg : depthLines)
                    {
                        positions.push_back(seg.Start);
                        positions.push_back(seg.End);
                        colors.push_back(seg.ColorStart);
                    }

                    // Overlay lines second.
                    for (const auto& seg : overlayLines)
                    {
                        positions.push_back(seg.Start);
                        positions.push_back(seg.End);
                        colors.push_back(seg.ColorStart);
                    }

                    // Upload to per-frame buffers.
                    m_TransientPosBuffer[frameIndex]->Write(
                        positions.data(), positions.size() * sizeof(glm::vec3));
                    m_TransientColorBuffer[frameIndex]->Write(
                        colors.data(), colors.size() * sizeof(uint32_t));

                    const uint64_t posAddr = m_TransientPosBuffer[frameIndex]->GetDeviceAddress();
                    const uint64_t edgeAddr = m_TransientEdgeBuffer->GetDeviceAddress();
                    const uint64_t colorAddr = m_TransientColorBuffer[frameIndex]->GetDeviceAddress();

                    // Emit depth-tested transient draw (identity model, world-space positions).
                    if (depthLineCount > 0)
                    {
                        DrawInfo di{};
                        di.Model = glm::mat4(1.0f);
                        di.PtrPositions = posAddr;
                        di.PtrEdges = edgeAddr;
                        di.EdgeCount = depthLineCount;
                        di.Color = 0xFFFFFFFFu; // white fallback (unused when PtrEdgeAux != 0)
                        di.LineWidth = LineWidth;
                        di.PtrEdgeAux = colorAddr;
                        depthDraws.push_back(di);
                    }

                    // Emit overlay transient draw (starts after depth-tested lines).
                    if (overlayLineCount > 0)
                    {
                        // Overlay positions start at offset depthLineCount * 2 vertices.
                        const uint64_t overlayPosAddr = posAddr
                            + static_cast<uint64_t>(depthLineCount) * 2 * sizeof(glm::vec3);
                        // Overlay edge pairs start at offset depthLineCount edges.
                        const uint64_t overlayEdgeAddr = edgeAddr
                            + static_cast<uint64_t>(depthLineCount) * sizeof(EdgePair);
                        // Overlay colors start at offset depthLineCount.
                        const uint64_t overlayColorAddr = colorAddr
                            + static_cast<uint64_t>(depthLineCount) * sizeof(uint32_t);

                        DrawInfo di{};
                        di.Model = glm::mat4(1.0f);
                        di.PtrPositions = overlayPosAddr;
                        di.PtrEdges = overlayEdgeAddr;
                        di.EdgeCount = overlayLineCount;
                        di.Color = 0xFFFFFFFFu;
                        di.LineWidth = LineWidth;
                        di.PtrEdgeAux = overlayColorAddr;
                        overlayDraws.push_back(di);
                    }
                }
            }
        }

        const bool hasDepthDraws = !depthDraws.empty();
        const bool hasOverlayDraws = !overlayDraws.empty();

        if (!hasDepthDraws && !hasOverlayDraws)
            return;

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid())
            return;

        // Capture values for the lambdas.
        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;

        // =================================================================
        // Pass 1: Depth-tested lines (retained + transient)
        // =================================================================
        if (hasDepthDraws && depth.IsValid())
        {
            auto allDepthDraws = std::make_shared<std::vector<DrawInfo>>(std::move(depthDraws));

            ctx.Graph.AddPass<PassData>("Lines_Depth",
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
                [this, globalSet, dynamicOffset, resolution, allDepthDraws]
                (const PassData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    SetViewportScissor(cmd, resolution);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_Pipeline->GetLayout(),
                                            0, 1, &globalSet,
                                            1, &dynamicOffset);

                    for (const auto& di : *allDepthDraws)
                    {
                        LinePushConstants push{};
                        push.Model = di.Model;
                        push.PtrPositions = di.PtrPositions;
                        push.PtrEdges = di.PtrEdges;
                        push.LineWidth = di.LineWidth;
                        push.ViewportWidth = static_cast<float>(resolution.width);
                        push.ViewportHeight = static_cast<float>(resolution.height);
                        push.Color = di.Color;
                        push.PtrEdgeAux = di.PtrEdgeAux;

                        vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(push), &push);

                        vkCmdDraw(cmd, di.EdgeCount * 6, 1, 0, 0);
                    }
                });
        }

        // =================================================================
        // Pass 2: Overlay lines (no depth test — always on top)
        // =================================================================
        if (hasOverlayDraws)
        {
            auto capturedOverlay = std::make_shared<std::vector<DrawInfo>>(std::move(overlayDraws));

            ctx.Graph.AddPass<PassData>("Lines_Overlay",
                [&](PassData& data, RGBuilder& builder)
                {
                    RGAttachmentInfo colorInfo{};
                    colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                    data.Color = builder.WriteColor(backbuffer, colorInfo);
                    data.Depth = {}; // No depth attachment.
                },
                [this, globalSet, dynamicOffset, resolution, capturedOverlay]
                (const PassData&, const RGRegistry&, VkCommandBuffer cmd)
                {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_OverlayPipeline->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    SetViewportScissor(cmd, resolution);

                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_OverlayPipeline->GetLayout(),
                                            0, 1, &globalSet,
                                            1, &dynamicOffset);

                    for (const auto& di : *capturedOverlay)
                    {
                        LinePushConstants push{};
                        push.Model = di.Model;
                        push.PtrPositions = di.PtrPositions;
                        push.PtrEdges = di.PtrEdges;
                        push.LineWidth = di.LineWidth;
                        push.ViewportWidth = static_cast<float>(resolution.width);
                        push.ViewportHeight = static_cast<float>(resolution.height);
                        push.Color = di.Color;
                        push.PtrEdgeAux = di.PtrEdgeAux;

                        vkCmdPushConstants(cmd, m_OverlayPipeline->GetLayout(),
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(push), &push);

                        vkCmdDraw(cmd, di.EdgeCount * 6, 1, 0, 0);
                    }
                });
        }
    }

} // namespace Graphics::Passes
