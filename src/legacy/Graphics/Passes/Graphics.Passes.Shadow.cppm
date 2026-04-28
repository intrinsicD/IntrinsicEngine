module;

export module Graphics.Passes.Shadow;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;

import RHI.Descriptors;
import RHI.Device;
import RHI.Pipeline;

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

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }
        void AddPasses(RenderPassContext& ctx) override;

    private:
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // owned by PipelineLibrary
    };
}
