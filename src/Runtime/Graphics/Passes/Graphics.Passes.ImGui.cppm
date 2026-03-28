module;

#include "RHI.Vulkan.hpp"

export module Graphics.Passes.ImGui;

import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Core.Hash;
import RHI.Descriptors;
import RHI.Device;
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
            // Only add the ImGui render pass when the extraction phase produced
            // editor overlay draw data.  This decouples ImGui draw-list
            // generation (which now runs before extraction) from render-graph
            // recording.
            if (!ctx.EditorOverlay.HasDrawData)
                return;

            struct Data
            {
                RGResourceHandle Target;
            };

            ctx.Graph.AddPass<Data>("ImGuiPass",
                                    [&](Data& data, RGBuilder& builder)
                                    {
                                        const RGResourceHandle target = GetPresentationTarget(ctx);
                                        if (!target.IsValid())
                                            return;

                                        RGAttachmentInfo info{VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE};
                                        data.Target = builder.WriteColor(target, info);
                                    },
                                    [](const Data&, const RGRegistry&, VkCommandBuffer cmd)
                                    {
                                        Interface::GUI::Render(cmd);
                                    });
        }
    };
}
