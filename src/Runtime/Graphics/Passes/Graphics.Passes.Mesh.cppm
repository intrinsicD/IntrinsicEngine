module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Mesh;

import :RenderPipeline;
import :Passes.PointCloud;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // MeshRenderPass — Mesh visualization data collection.
    // -------------------------------------------------------------------------
    //
    // Performs CPU-side data collection for entities with both
    // ECS::MeshRenderer::Component and ECS::RenderVisualization::Component:
    //   - Edge cache building (lazily extracts unique edges from collision mesh,
    //     consumed by LinePass via ECS::Line::Component).
    //   - Vertex points → submitted to PointCloudRenderPass (all 4 modes).
    //
    // Wireframe edge rendering is fully owned by LinePass which iterates
    // ECS::Line::Component directly. This pass only builds the edge cache.
    //
    // Face rendering (solid triangles) is the responsibility of SurfacePass.
    //
    // This pass has no GPU resources of its own — it is a data-collection pass.
    //
    // Usage in DefaultPipeline:
    //   Call AddPasses() after PointCloudRenderPass::ResetPoints() and before
    //   PointCloudRenderPass::AddPasses().
    //
    // Dependencies:
    //   SetPointCloudPass() must be called with a non-null pointer for vertex draw.
    //   ctx.GeometryStorage is used to determine mesh topology (triangles vs. lines).

    class MeshRenderPass final : public IRenderFeature
    {
    public:
        // No GPU resources allocated — delegates entirely to shared primitive passes.
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        // Collect mesh visualization data into shared staging buffers.
        // Must be called after PointCloudRenderPass::ResetPoints() and before
        // PointCloudRenderPass::AddPasses().
        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}

        // Set the shared PointCloudRenderPass that accumulates vertex splat data.
        // Pass nullptr to disable vertex point rendering.
        void SetPointCloudPass(PointCloudRenderPass* pass) { m_PointCloudPass = pass; }

        // Inform this pass whether the retained BDA point pass is active.
        // When active, entities with GPU geometry skip the CPU vertex path.
        void SetRetainedPassesActive(bool /*lines*/, bool points)
        {
            m_RetainedPointsActive = points;
        }

    private:
        PointCloudRenderPass* m_PointCloudPass = nullptr;
        bool m_RetainedPointsActive = false;
    };
}
