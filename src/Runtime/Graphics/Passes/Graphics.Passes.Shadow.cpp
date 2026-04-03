module;

#include <algorithm>
#include <glm/glm.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.Passes.Shadow;

namespace Graphics::Passes
{
    namespace
    {
        struct ShadowPushConstants
        {
            glm::mat4 Model{1.0f};
            uint64_t PtrPositions = 0;
            uint32_t CascadeIndex = 0;
            uint32_t _pad = 0;
        };
        static_assert(sizeof(ShadowPushConstants) == 80);

        struct ShadowPassData
        {
            RGResourceHandle Atlas{};
        };
    }

    void ShadowPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_Pipeline)
            return;
        if (!ctx.Recipe.Shadows || !ctx.Lighting.Shadows.Enabled)
            return;
        if (ctx.GlobalDescriptorSet == VK_NULL_HANDLE)
            return;
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
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
                                          });

        ctx.Graph.AddPass<ShadowPassData>("ShadowPass.Rasterize",
                                          [atlas](ShadowPassData& data, RGBuilder& builder)
                                          {
                                              RGAttachmentInfo depthInfo{};
                                              depthInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                                              depthInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                              data.Atlas = builder.WriteDepth(atlas, depthInfo);
                                          },
                                          [this, &ctx](const ShadowPassData&, const RGRegistry&, VkCommandBuffer cmd)
                                          {
                                              const uint32_t cascadeCount = std::clamp(
                                                  ctx.Lighting.Shadows.CascadeCount, 1u, ShadowParams::MaxCascades);
                                              if (cascadeCount == 0u || ctx.SurfaceDrawPackets.empty())
                                                  return;

                                              vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());

                                              const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                              vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                      m_Pipeline->GetLayout(),
                                                                      0, 1, &ctx.GlobalDescriptorSet,
                                                                      1, &dynamicOffset);

                                              constexpr uint32_t kShadowCascadeResolution = 2048u;
                                              for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                                              {
                                                  VkViewport viewport{};
                                                  viewport.x = static_cast<float>(cascade * kShadowCascadeResolution);
                                                  viewport.y = 0.0f;
                                                  viewport.width = static_cast<float>(kShadowCascadeResolution);
                                                  viewport.height = static_cast<float>(kShadowCascadeResolution);
                                                  viewport.minDepth = 0.0f;
                                                  viewport.maxDepth = 1.0f;
                                                  vkCmdSetViewport(cmd, 0, 1, &viewport);

                                                  VkRect2D scissor{};
                                                  scissor.offset.x = static_cast<int32_t>(cascade * kShadowCascadeResolution);
                                                  scissor.offset.y = 0;
                                                  scissor.extent.width = kShadowCascadeResolution;
                                                  scissor.extent.height = kShadowCascadeResolution;
                                                  vkCmdSetScissor(cmd, 0, 1, &scissor);

                                                  for (const auto& packet : ctx.SurfaceDrawPackets)
                                                  {
                                                      if (!packet.Geometry.IsValid())
                                                          continue;
                                                      auto* geo = ctx.GeometryStorage.GetIfValid(packet.Geometry);
                                                      if (!geo || !geo->GetVertexBuffer())
                                                          continue;
                                                      const auto& layout = geo->GetLayout();

                                                      ShadowPushConstants push{};
                                                      push.Model = packet.WorldMatrix;
                                                      push.PtrPositions = geo->GetVertexBuffer()->GetDeviceAddress() + layout.PositionsOffset;
                                                      push.CascadeIndex = cascade;
                                                      vkCmdPushConstants(cmd, m_Pipeline->GetLayout(),
                                                                         VK_SHADER_STAGE_VERTEX_BIT,
                                                                         0, sizeof(push), &push);

                                                      if (geo->GetIndexCount() > 0u && geo->GetIndexBuffer())
                                                      {
                                                          vkCmdBindIndexBuffer(cmd, geo->GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
                                                          vkCmdDrawIndexed(cmd, geo->GetIndexCount(), 1, 0, 0, 0);
                                                      }
                                                      else
                                                      {
                                                          const uint32_t vertexCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
                                                          if (vertexCount >= 3u)
                                                              vkCmdDraw(cmd, vertexCount, 1, 0, 0);
                                                      }
                                                  }
                                              }
                                          });
    }
}
