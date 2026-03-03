module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Mesh;

import :RenderPipeline;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // MeshRenderPass — Mesh wireframe edge cache building.
    // -------------------------------------------------------------------------
    //
    // Performs CPU-side edge cache construction for entities with both
    // ECS::MeshRenderer::Component and ECS::RenderVisualization::Component:
    //   - Lazily extracts unique edges from collision mesh (consumed by LinePass
    //     via ECS::Line::Component / ComponentMigration).
    //
    // Vertex point rendering is now handled by PointPass (BDA retained-mode).
    // Wireframe edge rendering is handled by LinePass (BDA retained-mode).
    //
    // Face rendering (solid triangles) is the responsibility of SurfacePass.
    //
    // This pass has no GPU resources of its own — it is a data-collection pass.
    //
    // Usage in DefaultPipeline:
    //   Call AddPasses() during VisualizationCollect stage.
    //
    // Dependencies:
    //   ctx.GeometryStorage is used to determine mesh topology (triangles vs. lines).

    class MeshRenderPass final : public IRenderFeature
    {
    public:
        // No GPU resources allocated — edge cache building only.
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        // Build edge caches for mesh entities with ShowWireframe enabled.
        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}
    };
}
