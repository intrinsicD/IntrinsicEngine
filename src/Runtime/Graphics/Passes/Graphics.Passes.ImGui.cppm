module;

#include "RHI.Vulkan.hpp"

export module Graphics:Passes.ImGui;

import :RenderPipeline;
import :RenderGraph;
import Core.Hash;
import RHI;
import Interface;

using namespace Core::Hash;

export namespace Graphics::Passes
{
    class ImGuiPass final : public IRenderFeature
    {
    public:
        void Initialize(RHI::VulkanDevice&,
                        RHI::DescriptorAllocator&, RHI::DescriptorLayout&) override
        {
            // Stateless.
        }

        void AddPasses(RenderPassContext& ctx) override
        {
            struct Data
            {
                RGResourceHandle Backbuffer;
            };

            ctx.Graph.AddPass<Data>("ImGuiPass",
                                    [&](Data& data, RGBuilder& builder)
                                    {
                                        const RGResourceHandle bb = ctx.Blackboard.Get("Backbuffer"_id);
                                        if (!bb.IsValid())
                                            return;

                                        RGAttachmentInfo info{VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE};
                                        data.Backbuffer = builder.WriteColor(bb, info);
                                    },
                                    [](const Data&, const RGRegistry&, VkCommandBuffer cmd)
                                    {
                                        Interface::GUI::Render(cmd);
                                    });
        }
    };
}
