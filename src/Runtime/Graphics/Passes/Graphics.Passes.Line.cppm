module;

#include <cstdint>
#include <memory>
#include <vector>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Line;

import :RenderPipeline;
import :RenderGraph;
import :DebugDraw;
import :ShaderRegistry;
import Core.Hash;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // LineRenderPass — GPU thick-line rendering for debug visualization.
    // -------------------------------------------------------------------------
    //
    // Architecture:
    // - Consumes line segments from DebugDraw (CPU-side accumulator).
    // - Uploads to a host-visible SSBO each frame.
    // - Vertex shader expands each segment into a screen-space quad (6 verts).
    // - Fragment shader applies smooth anti-aliased edges.
    // - Two sub-passes: depth-tested (normal) and overlay (no depth test).
    //
    // Integration:
    // - Registered in DefaultPipeline after SelectionOutline, before DebugView.
    // - Gated by FeatureRegistry ("LineRenderPass"_id).
    // - Reads camera UBO from global descriptor set (set 0).
    // - Line SSBO bound at set 1.
    //
    class LineRenderPass final : public IRenderFeature
    {
    public:
        // Configuration
        float LineWidth = 2.0f; // pixels (screen-space width)

        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator& descriptorPool,
                        RHI::DescriptorLayout& globalLayout) override;

        void SetShaderRegistry(const ShaderRegistry& reg) { m_ShaderRegistry = &reg; }
        void SetDebugDraw(DebugDraw* dd) { m_DebugDraw = dd; }

        void AddPasses(RenderPassContext& ctx) override;
        void Shutdown() override;

    private:
        struct LinePassData
        {
            RGResourceHandle Color;
            RGResourceHandle Depth;
        };

        RHI::VulkanDevice* m_Device = nullptr;
        RHI::DescriptorAllocator* m_DescriptorPool = nullptr;
        const ShaderRegistry* m_ShaderRegistry = nullptr;
        DebugDraw* m_DebugDraw = nullptr;

        // Descriptor set layout for the line SSBO (set 1, binding 0).
        VkDescriptorSetLayout m_LineSetLayout = VK_NULL_HANDLE;

        // Global camera layout (set 0) — borrowed from GlobalResources.
        VkDescriptorSetLayout m_GlobalSetLayout = VK_NULL_HANDLE;

        // Per-frame descriptor sets (persistent, updated each frame).
        static constexpr uint32_t FRAMES = RHI::VulkanDevice::GetFramesInFlight();
        VkDescriptorSet m_DepthLineSet[FRAMES] = {};
        VkDescriptorSet m_OverlayLineSet[FRAMES] = {};

        // Per-frame host-visible SSBOs for line data upload.
        std::unique_ptr<RHI::VulkanBuffer> m_DepthLineBuffer[FRAMES];
        std::unique_ptr<RHI::VulkanBuffer> m_OverlayLineBuffer[FRAMES];
        uint32_t m_DepthLineBufferCapacity = 0;   // in segments
        uint32_t m_OverlayLineBufferCapacity = 0;  // in segments

        // Lazily-built pipelines (need swapchain format).
        std::unique_ptr<RHI::GraphicsPipeline> m_DepthPipeline;    // depth test enabled
        std::unique_ptr<RHI::GraphicsPipeline> m_OverlayPipeline;  // depth test disabled

        // Ensure SSBO has enough capacity. Returns false on failure.
        bool EnsureBuffer(std::unique_ptr<RHI::VulkanBuffer> buffers[FRAMES],
                          uint32_t& capacity, uint32_t requiredSegments);

        // Build a graphics pipeline for line rendering.
        std::unique_ptr<RHI::GraphicsPipeline> BuildPipeline(VkFormat colorFormat, VkFormat depthFormat,
                                                              bool enableDepthTest);

        // Record draw commands for a set of lines.
        void RecordDraw(VkCommandBuffer cmd, RHI::GraphicsPipeline* pipeline,
                        VkDescriptorSet lineSet, VkDescriptorSet globalSet,
                        uint32_t dynamicOffset, VkExtent2D extent,
                        uint32_t lineCount);
    };
}
