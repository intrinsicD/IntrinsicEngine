module;

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics.Passes.Point;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Geometry;
import Graphics.DebugDraw;
import Graphics.ShaderRegistry;
import Geometry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // PointPass — Unified BDA-based point rendering for all point sources.
    // -------------------------------------------------------------------------
    //
    // Consolidates all point rendering in the three-pass architecture:
    // - Mesh vertex visualization (from ECS::Point::Component via MeshVertexView)
    // - Graph node rendering (from ECS::Point::Component via GraphGeometrySyncSystem)
    // - Standalone/preloaded point cloud rendering (from ECS::Point::Component via PointCloudGeometrySyncSystem)
    // - Cloud-backed point clouds (from ECS::Point::Component via PointCloudGeometrySyncSystem)
    // - Transient debug point markers (from DebugDraw::GetPoints())
    //
    // Architecture:
    // - Iterates ECS::Point::Component for retained-mode draws.
    // - Pulls from DebugDraw for transient point markers.
    // - Reads positions/normals via BDA from shared device-local vertex buffers.
    // - Per-mode pipelines: FlatDisc (camera-facing), Surfel (normal-oriented + EWA).
    // - Per-point attributes (colors, radii) via PtrAttr BDA channel.
    // - Set 0: Camera UBO (shared across all passes).
    // - Push constants: Model + BDA pointers + point config (120 bytes).
    //
    // Registered in DefaultPipeline. Gated by FeatureRegistry ("PointPass"_id).

    class PointPass final : public IRenderFeature
    {
    public:
        // Configuration
        float SizeMultiplier = 1.0f;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void SetShaderRegistry(const ShaderRegistry& shaderRegistry) { m_ShaderRegistry = &shaderRegistry; }

        // Set the geometry storage for looking up GeometryGpuData.
        void SetGeometryStorage(GeometryPool* pool) { m_GeometryStorage = pool; }

        // Set the DebugDraw accumulator for transient point data.
        void SetDebugDraw(DebugDraw* dd) { m_DebugDraw = dd; }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;

    private:
        struct PassData
        {
            RGResourceHandle Color;
            RGResourceHandle Depth;
        };

        RHI::VulkanDevice* m_Device = nullptr;
        RHI::DescriptorAllocator* m_DescriptorPool = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;
        GeometryPool* m_GeometryStorage = nullptr;
        DebugDraw* m_DebugDraw = nullptr;

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Per-mode pipelines (indexed by PointRenderMode).
        // [0] = FlatDisc, [1] = Surfel, [2] = EWA (surfel shader + flag), [3] = Sphere.
        static constexpr uint32_t kModeCount = 4;
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipelines[kModeCount];

        // Per-entity persistent per-point attribute buffer.
        struct RetainedBufferEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t Count = 0;
        };

        // Per-entity persistent per-point color attribute buffer (packed ABGR per point).
        std::unordered_map<uint32_t, RetainedBufferEntry> m_PointAttrBuffers;

        // Per-entity persistent per-point radii buffer (float per point).
        std::unordered_map<uint32_t, RetainedBufferEntry> m_PointRadiiBuffers;

        // --- Transient DebugDraw buffers (per-frame, host-visible, BDA) ---
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();

        // Position buffer: vec3 per point.
        std::unique_ptr<RHI::VulkanBuffer> m_TransientPosBuffer[FRAMES];
        uint32_t m_TransientPosCapacity = 0;

        // Normal buffer: vec3 per point.
        std::unique_ptr<RHI::VulkanBuffer> m_TransientNormBuffer[FRAMES];
        uint32_t m_TransientNormCapacity = 0;

        // Build a graphics pipeline for a specific render mode.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat, uint32_t mode);

        // Create or update a persistent per-point color attribute buffer.
        uint64_t EnsurePointAttrBuffer(uint32_t entityKey,
                                       const uint32_t* colorData,
                                       uint32_t pointCount);

        // Create or update a persistent per-point radii buffer.
        uint64_t EnsurePointRadiiBuffer(uint32_t entityKey,
                                        const float* radiiData,
                                        uint32_t pointCount);

        // Ensure transient buffers have enough capacity.
        bool EnsureTransientBuffers(uint32_t pointCount, uint32_t frameIndex);
    };
}
