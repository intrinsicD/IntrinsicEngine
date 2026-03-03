module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Mesh;

import :RenderPipeline;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // MeshRenderPass — Mesh visualization data collection (edge cache only).
    // -------------------------------------------------------------------------
    //
    // Performs CPU-side edge cache building for entities with both
    // ECS::MeshRenderer::Component and ECS::RenderVisualization::Component.
    // Edge cache is consumed by LinePass via ECS::Line::Component.
    //
    // Vertex point rendering is now handled by PointPass via
    // ECS::Point::Component (Phase 4). This pass no longer submits
    // vertex data to PointCloudRenderPass.
    //
    // Face rendering (solid triangles) is the responsibility of SurfacePass.
    //
    // This pass has no GPU resources of its own — it is a data-collection pass.

    class MeshRenderPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}

        // Legacy setters — retained for interface compatibility during Phase 5.
        void SetPointCloudPass(void*) {}
        void SetRetainedPassesActive(bool, bool) {}
    };
}
