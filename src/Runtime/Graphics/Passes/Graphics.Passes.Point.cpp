module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <array>
#include <algorithm>

#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.Point.Impl;

import :Passes.Point;
import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :Components;
import :ShaderRegistry;
import :GpuColor;
import :DebugDraw;
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

    // Push constants layout — shared across all PointPass shader variants.
    // 112 bytes: well under Vulkan 1.0 minimum of 128.
    struct PointPushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset 0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrNormals;      //  8 bytes, offset 72
        uint64_t  PtrAux;          //  8 bytes, offset 80  (per-point packed ABGR; 0 = use uniform Color)
        float     PointSize;       //  4 bytes, offset 88
        float     SizeMultiplier;  //  4 bytes, offset 92
        float     ViewportWidth;   //  4 bytes, offset 96
        float     ViewportHeight;  //  4 bytes, offset 100
        uint32_t  Color;           //  4 bytes, offset 104  (packed ABGR)
        uint32_t  Flags;           //  4 bytes, offset 108  (bit 0: has per-point colors via PtrAux)
    };
    static_assert(sizeof(PointPushConstants) == 112);

    // Shader ID pairs indexed by render mode.
    struct ModeShaderIds
    {
        StringID Vert;
        StringID Frag;
    };

    static constexpr ModeShaderIds kShaderIds[] = {
        { "Point.FlatDisc.Vert"_id, "Point.FlatDisc.Frag"_id },
        { "Point.Surfel.Vert"_id,   "Point.Surfel.Frag"_id   },
        { "Point.EWA.Vert"_id,      "Point.EWA.Frag"_id      },
    };

    // =========================================================================
    // Initialize
    // =========================================================================

    void PointPass::Initialize(RHI::VulkanDevice& device,
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

    void PointPass::Shutdown()
    {
        if (!m_Device) return;
        for (auto& p : m_Pipelines) p.reset();
        for (auto& buf : m_TransientPosBuffer) buf.reset();
        for (auto& buf : m_TransientNormBuffer) buf.reset();
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> PointPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat,
        Geometry::PointCloud::RenderMode mode)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("PointPass: ShaderRegistry not configured.");
            return nullptr;
        }

        const auto modeIdx = static_cast<size_t>(mode);
        if (modeIdx >= kModeCount)
        {
            Core::Log::Error("PointPass: Invalid render mode {}.", modeIdx);
            return nullptr;
        }

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry,
                                                       kShaderIds[modeIdx].Vert,
                                                       kShaderIds[modeIdx].Frag);

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
        pcr.size = sizeof(PointPushConstants);
        pb.AddPushConstantRange(pcr);

        auto result = pb.Build();
        if (!result)
        {
            Core::Log::Error("PointPass: Failed to build pipeline for mode {} (VkResult={}).",
                             modeIdx, (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // EnsureTransientBuffers
    // =========================================================================

    bool PointPass::EnsureTransientBuffers(uint32_t pointCount, uint32_t frameIndex)
    {
        if (pointCount == 0) return true;

        // Position buffer growth.
        if (pointCount > m_TransientPosCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientPosCapacity);
            while (newCap < pointCount) newCap *= 2;

            for (uint32_t i = 0; i < FRAMES; ++i)
            {
                if (m_TransientPosBuffer[i])
                    m_Device->SafeDestroy([old = std::move(m_TransientPosBuffer[i])]() {});

                m_TransientPosBuffer[i] = std::make_unique<RHI::VulkanBuffer>(
                    *m_Device, newCap * sizeof(glm::vec3),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!m_TransientPosBuffer[i]->GetMappedData())
                {
                    Core::Log::Error("PointPass: Failed to allocate transient position buffer.");
                    return false;
                }
            }
            m_TransientPosCapacity = newCap;
        }

        // Normal buffer growth (same capacity).
        if (pointCount > m_TransientNormCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientNormCapacity);
            while (newCap < pointCount) newCap *= 2;

            for (uint32_t i = 0; i < FRAMES; ++i)
            {
                if (m_TransientNormBuffer[i])
                    m_Device->SafeDestroy([old = std::move(m_TransientNormBuffer[i])]() {});

                m_TransientNormBuffer[i] = std::make_unique<RHI::VulkanBuffer>(
                    *m_Device, newCap * sizeof(glm::vec3),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!m_TransientNormBuffer[i]->GetMappedData())
                {
                    Core::Log::Error("PointPass: Failed to allocate transient normal buffer.");
                    return false;
                }
            }
            m_TransientNormCapacity = newCap;
        }

        return true;
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void PointPass::AddPasses(RenderPassContext& ctx)
    {
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;
        if (!m_GeometryStorage) return;

        const uint32_t frameIndex = ctx.FrameIndex;

        // -----------------------------------------------------------------
        // Draw info: one per entity or transient batch.
        // -----------------------------------------------------------------
        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint64_t PtrNormals;
            uint64_t PtrAux;
            uint32_t VertexCount;
            float PointSize;
            uint32_t RenderMode;   // index into m_Pipelines
            uint32_t Color;
            uint32_t Flags;
        };

        std::array<std::vector<DrawInfo>, kModeCount> drawsByMode;

        auto& registry = ctx.Scene.GetRegistry();

        // -----------------------------------------------------------------
        // Frustum extraction — CPU-side culling for retained-mode draws.
        // -----------------------------------------------------------------
        const bool cullingEnabled = !ctx.Debug.DisableCulling;
        const Geometry::Frustum frustum = cullingEnabled
            ? Geometry::Frustum::CreateFromMatrix(ctx.CameraProj * ctx.CameraView)
            : Geometry::Frustum{};

        // Helper: add a retained draw from geometry data.
        auto addDraw = [&](uint32_t mode, const glm::mat4& model,
                           uint64_t posAddr, uint64_t normAddr, uint64_t auxAddr,
                           uint32_t vertexCount, float pointSize,
                           uint32_t color, uint32_t flags)
        {
            if (mode >= kModeCount) mode = 0;
            DrawInfo di{};
            di.Model = model;
            di.PtrPositions = posAddr;
            di.PtrNormals = normAddr;
            di.PtrAux = auxAddr;
            di.VertexCount = vertexCount;
            di.PointSize = pointSize;
            di.RenderMode = mode;
            di.Color = color;
            di.Flags = flags;
            drawsByMode[mode].push_back(di);
        };

        // Helper: extract geometry data from a GeometryGpuData pointer.
        auto extractGeometry = [&](GeometryGpuData* geo, uint64_t& posAddr, uint64_t& normAddr,
                                   uint32_t& vertexCount, glm::vec4& localBounds) -> bool
        {
            if (!geo || !geo->GetVertexBuffer()) return false;
            const auto& layout = geo->GetLayout();
            if (layout.PositionsSize == 0) return false;
            vertexCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
            if (vertexCount == 0) return false;
            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            posAddr = baseAddr + layout.PositionsOffset;
            normAddr = (layout.NormalsSize > 0) ? (baseAddr + layout.NormalsOffset) : 0;
            localBounds = geo->GetLocalBoundingSphere();
            return true;
        };

        // Helper: get world matrix for an entity.
        auto getWorldMatrix = [&](entt::entity entity) -> glm::mat4
        {
            if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                return wm->Matrix;
            return glm::mat4(1.0f);
        };

        // =================================================================
        // 1. ECS::Point::Component — first-class per-pass typed component.
        // =================================================================
        auto pointView = registry.view<ECS::Point::Component>();
        for (auto [entity, ptComp] : pointView.each())
        {
            if (!ptComp.Geometry.IsValid()) continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(ptComp.Geometry);
            uint64_t posAddr = 0, normAddr = 0;
            uint32_t vertexCount = 0;
            glm::vec4 localBounds{};

            if (!extractGeometry(geo, posAddr, normAddr, vertexCount, localBounds))
                continue;

            glm::mat4 worldMatrix = getWorldMatrix(entity);

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, localBounds, frustum))
                continue;

            const uint32_t mode = static_cast<uint32_t>(ptComp.Mode);
            const uint32_t color = GpuColor::PackColorF(
                ptComp.Color.r, ptComp.Color.g, ptComp.Color.b, ptComp.Color.a);

            uint32_t flags = 0;
            if (ptComp.HasPerPointColors) flags |= 1u;

            addDraw(mode, worldMatrix, posAddr, normAddr, 0,
                    vertexCount, ptComp.Size * ptComp.SizeMultiplier, color, flags);
        }

        // =================================================================
        // 2. Mesh vertex visualization — MeshRenderer + ShowVertices.
        //    Uses MeshVertexView when available, falls back to direct mesh.
        // =================================================================
        auto meshView = registry.view<ECS::MeshRenderer::Component,
                                      ECS::RenderVisualization::Component>();
        for (auto [entity, mr, vis] : meshView.each())
        {
            if (!vis.ShowVertices) continue;

            auto* vtxViewComp = registry.try_get<ECS::MeshVertexView::Component>(entity);
            const bool useVertexView = vtxViewComp
                && vtxViewComp->HasGpuGeometry()
                && vtxViewComp->VertexCount > 0;

            uint64_t posAddr = 0, normAddr = 0;
            uint32_t vertexCount = 0;
            glm::vec4 localBounds{};

            if (useVertexView)
            {
                GeometryGpuData* vtxGeo = m_GeometryStorage->GetUnchecked(vtxViewComp->Geometry);
                if (!vtxGeo || !vtxGeo->GetVertexBuffer()) continue;
                vertexCount = vtxViewComp->VertexCount;
                const auto& vtxLayout = vtxGeo->GetLayout();
                const uint64_t baseAddr = vtxGeo->GetVertexBuffer()->GetDeviceAddress();
                posAddr = baseAddr + vtxLayout.PositionsOffset;
                normAddr = (vtxLayout.NormalsSize > 0) ? (baseAddr + vtxLayout.NormalsOffset) : 0;
                localBounds = vtxGeo->GetLocalBoundingSphere();
            }
            else
            {
                GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(mr.Geometry);
                if (!extractGeometry(geo, posAddr, normAddr, vertexCount, localBounds))
                    continue;
            }

            glm::mat4 worldMatrix = getWorldMatrix(entity);

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, localBounds, frustum))
                continue;

            const uint32_t mode = static_cast<uint32_t>(vis.VertexRenderMode);
            const uint32_t vtxColor = GpuColor::PackColorF(
                vis.VertexColor.r, vis.VertexColor.g,
                vis.VertexColor.b, vis.VertexColor.a);

            addDraw(mode, worldMatrix, posAddr, normAddr, 0,
                    vertexCount, vis.VertexSize, vtxColor, 0);
        }

        // =================================================================
        // 3. Standalone point cloud entities — PointCloudRenderer::Component.
        // =================================================================
        auto pcView = registry.view<ECS::PointCloudRenderer::Component>();
        for (auto [entity, pc] : pcView.each())
        {
            if (!pc.Visible || !pc.Geometry.IsValid()) continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(pc.Geometry);
            uint64_t posAddr = 0, normAddr = 0;
            uint32_t vertexCount = 0;
            glm::vec4 localBounds{};

            if (!extractGeometry(geo, posAddr, normAddr, vertexCount, localBounds))
                continue;

            glm::mat4 worldMatrix = getWorldMatrix(entity);

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, localBounds, frustum))
                continue;

            const uint32_t mode = static_cast<uint32_t>(pc.RenderMode);
            const uint32_t ptColor = GpuColor::PackColorF(
                pc.DefaultColor.r, pc.DefaultColor.g,
                pc.DefaultColor.b, pc.DefaultColor.a);

            addDraw(mode, worldMatrix, posAddr, normAddr, 0,
                    vertexCount, pc.DefaultRadius, ptColor, 0);
        }

        // =================================================================
        // 4. Graph node entities — ECS::Graph::Data.
        // =================================================================
        auto graphView = registry.view<ECS::Graph::Data>();
        for (auto [entity, graphData] : graphView.each())
        {
            if (!graphData.Visible || !graphData.GpuGeometry.IsValid()) continue;
            if (graphData.GpuVertexCount == 0) continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(graphData.GpuGeometry);
            if (!geo || !geo->GetVertexBuffer()) continue;

            glm::mat4 worldMatrix = getWorldMatrix(entity);

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                continue;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const auto& layout = geo->GetLayout();
            const uint64_t posAddr = baseAddr + layout.PositionsOffset;
            const uint64_t normAddr = (layout.NormalsSize > 0) ? (baseAddr + layout.NormalsOffset) : 0;

            const uint32_t mode = static_cast<uint32_t>(graphData.NodeRenderMode);
            const uint32_t nodeColor = GpuColor::PackColorF(
                graphData.DefaultNodeColor.r, graphData.DefaultNodeColor.g,
                graphData.DefaultNodeColor.b, graphData.DefaultNodeColor.a);

            addDraw(mode, worldMatrix, posAddr, normAddr, 0,
                    graphData.GpuVertexCount, graphData.DefaultNodeRadius, nodeColor, 0);
        }

        // =================================================================
        // 5. Cloud-backed point cloud entities — ECS::PointCloud::Data.
        // =================================================================
        auto cloudView = registry.view<ECS::PointCloud::Data>();
        for (auto [entity, pcData] : cloudView.each())
        {
            if (!pcData.Visible || !pcData.GpuGeometry.IsValid()) continue;
            if (pcData.GpuPointCount == 0) continue;

            GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(pcData.GpuGeometry);
            if (!geo || !geo->GetVertexBuffer()) continue;

            glm::mat4 worldMatrix = getWorldMatrix(entity);

            if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                continue;

            const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
            const auto& layout = geo->GetLayout();
            const uint64_t posAddr = baseAddr + layout.PositionsOffset;
            const uint64_t normAddr = (layout.NormalsSize > 0) ? (baseAddr + layout.NormalsOffset) : 0;

            const uint32_t mode = static_cast<uint32_t>(pcData.RenderMode);
            const uint32_t ptColor = GpuColor::PackColorF(
                pcData.DefaultColor.r, pcData.DefaultColor.g,
                pcData.DefaultColor.b, pcData.DefaultColor.a);

            addDraw(mode, worldMatrix, posAddr, normAddr, 0,
                    pcData.GpuPointCount, pcData.DefaultRadius, ptColor, 0);
        }

        // =================================================================
        // 6. Transient debug points — DebugDraw::GetPoints().
        // =================================================================
        uint32_t transientPointCount = 0;
        if (m_DebugDraw)
            transientPointCount = m_DebugDraw->GetPointCount();

        if (transientPointCount > 0 && EnsureTransientBuffers(transientPointCount, frameIndex))
        {
            auto points = m_DebugDraw->GetPoints();

            // Pack positions and normals into separate transient buffers.
            std::vector<glm::vec3> positions(transientPointCount);
            std::vector<glm::vec3> normals(transientPointCount);
            for (uint32_t i = 0; i < transientPointCount; ++i)
            {
                positions[i] = points[i].Position;
                normals[i] = points[i].Normal;
            }

            m_TransientPosBuffer[frameIndex]->Write(positions.data(),
                transientPointCount * sizeof(glm::vec3));
            m_TransientNormBuffer[frameIndex]->Write(normals.data(),
                transientPointCount * sizeof(glm::vec3));

            const uint64_t posAddr = m_TransientPosBuffer[frameIndex]->GetDeviceAddress();
            const uint64_t normAddr = m_TransientNormBuffer[frameIndex]->GetDeviceAddress();

            // Each transient point is drawn as FlatDisc by default.
            // Group by mode if needed in the future.
            for (uint32_t i = 0; i < transientPointCount; ++i)
            {
                const auto& pt = points[i];
                const uint32_t mode = 0u; // FlatDisc for debug points

                DrawInfo di{};
                di.Model = glm::mat4(1.0f);
                di.PtrPositions = posAddr + i * sizeof(glm::vec3);
                di.PtrNormals = normAddr + i * sizeof(glm::vec3);
                di.PtrAux = 0;
                di.VertexCount = 1;
                di.PointSize = pt.Size;
                di.RenderMode = mode;
                di.Color = pt.Color;
                di.Flags = 0;
                drawsByMode[mode].push_back(di);
            }
        }

        // Check if we have any draws.
        bool anyDraws = false;
        for (const auto& mode : drawsByMode)
        {
            if (!mode.empty()) { anyDraws = true; break; }
        }
        if (!anyDraws) return;

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid()) return;

        // Ensure pipelines are built lazily.
        for (size_t m = 0; m < kModeCount; ++m)
        {
            if (!drawsByMode[m].empty() && !m_Pipelines[m])
            {
                m_Pipelines[m] = BuildPipeline(ctx.SwapchainFormat, ctx.DepthFormat,
                                                static_cast<Geometry::PointCloud::RenderMode>(m));
                if (!m_Pipelines[m])
                {
                    Core::Log::Error("PointPass: pipeline creation failed for mode {}.", m);
                    drawsByMode[m].clear();
                }
            }
        }

        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;
        const float sizeMultiplier = SizeMultiplier;

        // Capture draws for the render graph lambda.
        auto capturedDraws = std::make_shared<std::array<std::vector<DrawInfo>, kModeCount>>(
            std::move(drawsByMode));

        ctx.Graph.AddPass<PassData>("Points",
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
                SetViewportScissor(cmd, resolution);

                for (size_t m = 0; m < kModeCount; ++m)
                {
                    const auto& draws = (*capturedDraws)[m];
                    if (draws.empty() || !m_Pipelines[m]) continue;

                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_Pipelines[m]->GetHandle());
                    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                    // Bind set 0: global camera (with dynamic offset).
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_Pipelines[m]->GetLayout(),
                                            0, 1, &globalSet,
                                            1, &dynamicOffset);

                    for (const auto& di : draws)
                    {
                        PointPushConstants push{};
                        push.Model = di.Model;
                        push.PtrPositions = di.PtrPositions;
                        push.PtrNormals = di.PtrNormals;
                        push.PtrAux = di.PtrAux;
                        push.PointSize = di.PointSize;
                        push.SizeMultiplier = sizeMultiplier;
                        push.ViewportWidth = static_cast<float>(resolution.width);
                        push.ViewportHeight = static_cast<float>(resolution.height);
                        push.Color = di.Color;
                        push.Flags = di.Flags;

                        vkCmdPushConstants(cmd, m_Pipelines[m]->GetLayout(),
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                           0, sizeof(push), &push);

                        // Each vertex becomes 6 GPU vertices (billboard quad).
                        vkCmdDraw(cmd, di.VertexCount * 6, 1, 0, 0);
                    }
                }
            });
    }

} // namespace Graphics::Passes
