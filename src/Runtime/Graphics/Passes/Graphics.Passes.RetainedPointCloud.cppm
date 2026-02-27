module;

#include <cstdint>
#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.RetainedPointCloud;

import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :ShaderRegistry;
import Geometry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // RetainedPointCloudRenderPass — BDA-based retained-mode vertex/point rendering.
    // -------------------------------------------------------------------------
    //
    // Architecture:
    // - Iterates mesh entities with ShowVertices + valid GPU geometry.
    // - Reads vertex positions and normals via BDA from the shared device-local
    //   vertex buffer (zero per-frame upload for position data).
    // - Vertex shader expands each point into a billboard quad (6 verts).
    // - Supports FlatDisc and Surfel render modes via push constants.
    //
    // Key difference from PointCloudRenderPass (SSBO-based):
    // - Positions/normals come from GPU-resident vertex buffers via BDA.
    // - Per-entity model matrix is pushed (correct transform without CPU work).
    // - No per-frame SSBO upload for position data.
    //
    // Integration:
    // - Registered in DefaultPipeline after VisualizationCollect.
    // - Set 0: Camera UBO (shared across all passes).
    // - No set 1 needed — all data comes via BDA push constants.
    // - Push constants: Model + BDA pointers + point config.

    class RetainedPointCloudRenderPass final : public IRenderFeature
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

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Lazily-built pipeline (with depth test).
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        // Build the graphics pipeline.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat);
    };
}
