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
        void SetMeshPickPipeline(RHI::GraphicsPipeline* p) { m_MeshPickPipeline = p; }
        void SetLinePickPipeline(RHI::GraphicsPipeline* p) { m_LinePickPipeline = p; }
        void SetPointPickPipeline(RHI::GraphicsPipeline* p) { m_PointPickPipeline = p; }

        void AddPasses(RenderPassContext& ctx) override;

    private:
        struct PickPassData
        {
            RGResourceHandle IdBuffer;
            RGResourceHandle PrimIdBuffer;
            RGResourceHandle Depth;
        };

        struct PickCopyPassData
        {
            RGResourceHandle IdBuffer;
            RGResourceHandle PrimIdBuffer;
        };

        RHI::VulkanDevice* m_Device = nullptr; // non-owning
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // legacy single-output (fallback)
        RHI::GraphicsPipeline* m_MeshPickPipeline = nullptr;
        RHI::GraphicsPipeline* m_LinePickPipeline = nullptr;
        RHI::GraphicsPipeline* m_PointPickPipeline = nullptr;
    };
}
