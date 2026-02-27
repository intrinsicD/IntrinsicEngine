module;

#include <cstdint>
#include <memory>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.RetainedLine;

import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :ShaderRegistry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // RetainedLineRenderPass — BDA-based retained-mode wireframe rendering.
    // -------------------------------------------------------------------------
    //
    // Architecture:
    // - Iterates mesh entities with ShowWireframe + valid GPU geometry.
    // - Reads vertex positions via BDA from the shared device-local vertex buffer.
    // - Uploads edge index pairs (from CachedEdges) to a per-frame SSBO.
    // - Vertex shader expands each edge into a screen-space quad (6 verts) for
    //   thick anti-aliased lines, using the model matrix from push constants.
    //
    // Key difference from LineRenderPass (SSBO-based):
    // - Positions come from GPU-resident vertex buffers via BDA, not CPU-transformed.
    // - Per-entity model matrix is pushed, so wireframe transforms correctly.
    // - Edge indices are compact (8 bytes per edge vs 32 bytes per LineSegment).
    //
    // Integration:
    // - Registered in DefaultPipeline after VisualizationCollect.
    // - Set 0: Camera UBO (shared across all passes).
    // - Set 1: Edge index SSBO (pairs of uint32 vertex indices).
    // - Push constants: Model matrix + BDA position pointer + line config.

    class RetainedLineRenderPass final : public IRenderFeature
    {
    public:
        // Configuration
        float LineWidth = 2.0f; // pixels (screen-space width)

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

        // Descriptor set layout for edge SSBO (set 1, binding 0).
        VkDescriptorSetLayout m_EdgeSetLayout = VK_NULL_HANDLE;

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Per-frame descriptor sets and SSBOs for edge index data.
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        VkDescriptorSet m_EdgeDescSets[FRAMES] = {};
        std::unique_ptr<RHI::VulkanBuffer> m_EdgeBuffers[FRAMES];
        uint32_t m_EdgeBufferCapacity = 0; // in edge pairs

        // Lazily-built pipeline (with depth test, no depth write).
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        // Build the graphics pipeline.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat);
    };
}
