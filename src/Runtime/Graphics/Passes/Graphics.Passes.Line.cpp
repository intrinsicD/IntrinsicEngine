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
    // EnsureEdgeBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable edge buffer for an entity.
    // Returns the buffer device address, or 0 on failure.

    uint64_t LinePass::EnsureEdgeBuffer(
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
            Core::Log::Error("LinePass: Failed to allocate edge buffer ({} bytes)", size);
            return 0;
        }

        buf->Write(edgeData, static_cast<size_t>(size));
        const uint64_t addr = buf->GetDeviceAddress();

        m_EdgeBuffers[entityKey] = { std::move(buf), edgeCount, sourceGeoIdx };
        return addr;
    }

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

        // Deferred-destroy all persistent edge buffers.
        for (auto& [key, entry] : m_EdgeBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_EdgeBuffers.clear();

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

        // Per-entity draw command — each entity has its own persistent edge buffer.
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

        std::vector<DrawInfo> draws;          // depth-tested retained draws
        std::vector<DrawInfo> transientDraws; // depth-tested transient draws
        std::vector<DrawInfo> overlayDraws;   // no-depth-test transient draws

        // Track which entity keys are active this frame (for orphan cleanup).
        std::vector<uint32_t> activeKeys;

        // =================================================================
        // Retained-mode draws (mesh wireframe + graph edges)
        // =================================================================
        if (m_GeometryStorage)
        {
            auto& registry = ctx.Scene.GetRegistry();

            // Frustum extraction — CPU-side culling for retained-mode draws.
            const bool cullingEnabled = !ctx.Debug.DisableCulling;
            const Geometry::Frustum frustum = cullingEnabled
                ? Geometry::Frustum::CreateFromMatrix(ctx.CameraProj * ctx.CameraView)
                : Geometry::Frustum{};

            // --- Mesh entities ---
            auto meshView = registry.view<ECS::MeshRenderer::Component,
                                          ECS::RenderVisualization::Component>();

            for (auto [entity, mr, vis] : meshView.each())
            {
                if (!vis.ShowWireframe)
                    continue;

                GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(mr.Geometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                auto* edgeViewComp = registry.try_get<ECS::MeshEdgeView::Component>(entity);
                const bool useEdgeView = edgeViewComp
                    && edgeViewComp->HasGpuGeometry()
                    && edgeViewComp->EdgeCount > 0;

                if (!useEdgeView && vis.CachedEdges.empty())
                    continue;

                const uint32_t entityKey = static_cast<uint32_t>(entity);
                if (!useEdgeView)
                    activeKeys.push_back(entityKey);

                glm::mat4 worldMatrix(1.0f);
                if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                    worldMatrix = wm->Matrix;

                if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                    continue;

                uint32_t edgeCount = 0;
                uint64_t edgeAddr = 0;
                uint64_t posAddr = 0;

                if (useEdgeView)
                {
                    GeometryGpuData* edgeGeo = m_GeometryStorage->GetUnchecked(edgeViewComp->Geometry);
                    if (!edgeGeo || !edgeGeo->GetIndexBuffer() || !edgeGeo->GetVertexBuffer())
                        continue;

                    edgeCount = edgeViewComp->EdgeCount;
                    edgeAddr = edgeGeo->GetIndexBuffer()->GetDeviceAddress();

                    const uint64_t baseAddr = edgeGeo->GetVertexBuffer()->GetDeviceAddress();
                    posAddr = baseAddr + edgeGeo->GetLayout().PositionsOffset;
                }
                else
                {
                    edgeCount = static_cast<uint32_t>(vis.CachedEdges.size());
                    edgeAddr = EnsureEdgeBuffer(
                        entityKey, vis.CachedEdges.data(), edgeCount, mr.Geometry.Index);
                    if (edgeAddr == 0)
                        continue;

                    const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                    posAddr = baseAddr + geo->GetLayout().PositionsOffset;
                }

                const uint32_t wireColor = GpuColor::PackColorF(
                    vis.WireframeColor.r, vis.WireframeColor.g,
                    vis.WireframeColor.b, vis.WireframeColor.a);

                uint64_t edgeAuxAddr = 0;
                if (!vis.CachedEdgeColors.empty() && vis.CachedEdgeColors.size() == edgeCount)
                {
                    if (useEdgeView)
                        activeKeys.push_back(entityKey);
                    edgeAuxAddr = EnsureEdgeAuxBuffer(
                        entityKey, vis.CachedEdgeColors.data(), edgeCount);
                }

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrEdges = edgeAddr;
                di.EdgeCount = edgeCount;
                di.Color = wireColor;
                di.LineWidth = vis.WireframeWidth;
                di.PtrEdgeAux = edgeAuxAddr;
                draws.push_back(di);

                if (auto* vr = registry.try_get<ECS::GeometryViewRenderer::Component>(entity))
                    vr->WireframeEdgeCount = edgeCount;
            }

            // --- Graph entities ---
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

                const uint32_t entityKey = static_cast<uint32_t>(entity) | 0x80000000u;
                activeKeys.push_back(entityKey);

                glm::mat4 worldMatrix(1.0f);
                if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                    worldMatrix = wm->Matrix;

                if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                    continue;

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

                uint64_t edgeAuxAddr = 0;
                if (!graphData.CachedEdgeColors.empty() && graphData.CachedEdgeColors.size() == edgeCount)
                {
                    edgeAuxAddr = EnsureEdgeAuxBuffer(
                        entityKey, graphData.CachedEdgeColors.data(), edgeCount);
                }

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrEdges = edgeAddr;
                di.EdgeCount = edgeCount;
                di.Color = edgeColor;
                di.LineWidth = graphData.EdgeWidth;
                di.PtrEdgeAux = edgeAuxAddr;
                draws.push_back(di);
            }

            // Cleanup orphaned edge buffers and edge aux buffers.
            if (!m_EdgeBuffers.empty())
            {
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

                for (auto it = m_EdgeAuxBuffers.begin(); it != m_EdgeAuxBuffers.end(); )
                {
                    if (!std::binary_search(activeKeys.begin(), activeKeys.end(), it->first))
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
                        transientDraws.push_back(di);
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

        const bool hasDepthDraws = !draws.empty() || !transientDraws.empty();
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
            // Merge retained and transient depth-tested draws.
            auto allDepthDraws = std::make_shared<std::vector<DrawInfo>>(std::move(draws));
            allDepthDraws->insert(allDepthDraws->end(),
                                  transientDraws.begin(), transientDraws.end());

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
