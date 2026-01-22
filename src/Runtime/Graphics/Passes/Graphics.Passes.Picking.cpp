module;

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.Picking.Impl;

import :Passes.Picking;

import :Geometry;
import Core;
import ECS;
import RHI;

using namespace Core::Hash;

namespace Graphics::Passes
{
    void PickingPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_Pipeline)
            return;

        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!depth.IsValid())
            return;

        RGResourceHandle pickIdHandle{};

        ctx.Graph.AddPass<PickPassData>("PickID",
                                        [&](PickPassData& data, RGBuilder& builder)
                                        {
                                            RGTextureDesc idDesc{};
                                            idDesc.Width = ctx.Resolution.width;
                                            idDesc.Height = ctx.Resolution.height;
                                            idDesc.Format = VK_FORMAT_R32_UINT;
                                            idDesc.Usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                VK_IMAGE_USAGE_SAMPLED_BIT;
                                            idDesc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

                                            auto idTex = builder.CreateTexture("PickID"_id, idDesc);

                                            RGAttachmentInfo idInfo{};
                                            idInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                            idInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                            idInfo.ClearValue.color.uint32[0] = 0u;

                                            RGAttachmentInfo depthInfo{};
                                            depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                            data.IdBuffer = builder.WriteColor(idTex, idInfo);
                                            data.Depth = builder.WriteDepth(depth, depthInfo);

                                            pickIdHandle = data.IdBuffer;
                                            ctx.Blackboard.Add("PickID"_id, data.IdBuffer);
                                        },
                                        [&, pipeline = m_Pipeline](const PickPassData&, const RGRegistry&,
                                                                   VkCommandBuffer cmd)
                                        {
                                            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                              pipeline->GetHandle());

                                            VkViewport viewport{};
                                            viewport.x = 0.0f;
                                            viewport.y = 0.0f;
                                            viewport.width = static_cast<float>(ctx.Resolution.width);
                                            viewport.height = static_cast<float>(ctx.Resolution.height);
                                            viewport.minDepth = 0.0f;
                                            viewport.maxDepth = 1.0f;
                                            VkRect2D scissor{{0, 0}, ctx.Resolution};
                                            vkCmdSetViewport(cmd, 0, 1, &viewport);
                                            vkCmdSetScissor(cmd, 0, 1, &scissor);

                                            const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.
                                                GlobalCameraDynamicOffset);
                                            vkCmdBindDescriptorSets(
                                                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipeline->GetLayout(),
                                                0, 1, &ctx.GlobalDescriptorSet,
                                                1, &dynamicOffset);

                                            auto view = ctx.Scene.GetRegistry().view<
                                                ECS::Components::Transform::Component,
                                                ECS::MeshRenderer::Component>();

                                            for (auto [entity, transform, renderable] : view.each())
                                            {
                                                if (!renderable.Geometry.IsValid())
                                                    continue;

                                                auto* geo = ctx.GeometryStorage.GetUnchecked(renderable.Geometry);
                                                if (!geo)
                                                    continue;

                                                glm::mat4 worldMatrix;
                                                if (auto* world = ctx.Scene.GetRegistry().try_get<
                                                    ECS::Components::Transform::WorldMatrix>(entity))
                                                    worldMatrix = world->Matrix;
                                                else
                                                    worldMatrix = GetMatrix(transform);

                                                /*auto* vBuf = geo->GetVertexBuffer()->GetHandle();
                                                const auto& layout = geo->GetLayout();
                                                VkBuffer vBuffers[] = {vBuf, vBuf, vBuf};
                                                VkDeviceSize offsets[] = {layout.PositionsOffset, layout.NormalsOffset, layout.AuxOffset};
                                                vkCmdBindVertexBuffers(cmd, 0, 3, vBuffers, offsets);*/

                                                if (geo->GetIndexCount() > 0)
                                                    vkCmdBindIndexBuffer(
                                                        cmd, geo->GetIndexBuffer()->GetHandle(), 0,
                                                        VK_INDEX_TYPE_UINT32);

                                                VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                switch (geo->GetTopology())
                                                {
                                                case PrimitiveTopology::Points: vkTopo =
                                                        VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                                                    break;
                                                case PrimitiveTopology::Lines: vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                                                    break;
                                                case PrimitiveTopology::Triangles:
                                                default: vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                    break;
                                                }
                                                vkCmdSetPrimitiveTopology(cmd, vkTopo);

                                                const uint32_t id = static_cast<uint32_t>(static_cast<entt::id_type>(
                                                    entity));
                                                uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                                                const auto& layout = geo->GetLayout();
                                                RHI::MeshPushConstants push{
                                                    .Model = worldMatrix,
                                                    .PtrPositions = baseAddr + layout.PositionsOffset,
                                                    .PtrNormals = 0,
                                                    .PtrAux = 0,
                                                    .TextureID = id,
                                                    ._pad = {}
                                                };

                                                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                                   VK_SHADER_STAGE_VERTEX_BIT |
                                                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                   0, sizeof(RHI::MeshPushConstants), &push);

                                                if (geo->GetIndexCount() > 0)
                                                    vkCmdDrawIndexed(cmd, geo->GetIndexCount(), 1, 0, 0, 0);
                                                else
                                                {
                                                    const uint32_t vertCount = static_cast<uint32_t>(geo->GetLayout().
                                                        PositionsSize / sizeof(glm::vec3));
                                                    vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                }
                                            }
                                        });

        ctx.Graph.AddPass<PickCopyPassData>("PickCopy",
                                            [&](PickCopyPassData& data, RGBuilder& builder)
                                            {
                                                if (!ctx.PickRequest.Pending)
                                                    return;

                                                data.IdBuffer = builder.Read(pickIdHandle,
                                                                             VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                             VK_ACCESS_2_TRANSFER_READ_BIT);
                                            },
                                            [&](const PickCopyPassData& data, const RGRegistry& reg,
                                                VkCommandBuffer cmd)
                                            {
                                                (void)cmd;

                                                if (!ctx.PickRequest.Pending)
                                                    return;
                                                if (!data.IdBuffer.IsValid())
                                                    return;

                                                if (ctx.FrameIndex >= m_ReadbackBuffers.size() || !m_ReadbackBuffers[ctx
                                                    .FrameIndex])
                                                    return;

                                                const VkBuffer dst = m_ReadbackBuffers[ctx.FrameIndex]->GetHandle();
                                                const VkImage img = reg.GetImage(data.IdBuffer);

                                                ctx.Renderer.CopyPixel_R32_UINT_ToBuffer(
                                                    img,
                                                    ctx.Resolution.width,
                                                    ctx.Resolution.height,
                                                    ctx.PickRequest.X,
                                                    ctx.PickRequest.Y,
                                                    dst);
                                            });
    }
}
