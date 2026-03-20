module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.Passes.Line;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Geometry;
import Graphics.Components;
import Graphics.DebugDraw;
import Graphics.ShaderRegistry;
import Graphics.GpuColor;

import Geometry.Frustum;
import Geometry.Overlap;
import Geometry.Sphere;

import Core.Hash;
import Core.Logging;
import Core.Filesystem;

import ECS;

import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;
import RHI.Shader;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

    // Push constants layout (must match line.vert / line.frag).
    struct LinePushConstants
    {
        glm::mat4 Model{1.0f};           // 64 bytes, offset  0
        uint64_t  PtrPositions{0};    //  8 bytes, offset 64
        uint64_t  PtrEdges{0};        //  8 bytes, offset 72
        float     LineWidth{0.0f};       //  4 bytes, offset 80
        float     ViewportWidth{0.0f};   //  4 bytes, offset 84
        float     ViewportHeight{0.0f};  //  4 bytes, offset 88
        uint32_t  Color{0};           //  4 bytes, offset 92
        uint64_t  PtrEdgeAttr{0};      //  8 bytes, offset 96 — BDA to per-edge packed ABGR colors (0 = uniform Color)
    };
    static_assert(sizeof(LinePushConstants) == 104);

    // Use EdgePair from the component — identical layout, eliminates type mismatch.
    using EdgePair = ECS::EdgePair;

    // =========================================================================
    // EnsureEdgeAttrBuffer — per-edge attribute buffer (packed ABGR per edge).
    // =========================================================================

    uint64_t LinePass::EnsureEdgeAttrBuffer(
        uint32_t entityKey,
        const uint32_t* colorData,
        uint32_t edgeCount)
    {
        return EnsurePerEntityBuffer<uint32_t>(
            *m_Device, m_EdgeAttrBuffers, entityKey, colorData, edgeCount, "LinePass");
    }

    // =========================================================================
    // EnsureTransientBuffers
    // =========================================================================
    // Ensures per-frame transient buffers (positions, identity edges, colors)
    // have enough capacity for the given segment count.

    bool LinePass::EnsureTransientBuffers(uint32_t segmentCount, [[maybe_unused]] uint32_t frameIndex)
    {
        if (segmentCount == 0)
            return true;

        constexpr uint32_t kMinCap = 256u;
        constexpr VkBufferUsageFlags kBDA = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        const uint32_t vertexCount = segmentCount * 2; // 2 vec3 per segment

        // --- Position buffer (per-frame) ---
        if (!EnsurePerFrameBuffer<glm::vec3, FRAMES>(
                *m_Device, m_TransientPosBuffer, m_TransientPosCapacity,
                vertexCount, kMinCap, "LinePass", kBDA))
            return false;

        // --- Identity edge pair buffer (shared across frames — data is constant) ---
        const uint32_t prevEdgeCap = m_TransientEdgeCapacity;
        if (!EnsureSingleBuffer<EdgePair>(
                *m_Device, m_TransientEdgeBuffer, m_TransientEdgeCapacity,
                segmentCount, kMinCap, "LinePass", kBDA))
            return false;

        // Fill with identity edge pairs when capacity grew: {0,1}, {2,3}, {4,5}, ...
        if (m_TransientEdgeCapacity != prevEdgeCap)
        {
            std::vector<EdgePair> identityEdges(m_TransientEdgeCapacity);
            for (uint32_t i = 0; i < m_TransientEdgeCapacity; ++i)
                identityEdges[i] = { i * 2, i * 2 + 1 };
            m_TransientEdgeBuffer->Write(identityEdges.data(),
                                         m_TransientEdgeCapacity * sizeof(EdgePair));
        }

        // --- Per-edge color buffer (per-frame) ---
        if (!EnsurePerFrameBuffer<uint32_t, FRAMES>(
                *m_Device, m_TransientColorBuffer, m_TransientColorCapacity,
                segmentCount, kMinCap, "LinePass", kBDA))
            return false;

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
        for (auto& [key, entry] : m_EdgeAttrBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_EdgeAttrBuffers.clear();

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
            // Depth bias to prevent z-fighting between edges and mesh surfaces.
            pb.EnableDepthBias(-1.0f, -1.0f);
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
            m_Pipeline = BuildPipeline(ctx.SceneColorFormat, ctx.DepthFormat, true);
            m_OverlayPipeline = BuildPipeline(ctx.SceneColorFormat, ctx.DepthFormat, false);
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
            uint64_t PtrEdgeAttr;   // 0 = use uniform Color; non-zero = per-edge BDA colors
        };

        std::vector<DrawInfo> depthDraws;    // depth-tested draws (retained + transient)
        std::vector<DrawInfo> overlayDraws;  // no-depth-test draws (retained overlay + transient overlay)

        // Track which entity keys have active aux buffers this frame.
        std::vector<uint32_t> activeAttrKeys;

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
            const Geometry::Frustum frustum = CreateCullingFrustum(
                ctx.CameraProj, ctx.CameraView, cullingEnabled);

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

                GeometryGpuData* geo = m_GeometryStorage->GetIfValid(line.Geometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                GeometryGpuData* edgeGeo = m_GeometryStorage->GetIfValid(line.EdgeView);
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
                uint64_t edgeAttrAddr = 0;
                if (line.HasPerEdgeColors && line.ShowPerEdgeColors)
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
                        if (!line.CachedEdgeColors.empty() &&
                            line.CachedEdgeColors.size() == edgeCount)
                        {
                            colorData = line.CachedEdgeColors.data();
                            colorCount = edgeCount;
                        }
                    }

                    if (colorData)
                    {
                        activeAttrKeys.push_back(entityKey);
                        edgeAttrAddr = EnsureEdgeAttrBuffer(entityKey, colorData, colorCount);
                    }
                }

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrEdges = edgeAddr;
                di.EdgeCount = edgeCount;
                di.Color = wireColor;
                // Clamp line width to safe pixel range [0.5, 32.0].
                di.LineWidth = ClampLineWidth(line.Width);
                di.PtrEdgeAttr = edgeAttrAddr;

                if (line.Overlay)
                    overlayDraws.push_back(di);
                else
                    depthDraws.push_back(di);

            }

            // Cleanup orphaned edge attribute buffers.
            CleanupOrphanedBuffers(*m_Device, m_EdgeAttrBuffers, activeAttrKeys);
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

                    auto* mappedPositions = static_cast<glm::vec3*>(m_TransientPosBuffer[frameIndex]->GetMappedData());
                    auto* mappedColors = static_cast<uint32_t*>(m_TransientColorBuffer[frameIndex]->GetMappedData());
                    if (!mappedPositions || !mappedColors)
                    {
                        Core::Log::Error("LinePass: Transient line buffers are not mapped.");
                    }
                    else
                    {
                        uint32_t segmentIndex = 0;

                        // Depth-tested lines first.
                        for (const auto& seg : depthLines)
                        {
                            const uint32_t vertexIndex = segmentIndex * 2u;
                            mappedPositions[vertexIndex + 0u] = seg.Start;
                            mappedPositions[vertexIndex + 1u] = seg.End;
                            mappedColors[segmentIndex] = seg.ColorStart;
                            ++segmentIndex;
                        }

                        // Overlay lines second.
                        for (const auto& seg : overlayLines)
                        {
                            const uint32_t vertexIndex = segmentIndex * 2u;
                            mappedPositions[vertexIndex + 0u] = seg.Start;
                            mappedPositions[vertexIndex + 1u] = seg.End;
                            mappedColors[segmentIndex] = seg.ColorStart;
                            ++segmentIndex;
                        }

                        const size_t positionBytes = static_cast<size_t>(totalTransient) * 2u * sizeof(glm::vec3);
                        const size_t colorBytes = static_cast<size_t>(totalTransient) * sizeof(uint32_t);
                        m_TransientPosBuffer[frameIndex]->Flush(0u, positionBytes);
                        m_TransientColorBuffer[frameIndex]->Flush(0u, colorBytes);

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
                            di.Color = 0xFFFFFFFFu; // white fallback (unused when PtrEdgeAttr != 0)
                            di.LineWidth = ClampLineWidth(LineWidth);
                            di.PtrEdgeAttr = colorAddr;
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
                            di.LineWidth = ClampLineWidth(LineWidth);
                            di.PtrEdgeAttr = overlayColorAddr;
                            overlayDraws.push_back(di);
                        }
                    }
                }
            }
        }

        const bool hasDepthDraws = !depthDraws.empty();
        const bool hasOverlayDraws = !overlayDraws.empty();

        if (!hasDepthDraws && !hasOverlayDraws)
            return;

        // Fetch canonical render targets — line passes render to HDR scene color.
        const RGResourceHandle sceneColor = ctx.Blackboard.Get(RenderResource::SceneColorHDR);
        const RGResourceHandle depth = ctx.Blackboard.Get(RenderResource::SceneDepth);
        if (!sceneColor.IsValid())
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
                    data.Color = builder.WriteColor(sceneColor, colorInfo);

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
                        push.PtrEdgeAttr = di.PtrEdgeAttr;

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
                    data.Color = builder.WriteColor(sceneColor, colorInfo);
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
                        push.PtrEdgeAttr = di.PtrEdgeAttr;

                        vkCmdPushConstants(cmd, m_OverlayPipeline->GetLayout(),
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(push), &push);

                        vkCmdDraw(cmd, di.EdgeCount * 6, 1, 0, 0);
                    }
                });
        }
    }

} // namespace Graphics::Passes
