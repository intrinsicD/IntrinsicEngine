module;

#include "RHI.Vulkan.hpp"

module Graphics.Passes.Shadow;

namespace Graphics::Passes
{
    namespace
    {
        struct ShadowPassData
        {
            RGResourceHandle Atlas{};
        };
    }

    void ShadowPass::AddPasses(RenderPassContext& ctx)
    {
        if (!ctx.Recipe.Shadows || !ctx.Lighting.Shadows.Enabled)
            return;

        const RGResourceHandle atlas = ctx.Blackboard.Get(RenderResource::ShadowAtlas);
        if (!atlas.IsValid())
            return;

        ctx.Graph.AddPass<ShadowPassData>("ShadowPass.ClearAtlas",
                                          [atlas](ShadowPassData& data, RGBuilder& builder)
                                          {
                                              RGAttachmentInfo depthInfo{};
                                              depthInfo.ClearValue.depthStencil = {1.0f, 0};
                                              depthInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                              depthInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                              data.Atlas = builder.WriteDepth(atlas, depthInfo);
                                          },
                                          [](const ShadowPassData&, const RGRegistry&, VkCommandBuffer)
                                          {
                                              // Atlas clear establishes a deterministic depth baseline.
                                              // Cascade rasterization is integrated in the next TODO step.
                                          });
    }
}
