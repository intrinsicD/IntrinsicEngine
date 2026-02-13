module;

#include <memory>
#include <span>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Picking;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import ECS;
import RHI;

export namespace Graphics::Passes
{
    class PickingPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice& device,
                        RHI::DescriptorAllocator&, RHI::DescriptorLayout&) override
        {
            m_Device = &device;
        }

        void SetPipeline(RHI::GraphicsPipeline* p) { m_Pipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

    private:
        struct PickPassData
        {
            RGResourceHandle IdBuffer;
            RGResourceHandle Depth;
        };

        struct PickCopyPassData
        {
            RGResourceHandle IdBuffer;
        };

        RHI::VulkanDevice* m_Device = nullptr; // non-owning
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // owned by pipeline library / engine
    };
}
