module;

#include <cstdint>
#include <memory>
#include <array>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Point;

import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :ShaderRegistry;
import :DebugDraw;
import Geometry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // PointPass — Unified BDA-based point rendering for all point sources.
    // -------------------------------------------------------------------------
    //
    // Architecture (PLAN.md Phase 4):
    // - Consolidates all point rendering into a single pass:
    //   1. ECS::Point::Component — first-class per-pass typed component.
    //   2. Mesh vertex visualization — MeshVertexView geometry.
    //   3. Standalone point clouds — PointCloudRenderer entities.
    //   4. Graph nodes — ECS::Graph::Data entities.
    //   5. Cloud-backed point clouds — ECS::PointCloud::Data entities.
    //   6. Transient debug points — DebugDraw::GetPoints() API.
    //
    // - Reads vertex positions and normals via BDA from shared device-local
    //   vertex buffers (zero per-frame upload for retained position data).
    // - Vertex shader expands each point into a billboard quad (6 verts).
    // - Multiple render modes via separate pipeline variants:
    //   FlatDisc (camera-facing billboard), Surfel (normal-oriented disc),
    //   EWA (perspective-correct elliptical Gaussian splats).
    //
    // Integration:
    // - Registered in DefaultPipeline as "Points" stage.
    // - Set 0: Camera UBO (shared across all passes).
    // - No set 1 needed — all data comes via BDA push constants.
    // - Push constants: Model + BDA pointers + point config.

    class PointPass final : public IRenderFeature
    {
    public:
        // Configuration
        float SizeMultiplier = 1.0f;

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void SetShaderRegistry(const ShaderRegistry& reg) { m_ShaderRegistry = &reg; }

        // Set the geometry storage for looking up GeometryGpuData.
        void SetGeometryStorage(GeometryPool* pool) { m_GeometryStorage = pool; }

        // Set the debug draw accumulator for transient point markers.
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

        // Pipeline array indexed by Geometry::PointCloud::RenderMode.
        // [0] = FlatDisc, [1] = Surfel, [2] = EWA.
        static constexpr size_t kModeCount = 3;
        std::array<std::unique_ptr<RHI::GraphicsPipeline>, kModeCount> m_Pipelines;

        // --- Transient DebugDraw buffers (per-frame, host-visible, BDA) ---
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();

        // Position buffer: vec3 array [p0, p1, p2, ...]
        std::unique_ptr<RHI::VulkanBuffer> m_TransientPosBuffer[FRAMES];
        uint32_t m_TransientPosCapacity = 0; // in vec3 elements

        // Normal buffer: vec3 array (optional, same count as positions)
        std::unique_ptr<RHI::VulkanBuffer> m_TransientNormBuffer[FRAMES];
        uint32_t m_TransientNormCapacity = 0;

        // Build a graphics pipeline for the given render mode.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat,
            Geometry::PointCloud::RenderMode mode);

        // Ensure transient buffers have enough capacity for the given point count.
        bool EnsureTransientBuffers(uint32_t pointCount, uint32_t frameIndex);
    };
}
