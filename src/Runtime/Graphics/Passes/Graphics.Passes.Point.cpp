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

module Graphics.Passes.Point;

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

import RHI.Buffer;
import RHI.CommandUtils;
import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;
import RHI.Shader;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{

    // Push constants layout (matches point_flatdisc.vert/frag and point_surfel.vert/frag).
    struct PointPushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset  0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrNormals;      //  8 bytes, offset 72
        uint64_t  PtrAttr;          //  8 bytes, offset 80 — per-point packed ABGR colors (0 = uniform Color)
        float     PointSize;       //  4 bytes, offset 88
        float     SizeMultiplier;  //  4 bytes, offset 92
        float     ViewportWidth;   //  4 bytes, offset 96
        float     ViewportHeight;  //  4 bytes, offset 100
        uint32_t  Color;           //  4 bytes, offset 104
        uint32_t  Flags;           //  4 bytes, offset 108 — bit 0: per-point colors, bit 1: EWA, bit 2: per-point radii
        uint64_t  PtrRadii;       //  8 bytes, offset 112 — per-point float radii (0 = uniform PointSize)
    };
    static_assert(sizeof(PointPushConstants) == 120);

    // =========================================================================
    // EnsurePointAttrBuffer — per-point color attribute buffer (packed ABGR).
    // =========================================================================

    uint64_t PointPass::EnsurePointAttrBuffer(
        uint32_t entityKey,
        const uint32_t* colorData,
        uint32_t pointCount)
    {
        return EnsurePerEntityBuffer<uint32_t>(
            *m_Device, m_PointAttrBuffers, entityKey, colorData, pointCount, "PointPass");
    }

    // =========================================================================
    // EnsurePointRadiiBuffer
    // =========================================================================

    uint64_t PointPass::EnsurePointRadiiBuffer(
        uint32_t entityKey,
        const float* radiiData,
        uint32_t pointCount)
    {
        return EnsurePerEntityBuffer<float>(
            *m_Device, m_PointRadiiBuffers, entityKey, radiiData, pointCount, "PointPass");
    }

    // =========================================================================
    // EnsureTransientBuffers
    // =========================================================================

    bool PointPass::EnsureTransientBuffers(uint32_t pointCount, [[maybe_unused]] uint32_t frameIndex)
    {
        if (pointCount == 0)
            return true;

        constexpr uint32_t kMinCap = 256u;
        constexpr VkBufferUsageFlags kBDA = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        // --- Position buffer (per-frame) ---
        if (!EnsurePerFrameBuffer<glm::vec3, FRAMES>(
                *m_Device, m_TransientPosBuffer, m_TransientPosCapacity,
                pointCount, kMinCap, "PointPass", kBDA))
            return false;

        // --- Normal buffer (per-frame) ---
        if (!EnsurePerFrameBuffer<glm::vec3, FRAMES>(
                *m_Device, m_TransientNormBuffer, m_TransientNormCapacity,
                pointCount, kMinCap, "PointPass", kBDA))
            return false;

        return true;
    }

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

        // Deferred-destroy persistent point attribute buffers.
        for (auto& [key, entry] : m_PointAttrBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_PointAttrBuffers.clear();

        // Deferred-destroy persistent point radii buffers.
        for (auto& [key, entry] : m_PointRadiiBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_PointRadiiBuffers.clear();

        // Destroy transient buffers.
        for (auto& buf : m_TransientPosBuffer) buf.reset();
        for (auto& buf : m_TransientNormBuffer) buf.reset();

        for (auto& p : m_Pipelines) p.reset();
    }

    // =========================================================================
    // BuildPipeline
    // =========================================================================

    std::unique_ptr<RHI::GraphicsPipeline> PointPass::BuildPipeline(
        VkFormat colorFormat, VkFormat depthFormat, uint32_t mode)
    {
        if (!m_ShaderRegistry)
        {
            Core::Log::Error("PointPass: ShaderRegistry not configured.");
            return nullptr;
        }

        // Select shader pair based on render mode.
        Core::Hash::StringID vertId, fragId;
        if (mode == 0)
        {
            // FlatDisc mode.
            vertId = "Point.FlatDisc.Vert"_id;
            fragId = "Point.FlatDisc.Frag"_id;
        }
        else if (mode == 3)
        {
            // Sphere (impostor) mode — writes gl_FragDepth for correct depth.
            vertId = "Point.Sphere.Vert"_id;
            fragId = "Point.Sphere.Frag"_id;
        }
        else
        {
            // Surfel and EWA modes share the same shader pair.
            vertId = "Point.Surfel.Vert"_id;
            fragId = "Point.Surfel.Frag"_id;
        }

        auto [vertPath, fragPath] = ResolveShaderPaths(*m_ShaderRegistry, vertId, fragId);

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
        // Depth bias to prevent z-fighting between points and mesh surfaces.
        pb.EnableDepthBias(-2.0f, -2.0f);

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
            Core::Log::Error("PointPass: Failed to build pipeline for mode {} (VkResult={})",
                             mode, (int)result.error());
            return nullptr;
        }
        return std::move(*result);
    }

    // =========================================================================
    // AddPasses
    // =========================================================================

    void PointPass::AddPasses(RenderPassContext& ctx)
    {
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0) return;
        if (!m_GeometryStorage) return;

        const uint32_t frameIndex = ctx.FrameIndex;

        // Lazy per-mode pipeline creation;
        bool pipelinesReady = true;
        for (uint32_t i = 0; i < kModeCount; ++i)
        {
            if (!m_Pipelines[i])
            {
                m_Pipelines[i] = BuildPipeline(ctx.SceneColorFormat, ctx.DepthFormat, i);
                if (!m_Pipelines[i])
                {
                    pipelinesReady = false;
                    break;
                }
            }
        }

        if (!pipelinesReady)
        {
            static bool s_Logged = false;
            if (!s_Logged)
            {
                s_Logged = true;
                Core::Log::Error("PointPass: pipeline creation failed. Point rendering will be skipped.");
            }
            return;
        }

        // Per-entity draw command.
        struct DrawInfo
        {
            glm::mat4 Model;
            uint64_t PtrPositions;
            uint64_t PtrNormals;
            uint64_t PtrAttr;        // 0 = use uniform Color; non-zero = per-point BDA colors
            uint32_t VertexCount;
            float    PointSize;
            float    SizeMultiplier;
            uint32_t Color;
            uint32_t Flags;         // bit 0: has per-point colors, bit 1: EWA, bit 2: per-point radii
            uint64_t PtrRadii;      // BDA to per-point float radii (0 = uniform PointSize)
            uint32_t PipelineIndex; // 0=FlatDisc, 1=Surfel, 2=EWA (uses pipeline[1])
        };

        std::vector<DrawInfo> draws;

        // Track which entity keys have active aux buffers this frame.
        std::vector<uint32_t> activeAttrKeys;

        auto& registry = ctx.Scene.GetRegistry();

        // Frustum extraction — CPU-side culling for retained-mode draws.
        const bool cullingEnabled = !ctx.Debug.DisableCulling;
        const Geometry::Frustum frustum = CreateCullingFrustum(
            ctx.CameraProj, ctx.CameraView, cullingEnabled);

        // =================================================================
        // Retained-mode draws via ECS::Point::Component
        // =================================================================
        // Point::Component is populated by lifecycle systems:
        // - MeshViewLifecycle (mesh vertex views)
        // - GraphGeometrySyncSystem (graph nodes)
        {
            auto pointView = registry.view<ECS::Point::Component>();

            for (auto [entity, pt] : pointView.each())
            {
                if (!pt.Geometry.IsValid())
                    continue;

                GeometryGpuData* geo = m_GeometryStorage->GetIfValid(pt.Geometry);
                if (!geo || !geo->GetVertexBuffer())
                    continue;

                const auto& layout = geo->GetLayout();
                if (layout.PositionsSize == 0)
                    continue;

                const uint32_t vertexCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
                if (vertexCount == 0)
                    continue;

                glm::mat4 worldMatrix(1.0f);
                if (auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
                    worldMatrix = wm->Matrix;

                // Frustum cull.
                if (cullingEnabled && !FrustumCullSphere(worldMatrix, geo->GetLocalBoundingSphere(), frustum))
                    continue;

                const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                const uint64_t posAddr = baseAddr + layout.PositionsOffset;

                // Only expose normals to shaders when the component explicitly reports
                // per-point normals and the uploaded layout actually contains them.
                const bool hasValidNormals = pt.HasPerPointNormals && (layout.NormalsSize > 0);
                const uint64_t normAddr = hasValidNormals ? (baseAddr + layout.NormalsOffset) : 0;

                const uint32_t ptColor = GpuColor::PackColorF(
                    pt.Color.r, pt.Color.g, pt.Color.b, pt.Color.a);

                const uint32_t requestedModeIdx = static_cast<uint32_t>(pt.Mode);
                const bool requestedNormalOrientedMode = (requestedModeIdx == 1u || requestedModeIdx == 2u);

                // Surfel/EWA require geometric normals; otherwise render as FlatDisc.
                const uint32_t effectiveModeIdx =
                    (requestedNormalOrientedMode && !hasValidNormals) ? 0u : requestedModeIdx;
                const bool isEWA = (effectiveModeIdx == 2u);

                // Per-point color aux buffer (sourced from legacy components).
                uint64_t attrAddr = 0;
                if (pt.HasPerPointColors)
                {
                    const uint32_t entityKey = static_cast<uint32_t>(entity);
                    const uint32_t* colorData = nullptr;
                    uint32_t colorCount = 0;

                    // Try Graph::Data.CachedNodeColors
                    if (const auto* gd = registry.try_get<ECS::Graph::Data>(entity))
                    {
                        if (!gd->CachedNodeColors.empty() &&
                            gd->CachedNodeColors.size() == vertexCount)
                        {
                            colorData = gd->CachedNodeColors.data();
                            colorCount = vertexCount;
                        }
                    }

                    // Try PointCloud::Data.CachedColors
                    if (!colorData)
                    {
                        if (const auto* pcd = registry.try_get<ECS::PointCloud::Data>(entity))
                        {
                            if (!pcd->CachedColors.empty() &&
                                pcd->CachedColors.size() == vertexCount)
                            {
                                colorData = pcd->CachedColors.data();
                                colorCount = vertexCount;
                            }
                        }
                    }

                    if (colorData)
                    {
                        activeAttrKeys.push_back(entityKey);
                        attrAddr = EnsurePointAttrBuffer(entityKey, colorData, colorCount);
                    }
                }

                // Per-point radii buffer (sourced from Graph::Data or PointCloud::Data).
                uint64_t radiiAddr = 0;
                {
                    const uint32_t entityKey = static_cast<uint32_t>(entity);
                    const float* radiiData = nullptr;
                    uint32_t radiiCount = 0;

                    // Try Graph::Data.CachedNodeRadii
                    if (const auto* gd = registry.try_get<ECS::Graph::Data>(entity))
                    {
                        if (!gd->CachedNodeRadii.empty() &&
                            gd->CachedNodeRadii.size() == vertexCount)
                        {
                            radiiData = gd->CachedNodeRadii.data();
                            radiiCount = vertexCount;
                        }
                    }

                    // Try PointCloud::Data.CachedRadii
                    if (!radiiData)
                    {
                        if (const auto* pcd = registry.try_get<ECS::PointCloud::Data>(entity))
                        {
                            if (!pcd->CachedRadii.empty() &&
                                pcd->CachedRadii.size() == vertexCount)
                            {
                                radiiData = pcd->CachedRadii.data();
                                radiiCount = vertexCount;
                            }
                        }
                    }

                    if (radiiData)
                    {
                        radiiAddr = EnsurePointRadiiBuffer(entityKey + 0x80000000u, radiiData, radiiCount);
                    }
                }

                uint32_t flags = 0;
                if (attrAddr != 0)   flags |= 1u; // has per-point colors
                if (isEWA)          flags |= 2u; // EWA mode
                if (radiiAddr != 0) flags |= 4u; // has per-point radii

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrNormals = normAddr;
                di.PtrAttr = attrAddr;
                di.VertexCount = vertexCount;
                // Clamp point radius to safe world-space range [0.0001, 1.0].
                di.PointSize = ClampPointSize(pt.Size);
                di.SizeMultiplier = pt.SizeMultiplier;
                di.Color = ptColor;
                di.Flags = flags;
                di.PtrRadii = radiiAddr;
                // Pipeline index: FlatDisc=0, Surfel/EWA=1, Sphere=3
                di.PipelineIndex = (effectiveModeIdx == 0u) ? 0u
                                 : (effectiveModeIdx == 3u) ? 3u : 1u;
                draws.push_back(di);
            }
        }

        // Cleanup orphaned point attribute buffers.
        CleanupOrphanedBuffers(*m_Device, m_PointAttrBuffers, activeAttrKeys);

        // =================================================================
        // Transient DebugDraw points (uploaded per-frame via BDA)
        // =================================================================
        if (m_DebugDraw)
        {
            auto debugPoints = m_DebugDraw->GetPoints();
            const uint32_t transientCount = static_cast<uint32_t>(debugPoints.size());

            if (transientCount > 0)
            {
                if (!EnsureTransientBuffers(transientCount, frameIndex))
                {
                    Core::Log::Error("PointPass: Failed to ensure transient buffers.");
                }
                else
                {
                    // Build flat position and normal arrays from PointMarkers.
                    std::vector<glm::vec3> positions;
                    std::vector<glm::vec3> normals;
                    positions.reserve(transientCount);
                    normals.reserve(transientCount);

                    for (const auto& pt : debugPoints)
                    {
                        positions.push_back(pt.Position);
                        normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
                    }

                    m_TransientPosBuffer[frameIndex]->Write(
                        positions.data(), positions.size() * sizeof(glm::vec3));
                    m_TransientNormBuffer[frameIndex]->Write(
                        normals.data(), normals.size() * sizeof(glm::vec3));

                    const uint64_t posAddr = m_TransientPosBuffer[frameIndex]->GetDeviceAddress();
                    const uint64_t normAddr = m_TransientNormBuffer[frameIndex]->GetDeviceAddress();

                    // Use the first point's size and color (uniform for all transient).
                    const auto& firstPt = debugPoints[0];

                    DrawInfo di{};
                    di.Model = glm::mat4(1.0f);
                    di.PtrPositions = posAddr;
                    di.PtrNormals = normAddr;
                    di.PtrAttr = 0;
                    di.VertexCount = transientCount;
                    di.PointSize = firstPt.Size;
                    di.SizeMultiplier = 1.0f;
                    di.Color = firstPt.Color;
                    di.Flags = 0;
                    di.PipelineIndex = 0; // FlatDisc for debug points
                    draws.push_back(di);
                }
            }
        }

        if (draws.empty())
            return;

        // Fetch canonical render targets — point passes render to HDR scene color.
        const RGResourceHandle sceneColor = ctx.Blackboard.Get(RenderResource::SceneColorHDR);
        const RGResourceHandle depth = ctx.Blackboard.Get(RenderResource::SceneDepth);
        if (!sceneColor.IsValid() || !depth.IsValid())
            return;

        const VkDescriptorSet globalSet = ctx.GlobalDescriptorSet;
        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
        const VkExtent2D resolution = ctx.Resolution;

        // Capture pipeline handles for the lambda.
        VkPipeline pipelines[kModeCount];
        VkPipelineLayout pipelineLayout = m_Pipelines[0]->GetLayout(); // Same layout for all modes.
        for (uint32_t i = 0; i < kModeCount; ++i)
            pipelines[i] = m_Pipelines[i]->GetHandle();

        auto capturedDraws = std::make_shared<std::vector<DrawInfo>>(std::move(draws));

        ctx.Graph.AddPass<PassData>("Points",
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
            [globalSet, dynamicOffset, resolution, pipelines, pipelineLayout, capturedDraws]
            (const PassData&, const RGRegistry&, VkCommandBuffer cmd)
            {
                vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                SetViewportScissor(cmd, resolution);

                // Track current pipeline to minimize bind calls.
                uint32_t currentPipelineIdx = ~0u;

                for (const auto& di : *capturedDraws)
                {
                    // Bind pipeline if mode changed.
                    if (di.PipelineIndex != currentPipelineIdx)
                    {
                        currentPipelineIdx = di.PipelineIndex;
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                          pipelines[currentPipelineIdx]);

                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipelineLayout,
                                                0, 1, &globalSet,
                                                1, &dynamicOffset);
                    }

                    PointPushConstants push{};
                    push.Model = di.Model;
                    push.PtrPositions = di.PtrPositions;
                    push.PtrNormals = di.PtrNormals;
                    push.PtrAttr = di.PtrAttr;
                    push.PointSize = di.PointSize;
                    push.SizeMultiplier = di.SizeMultiplier;
                    push.ViewportWidth = static_cast<float>(resolution.width);
                    push.ViewportHeight = static_cast<float>(resolution.height);
                    push.Color = di.Color;
                    push.Flags = di.Flags;
                    push.PtrRadii = di.PtrRadii;

                    vkCmdPushConstants(cmd, pipelineLayout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(push), &push);

                    // Each vertex becomes 6 GPU vertices (billboard quad).
                    vkCmdDraw(cmd, di.VertexCount * 6, 1, 0, 0);
                }
            });
    }

} // namespace Graphics::Passes
