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
    // GraphRenderPass — GPU graph rendering via composited primitive passes.
    // -------------------------------------------------------------------------
    //
    // Renders entities with ECS::GraphRenderer::Component:
    //   - Nodes rendered as point splats via PointCloudRenderPass (all 4 modes).
    //   - Edges rendered as anti-aliased thick lines via LineRenderPass
    //     (submitted to DebugDraw accumulator in RenderPassContext).
    //
    // This pass has no GPU resources of its own — it is a data-collection pass
    // that feeds the shared PointCloudRenderPass and LineRenderPass staging buffers.
    // GPU drawing is performed by those passes after all collectors run.
    //
    // Usage in DefaultPipeline:
    //   Call AddPasses() before PointCloudRenderPass.AddPasses() so that node data
    //   is accumulated in the staging buffers before the GPU draw pass is added.
    //
    // Dependencies:
    //   SetPointCloudPass() must be called before AddPasses() (non-null for node draw).
    //   DebugDraw is read from ctx.DebugDrawPtr each frame for edge submission.

    class GraphRenderPass final : public IRenderFeature
    {
    public:
        // No GPU resources allocated — delegates entirely to shared primitive passes.
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        // Collect graph entity data into shared staging buffers.
        // Must be called after PointCloudRenderPass::ResetPoints() and before
        // PointCloudRenderPass::AddPasses() / LineRenderPass::AddPasses().
        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}

        // Set the shared PointCloudRenderPass that accumulates node splat data.
        // Pass nullptr to disable node rendering (edges still submitted to DebugDraw).
        void SetPointCloudPass(PointCloudRenderPass* pass) { m_PointCloudPass = pass; }

    private:
        PointCloudRenderPass* m_PointCloudPass = nullptr;
    };
}
