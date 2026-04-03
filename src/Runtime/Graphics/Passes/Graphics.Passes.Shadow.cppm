module;

export module Graphics.Passes.Shadow;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;

import RHI.Descriptors;
import RHI.Device;

export namespace Graphics::Passes
{
    class ShadowPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&,
                        RHI::DescriptorLayout&) override
        {
        }

        void AddPasses(RenderPassContext& ctx) override;
    };
}
