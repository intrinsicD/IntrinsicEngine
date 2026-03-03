module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Graph;

import :RenderPipeline;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // GraphRenderPass — Stub (Phase 5 deletion candidate).
    // -------------------------------------------------------------------------
    //
    // Graph node rendering is now handled by PointPass via
    // ECS::Point::Component (Phase 4). Graph edge rendering is handled by
    // LinePass via ECS::Line::Component. This pass is retained as a stub
    // for interface compatibility until Phase 5 dead code deletion.

    class GraphRenderPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        void AddPasses(RenderPassContext&) override {}

        void Shutdown() override {}

        // Legacy setters — retained for interface compatibility during Phase 5.
        void SetPointCloudPass(void*) {}
        void SetRetainedPassesActive(bool, bool) {}
    };
}
