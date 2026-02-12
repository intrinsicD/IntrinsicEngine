module;

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.Picking.Impl;

import :Passes.Picking;

import :RenderPipeline;
import :RenderGraph;
import :Components;
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

        // Hard preconditions: if the swapchain is minimized / invalid, or globals aren't ready,
        // skip emitting picking passes for this frame.
        // This prevents vkCmdSetViewport/vkCmdSetScissor from being fed bogus extents and avoids
        // binding an invalid descriptor set.
        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
            return;

        // Vulkan's "undefined extent" sentinel can leak through during WSI transitions.
        // Treat it as invalid and skip. (UINT32_MAX is allowed as the sentinel in VkSurfaceCapabilitiesKHR.)
        if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
            return;

        if (ctx.GlobalDescriptorSet == VK_NULL_HANDLE)
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
                                        [&, pipeline = m_Pipeline](const PickPassData&, const RGRegistry&, VkCommandBuffer cmd)
                                        {
                                            // Re-check: the execute lambda can run later; keep it safe.
                                            if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
                                                return;
                                            if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
                                                return;
                                            if (ctx.GlobalDescriptorSet == VK_NULL_HANDLE)
                                                return;

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

                                            const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
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

                                                // Use a stable, explicit GPU pick ID.
                                                // entt::entity values can be recycled and are not safe as persistent pick identifiers.
                                                uint32_t pickId = 0u;
                                                if (auto* pid = ctx.Scene.GetRegistry().try_get<ECS::Components::Selection::PickID>(entity))
                                                    pickId = pid->Value;

                                                uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                                                const auto& layout = geo->GetLayout();

                                                // Match the push constant layout expected by pick_id.vert/frag (PickPushConsts).
                                                struct PickPushConsts
                                                {
                                                    glm::mat4 Model;
                                                    uint64_t PtrPositions;
                                                    uint64_t PtrNormals;
                                                    uint64_t PtrAux;
                                                    uint32_t EntityID;
                                                    uint32_t _pad[3];
                                                };

                                                PickPushConsts push{
                                                    .Model = worldMatrix,
                                                    .PtrPositions = baseAddr + layout.PositionsOffset,
                                                    .PtrNormals = 0,
                                                    .PtrAux = 0,
                                                    .EntityID = pickId,
                                                    ._pad = {}
                                                };

                                                vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                                   VK_SHADER_STAGE_VERTEX_BIT |
                                                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                   0, sizeof(PickPushConsts), &push);

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

                                                 // Force a proper layout transition out of COLOR_ATTACHMENT_OPTIMAL.
                                                 // The graph currently considers barriers mandatory when either previous or current access writes.
                                                 // A pure-read access can get optimized away if the graph thinks it is read-after-read.
                                                 // For copy, we conservatively mark the usage as memory write as well, to enforce the barrier.
                                                 data.IdBuffer = builder.Read(pickIdHandle,
                                                                              VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                              VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                                             },
                                             [&](const PickCopyPassData& data, const RGRegistry& reg,
                                                 VkCommandBuffer cmd)
                                             {
                                                 if (!ctx.PickRequest.Pending)
                                                     return;
                                                 if (!data.IdBuffer.IsValid())
                                                     return;

                                                 if (!ctx.PickReadbackBuffer)
                                                     return;

                                                 const VkBuffer dst = ctx.PickReadbackBuffer->GetHandle();
                                                 const VkImage img = reg.GetImage(data.IdBuffer);
                                                 if (img == VK_NULL_HANDLE || dst == VK_NULL_HANDLE)
                                                     return;

                                                 const uint32_t srcW = ctx.Resolution.width;
                                                 const uint32_t srcH = ctx.Resolution.height;
                                                 if (srcW == 0 || srcH == 0)
                                                     return;

                                                 const uint32_t x = std::min(ctx.PickRequest.X, srcW - 1u);
                                                 const uint32_t y = std::min(ctx.PickRequest.Y, srcH - 1u);

                                                 VkBufferImageCopy region{};
                                                 region.bufferOffset = 0;
                                                 region.bufferRowLength = 0;
                                                 region.bufferImageHeight = 0;
                                                 region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                                 region.imageSubresource.mipLevel = 0;
                                                 region.imageSubresource.baseArrayLayer = 0;
                                                 region.imageSubresource.layerCount = 1;
                                                 region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
                                                 region.imageExtent = {1, 1, 1};

                                                 // The graph must have transitioned this image to TRANSFER_SRC_OPTIMAL for correctness.
                                                 vkCmdCopyImageToBuffer(cmd,
                                                                       img,
                                                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                                       dst,
                                                                       1,
                                                                       &region);
                                             });
    }
}
