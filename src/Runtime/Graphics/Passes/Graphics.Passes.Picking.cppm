module;

#include <memory>
#include <span>

#include <entt/entt.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.Picking;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import Core;
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
        void SetReadbackBuffers(std::span<std::unique_ptr<RHI::VulkanBuffer>> buffers) { m_ReadbackBuffers = buffers; }

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
        RHI::GraphicsPipeline* m_Pipeline = nullptr; // owned by RenderSystem for now
        std::span<std::unique_ptr<RHI::VulkanBuffer>> m_ReadbackBuffers{}; // one per frame-in-flight
    };
}
