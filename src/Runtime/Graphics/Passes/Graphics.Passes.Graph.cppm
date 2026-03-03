module;

#include <memory>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Graph;

import :RenderPipeline;
import RHI;

export namespace Graphics::Passes
{
    // -------------------------------------------------------------------------
    // GraphRenderPass — Graph visualization data collection (legacy shell).
    // -------------------------------------------------------------------------
    //
    // Node point rendering is now handled by PointPass (BDA retained-mode).
    // Edge rendering is handled by LinePass (BDA retained-mode).
    //
    // This pass is a no-op shell retained for pipeline compatibility.
    // Scheduled for deletion in TODO §1.5.

    class GraphRenderPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override {}

        void AddPasses(RenderPassContext& ctx) override;

        void Shutdown() override {}
    };
}
