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
    // MeshRenderPass — Mesh visualization overlay via composited primitive passes.
    // -------------------------------------------------------------------------
    //
    // Renders the non-face visualization overlays for entities with both
    // ECS::MeshRenderer::Component and ECS::RenderVisualization::Component:
    //   - Wireframe edges  → submitted to DebugDraw accumulator → LineRenderPass.
    //   - Vertex points    → submitted to PointCloudRenderPass (all 4 modes).
    //
    // Face rendering (solid triangles) is the responsibility of ForwardPass, which
    // runs before this pass in the DefaultPipeline.  Together, ForwardPass (faces)
    // + MeshRenderPass (wireframe + vertices) constitute the complete mesh pass.
    //
    // This pass has no GPU resources of its own — it is a data-collection pass
    // that feeds the shared PointCloudRenderPass and LineRenderPass staging buffers.
    // GPU drawing is performed by those passes after all collectors run.
    //
    // Usage in DefaultPipeline:
    //   Call AddPasses() after PointCloudRenderPass::ResetPoints() and before
    //   PointCloudRenderPass::AddPasses() / LineRenderPass::AddPasses().
    //
    // Dependencies:
    //   SetPointCloudPass() must be called with a non-null pointer for vertex draw.
    //   DebugDraw is read from ctx.DebugDrawPtr each frame for wireframe lines.
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
        // PointCloudRenderPass::AddPasses() / LineRenderPass::AddPasses().
        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}

        // Set the shared PointCloudRenderPass that accumulates vertex splat data.
        // Pass nullptr to disable vertex point rendering.
        void SetPointCloudPass(PointCloudRenderPass* pass) { m_PointCloudPass = pass; }

    private:
        PointCloudRenderPass* m_PointCloudPass = nullptr;
    };
}
