module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Graph;

import :RenderPipeline;
import :Passes.PointCloud;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // GraphRenderPass — CPU-side node data collection for graph rendering.
    // -------------------------------------------------------------------------
    //
    // Submits graph node positions to PointCloudRenderPass as a CPU fallback.
    // Edge rendering is fully owned by LinePass which iterates
    // ECS::Line::Component directly — no edge submission here.
    //
    // When retained-mode point rendering is active, entities with valid
    // GpuGeometry are skipped — PointPass handles them
    // via BDA.
    //
    // Usage in DefaultPipeline:
    //   Call AddPasses() before PointCloudRenderPass.AddPasses() so that node data
    //   is accumulated in the staging buffers before the GPU draw pass is added.
    //
    // Dependencies:
    //   SetPointCloudPass() must be called before AddPasses() (non-null for node draw).

    class GraphRenderPass final : public IRenderFeature
    {
    public:
        // No GPU resources allocated — delegates entirely to shared primitive passes.
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        // Collect graph node data into shared staging buffers.
        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}

        // Set the shared PointCloudRenderPass that accumulates node splat data.
        void SetPointCloudPass(PointCloudRenderPass* pass) { m_PointCloudPass = pass; }

        // When point pass is active, entities with valid GpuGeometry are skipped.
        void SetRetainedPassesActive(bool /*lines*/, bool points)
        {
            m_RetainedPointsActive = points;
        }

    private:
        PointCloudRenderPass* m_PointCloudPass = nullptr;
        bool m_RetainedPointsActive = false;
    };
}
