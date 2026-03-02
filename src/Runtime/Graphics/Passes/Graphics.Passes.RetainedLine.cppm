module;

#include <cstdint>
#include <memory>
#include <unordered_map>
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
    // - Edge index pairs are uploaded ONCE to a persistent device-local buffer
    //   when wireframe is first enabled, then read via BDA each frame.
    // - Vertex shader expands each edge into a screen-space quad (6 verts) for
    //   thick anti-aliased lines, using the model matrix from push constants.
    //
    // Key properties:
    // - Positions AND edges both come from GPU-resident buffers via BDA.
    // - Zero per-frame CPU→GPU edge uploads (fully retained).
    // - Per-entity model matrix is pushed, so wireframe transforms correctly.
    // - Edge indices are compact (8 bytes per edge vs 32 bytes per LineSegment).
    //
    // Integration:
    // - Registered in DefaultPipeline after VisualizationCollect.
    // - Set 0: Camera UBO (shared across all passes).
    // - Push constants: Model matrix + BDA pointers (positions, edges) + line config.

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
        const ShaderRegistry* m_ShaderRegistry = nullptr;
        GeometryPool* m_GeometryStorage = nullptr;

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Lazily-built pipeline (with depth test, no depth write).
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;

        // Per-entity persistent edge buffer: uploaded once, read via BDA each frame.
        struct RetainedEdgeEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t EdgeCount = 0;
            uint32_t SourceGeometryIndex = 0; // Tracks source geometry changes
        };

        // Entity ID → persistent edge buffer.
        // Mesh entities: keyed by entt::entity value.
        // Graph entities: keyed by entt::entity value (separate namespace via bit 31).
        std::unordered_map<uint32_t, RetainedEdgeEntry> m_EdgeBuffers;

        // Create or update a persistent edge buffer for an entity.
        // sourceGeoIdx: geometry handle index of the source vertex buffer. When this
        //               changes (e.g. graph re-layout), the edge buffer is recreated
        //               even if the edge count stays the same.
        // Returns the BDA device address, or 0 on failure.
        uint64_t EnsureEdgeBuffer(uint32_t entityKey,
                                  const void* edgeData,
                                  uint32_t edgeCount,
                                  uint32_t sourceGeoIdx);

        // Build the graphics pipeline.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat);
    };
}
