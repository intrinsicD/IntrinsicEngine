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

module Graphics:Passes.Point.Impl;

import :Passes.Point;
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

    // Push constants layout (matches point_flatdisc.vert/frag and point_surfel.vert/frag).
    struct PointPushConstants
    {
        glm::mat4 Model;           // 64 bytes, offset  0
        uint64_t  PtrPositions;    //  8 bytes, offset 64
        uint64_t  PtrNormals;      //  8 bytes, offset 72
        uint64_t  PtrAux;          //  8 bytes, offset 80 — per-point packed ABGR colors (0 = uniform Color)
        float     PointSize;       //  4 bytes, offset 88
        float     SizeMultiplier;  //  4 bytes, offset 92
        float     ViewportWidth;   //  4 bytes, offset 96
        float     ViewportHeight;  //  4 bytes, offset 100
        uint32_t  Color;           //  4 bytes, offset 104
        uint32_t  Flags;           //  4 bytes, offset 108 — bit 0: has per-point colors, bit 1: EWA mode
        uint32_t  _pad0;           //  4 bytes, offset 112
        uint32_t  _pad1;           //  4 bytes, offset 116
    };
    static_assert(sizeof(PointPushConstants) == 120);

    // =========================================================================
    // EnsurePointAuxBuffer
    // =========================================================================

    uint64_t PointPass::EnsurePointAuxBuffer(
        uint32_t entityKey,
        const uint32_t* colorData,
        uint32_t pointCount)
    {
        auto it = m_PointAuxBuffers.find(entityKey);

        // Buffer exists and count matches — update data in-place.
        if (it != m_PointAuxBuffers.end() && it->second.PointCount == pointCount && it->second.Buffer)
        {
            it->second.Buffer->Write(colorData, static_cast<size_t>(pointCount) * sizeof(uint32_t));
            return it->second.Buffer->GetDeviceAddress();
        }

        // Need to create or recreate (count changed).
        if (it != m_PointAuxBuffers.end() && it->second.Buffer)
        {
            m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
            it->second.PointCount = 0;
        }

        const VkDeviceSize size = static_cast<VkDeviceSize>(pointCount) * sizeof(uint32_t);
        auto buf = std::make_unique<RHI::VulkanBuffer>(
            *m_Device, size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU);

        if (!buf->GetMappedData())
        {
            Core::Log::Error("PointPass: Failed to allocate point aux buffer ({} bytes)", size);
            return 0;
        }

        buf->Write(colorData, static_cast<size_t>(size));
        const uint64_t addr = buf->GetDeviceAddress();

        m_PointAuxBuffers[entityKey] = { std::move(buf), pointCount };
        return addr;
    }

    // =========================================================================
    // EnsureTransientBuffers
    // =========================================================================

    bool PointPass::EnsureTransientBuffers(uint32_t pointCount, uint32_t frameIndex)
    {
        if (pointCount == 0)
            return true;

        // --- Position buffer (per-frame) ---
        if (pointCount > m_TransientPosCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientPosCapacity);
            while (newCap < pointCount) newCap *= 2;

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
                    Core::Log::Error("PointPass: Failed to allocate transient position buffer ({} bytes)", size);
                    return false;
                }
            }
            m_TransientPosCapacity = newCap;
        }

        // --- Normal buffer (per-frame) ---
        if (pointCount > m_TransientNormCapacity)
        {
            uint32_t newCap = std::max(256u, m_TransientNormCapacity);
            while (newCap < pointCount) newCap *= 2;

            const VkDeviceSize size = static_cast<VkDeviceSize>(newCap) * sizeof(glm::vec3);
            for (uint32_t i = 0; i < FRAMES; ++i)
            {
                if (m_TransientNormBuffer[i])
                    m_Device->SafeDestroy([old = std::move(m_TransientNormBuffer[i])]() {});

                m_TransientNormBuffer[i] = std::make_unique<RHI::VulkanBuffer>(
                    *m_Device, size,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU);

                if (!m_TransientNormBuffer[i]->GetMappedData())
                {
                    Core::Log::Error("PointPass: Failed to allocate transient normal buffer ({} bytes)", size);
                    return false;
                }
            }
            m_TransientNormCapacity = newCap;
        }

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

        // Deferred-destroy persistent point aux buffers.
        for (auto& [key, entry] : m_PointAuxBuffers)
        {
            if (entry.Buffer)
                m_Device->SafeDestroy([old = std::move(entry.Buffer)]() {});
        }
        m_PointAuxBuffers.clear();

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

        // Lazy per-mode pipeline creation.
        bool pipelinesReady = true;
        for (uint32_t i = 0; i < kModeCount; ++i)
        {
            if (!m_Pipelines[i])
            {
                m_Pipelines[i] = BuildPipeline(ctx.SwapchainFormat, ctx.DepthFormat, i);
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
            uint64_t PtrAux;        // 0 = use uniform Color; non-zero = per-point BDA colors
            uint32_t VertexCount;
            float    PointSize;
            float    SizeMultiplier;
            uint32_t Color;
            uint32_t Flags;         // bit 0: has per-point colors, bit 1: EWA mode
            uint32_t PipelineIndex; // 0=FlatDisc, 1=Surfel, 2=EWA (uses pipeline[1])
        };

        std::vector<DrawInfo> draws;

        // Track which entity keys have active aux buffers this frame.
        std::vector<uint32_t> activeAuxKeys;

        auto& registry = ctx.Scene.GetRegistry();

        // -----------------------------------------------------------------
        // Frustum extraction — CPU-side culling for retained-mode draws.
        // -----------------------------------------------------------------
        const bool cullingEnabled = !ctx.Debug.DisableCulling;
        const Geometry::Frustum frustum = cullingEnabled
            ? Geometry::Frustum::CreateFromMatrix(ctx.CameraProj * ctx.CameraView)
            : Geometry::Frustum{};

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

                GeometryGpuData* geo = m_GeometryStorage->GetUnchecked(pt.Geometry);
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
                uint64_t auxAddr = 0;
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
                        activeAuxKeys.push_back(entityKey);
                        auxAddr = EnsurePointAuxBuffer(entityKey, colorData, colorCount);
                    }
                }

                uint32_t flags = 0;
                if (auxAddr != 0) flags |= 1u; // has per-point colors
                if (isEWA)        flags |= 2u; // EWA mode

                DrawInfo di{};
                di.Model = worldMatrix;
                di.PtrPositions = posAddr;
                di.PtrNormals = normAddr;
                di.PtrAux = auxAddr;
                di.VertexCount = vertexCount;
                // Clamp point radius to safe world-space range [0.0001, 1.0].
                di.PointSize = std::clamp(pt.Size, 0.0001f, 1.0f);
                di.SizeMultiplier = pt.SizeMultiplier;
                di.Color = ptColor;
                di.Flags = flags;
                di.PipelineIndex = (effectiveModeIdx == 0u) ? 0u : 1u; // FlatDisc=0, Surfel/EWA=1
                draws.push_back(di);
            }
        }

        // Cleanup orphaned aux buffers.
        if (!m_PointAuxBuffers.empty())
        {
            std::sort(activeAuxKeys.begin(), activeAuxKeys.end());

            for (auto it = m_PointAuxBuffers.begin(); it != m_PointAuxBuffers.end(); )
            {
                if (!std::binary_search(activeAuxKeys.begin(), activeAuxKeys.end(), it->first))
                {
                    if (it->second.Buffer)
                        m_Device->SafeDestroy([old = std::move(it->second.Buffer)]() {});
                    it = m_PointAuxBuffers.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

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
                    di.PtrAux = 0;
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

        // Fetch render targets.
        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
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
                data.Color = builder.WriteColor(backbuffer, colorInfo);

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
                    push.PtrAux = di.PtrAux;
                    push.PointSize = di.PointSize;
                    push.SizeMultiplier = di.SizeMultiplier;
                    push.ViewportWidth = static_cast<float>(resolution.width);
                    push.ViewportHeight = static_cast<float>(resolution.height);
                    push.Color = di.Color;
                    push.Flags = di.Flags;

                    vkCmdPushConstants(cmd, pipelineLayout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0, sizeof(push), &push);

                    // Each vertex becomes 6 GPU vertices (billboard quad).
                    vkCmdDraw(cmd, di.VertexCount * 6, 1, 0, 0);
                }
            });
    }

} // namespace Graphics::Passes
