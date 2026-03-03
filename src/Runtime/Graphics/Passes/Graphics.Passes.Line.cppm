module;

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Line;

import :RenderPipeline;
import :RenderGraph;
import :Geometry;
import :DebugDraw;
import :ShaderRegistry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // LinePass — Unified BDA-based line rendering for all line sources.
    // -------------------------------------------------------------------------
    //
    // Iterates ECS::Line::Component for retained-mode edge rendering and
    // pulls from DebugDraw for transient lines. All data is read via BDA
    // push constants.
    //
    // Retained sources (via ECS::Line::Component, populated by ComponentMigration):
    // - Mesh wireframe edges (MeshEdgeView BDA index buffer, or CachedEdges fallback)
    // - Graph edges (CachedEdgePairs from GraphGeometrySyncSystem)
    // - Standalone line entities (future)
    //
    // Transient sources:
    // - DebugDraw lines (octree overlays, bounds, contact manifolds, etc.)
    //   uploaded per-frame to host-visible BDA buffers.
    //
    // Architecture:
    // - Vertex positions + edge indices read via BDA from push constants.
    // - Two pipelines: depth-tested and overlay (no depth test).
    // - Both retained and transient draws are dispatched to the correct
    //   pipeline based on their overlay flag.
    // - Per-entity model matrix pushed for retained draws; identity for transient.
    // - Set 0: Camera UBO (shared across all passes).
    // - Push constants: Model + BDA pointers + line config (104 bytes).
    //
    // Registered in DefaultPipeline. Gated by FeatureRegistry ("LinePass"_id).

    class LinePass final : public IRenderFeature
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

        // Set the DebugDraw accumulator for transient line data.
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
        const ShaderRegistry* m_ShaderRegistry = nullptr;
        GeometryPool* m_GeometryStorage = nullptr;
        DebugDraw* m_DebugDraw = nullptr;

        // Global camera layout (set 0) — borrowed.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Lazily-built pipelines.
        std::unique_ptr<RHI::GraphicsPipeline> m_Pipeline;        // depth test enabled
        std::unique_ptr<RHI::GraphicsPipeline> m_OverlayPipeline; // depth test disabled

        // Per-entity persistent edge buffer: uploaded once, read via BDA each frame.
        struct RetainedEdgeEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t EdgeCount = 0;
            uint32_t SourceGeometryIndex = 0; // Tracks source geometry changes
        };

        // Per-entity persistent edge attribute buffer (packed ABGR per edge).
        struct RetainedEdgeAuxEntry
        {
            std::unique_ptr<RHI::VulkanBuffer> Buffer;
            uint32_t EdgeCount = 0;
        };

        // Entity ID → persistent edge buffer.
        std::unordered_map<uint32_t, RetainedEdgeEntry> m_EdgeBuffers;

        // Entity ID → persistent edge attribute buffer.
        std::unordered_map<uint32_t, RetainedEdgeAuxEntry> m_EdgeAuxBuffers;

        // --- Transient DebugDraw buffers (per-frame, host-visible, BDA) ---
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();

        // Position buffer: flat vec3 array [start0, end0, start1, end1, ...]
        std::unique_ptr<RHI::VulkanBuffer> m_TransientPosBuffer[FRAMES];
        uint32_t m_TransientPosCapacity = 0; // in vec3 elements (2 per segment)

        // Edge pair buffer: identity mapping {0,1}, {2,3}, {4,5}, ...
        std::unique_ptr<RHI::VulkanBuffer> m_TransientEdgeBuffer;
        uint32_t m_TransientEdgeCapacity = 0; // in edge pairs (1 per segment)

        // Per-edge color buffer: packed ABGR per segment
        std::unique_ptr<RHI::VulkanBuffer> m_TransientColorBuffer[FRAMES];
        uint32_t m_TransientColorCapacity = 0; // in segments

        // Create or update a persistent edge buffer for an entity.
        uint64_t EnsureEdgeBuffer(uint32_t entityKey,
                                  const void* edgeData,
                                  uint32_t edgeCount,
                                  uint32_t sourceGeoIdx);

        // Create or update a persistent per-edge attribute buffer for an entity.
        uint64_t EnsureEdgeAuxBuffer(uint32_t entityKey,
                                     const uint32_t* colorData,
                                     uint32_t edgeCount);

        // Build a graphics pipeline (depth test on or off).
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(
            VkFormat colorFormat, VkFormat depthFormat, bool enableDepthTest);

        // Ensure transient buffers have enough capacity for the given segment count.
        bool EnsureTransientBuffers(uint32_t segmentCount, uint32_t frameIndex);
    };
}
