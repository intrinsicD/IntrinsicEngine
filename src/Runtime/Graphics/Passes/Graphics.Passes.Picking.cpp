module;

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

module Graphics:Passes.Picking.Impl;

#include "Graphics.PassUtils.hpp"

import :Passes.Picking;

import :RenderPipeline;
import :RenderGraph;
import :Components;
import :Geometry;
import Core.Hash;
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

        const RGResourceHandle depth = ctx.Blackboard.Get(RenderResource::SceneDepth);
        const RGResourceHandle entityId = ctx.Blackboard.Get(RenderResource::EntityId);
        if (!depth.IsValid() || !entityId.IsValid())
            return;

        RGResourceHandle pickIdHandle = entityId;

        ctx.Graph.AddPass<PickPassData>("PickID",
                                        [&](PickPassData& data, RGBuilder& builder)
                                        {
                                            RGAttachmentInfo idInfo{};
                                            idInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                            idInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                            idInfo.ClearValue.color.uint32[0] = 0u;

                                            RGAttachmentInfo depthInfo{};
                                            depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                            data.IdBuffer = builder.WriteColor(entityId, idInfo);
                                            data.Depth = builder.WriteDepth(depth, depthInfo);
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

                                            SetViewportScissor(cmd, ctx.Resolution);

                                            const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                            vkCmdBindDescriptorSets(
                                                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                pipeline->GetLayout(),
                                                0, 1, &ctx.GlobalDescriptorSet,
                                                1, &dynamicOffset);

                                            auto& registry = ctx.Scene.GetRegistry();
                                            auto drawPickGeometry =
                                                [&](entt::entity entity,
                                                    const glm::mat4& worldMatrix,
                                                    const GeometryGpuData* vertexGeo,
                                                    const GeometryGpuData* indexGeo,
                                                    PrimitiveTopology topology)
                                            {
                                                if (vertexGeo == nullptr || vertexGeo->GetVertexBuffer() == nullptr)
                                                    return;

                                                const GeometryGpuData* drawGeo = indexGeo != nullptr ? indexGeo : vertexGeo;
                                                if (drawGeo->GetIndexCount() > 0 && drawGeo->GetIndexBuffer() != nullptr)
                                                {
                                                    vkCmdBindIndexBuffer(cmd,
                                                                        drawGeo->GetIndexBuffer()->GetHandle(),
                                                                        0,
                                                                        VK_INDEX_TYPE_UINT32);
                                                }

                                                VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                switch (topology)
                                                {
                                                case PrimitiveTopology::Points: vkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
                                                case PrimitiveTopology::Lines: vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
                                                case PrimitiveTopology::Triangles:
                                                default: vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
                                                }
                                                vkCmdSetPrimitiveTopology(cmd, vkTopo);

                                                uint32_t pickId = 0u;
                                                if (auto* pid = registry.try_get<ECS::Components::Selection::PickID>(entity))
                                                    pickId = pid->Value;

                                                const uint64_t baseAddr = vertexGeo->GetVertexBuffer()->GetDeviceAddress();
                                                const auto& layout = vertexGeo->GetLayout();

                                                struct PickPushConsts
                                                {
                                                    glm::mat4 Model;
                                                    uint64_t PtrPositions;
                                                    uint64_t PtrNormals;
                                                    uint64_t PtrAux;
                                                    uint32_t EntityID;
                                                    uint32_t _pad[3];
                                                };

                                                const PickPushConsts push{
                                                    .Model = worldMatrix,
                                                    .PtrPositions = baseAddr + layout.PositionsOffset,
                                                    .PtrNormals = 0,
                                                    .PtrAux = 0,
                                                    .EntityID = pickId,
                                                    ._pad = {}
                                                };

                                                vkCmdPushConstants(cmd,
                                                                   pipeline->GetLayout(),
                                                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                   0,
                                                                   sizeof(PickPushConsts),
                                                                   &push);

                                                if (drawGeo->GetIndexCount() > 0)
                                                {
                                                    vkCmdDrawIndexed(cmd, drawGeo->GetIndexCount(), 1, 0, 0, 0);
                                                }
                                                else
                                                {
                                                    const uint32_t vertCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
                                                    vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                }
                                            };

                                            auto surfaceView = registry.view<ECS::Components::Transform::Component,
                                                                             ECS::Surface::Component>();
                                            for (auto [entity, transform, surface] : surfaceView.each())
                                            {
                                                if (!surface.Geometry.IsValid())
                                                    continue;
                                                auto* geo = ctx.GeometryStorage.GetIfValid(surface.Geometry);
                                                if (!geo)
                                                    continue;

                                                const glm::mat4 worldMatrix = registry.all_of<ECS::Components::Transform::WorldMatrix>(entity)
                                                    ? registry.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix
                                                    : GetMatrix(transform);
                                                drawPickGeometry(entity, worldMatrix, geo, nullptr, PrimitiveTopology::Triangles);
                                            }

                                            auto lineView = registry.view<ECS::Components::Transform::Component,
                                                                          ECS::Line::Component>();
                                            for (auto [entity, transform, line] : lineView.each())
                                            {
                                                if (!line.Geometry.IsValid() || !line.EdgeView.IsValid())
                                                    continue;

                                                auto* vertexGeo = ctx.GeometryStorage.GetIfValid(line.Geometry);
                                                auto* edgeGeo = ctx.GeometryStorage.GetIfValid(line.EdgeView);
                                                if (!vertexGeo || !edgeGeo)
                                                    continue;

                                                const glm::mat4 worldMatrix = registry.all_of<ECS::Components::Transform::WorldMatrix>(entity)
                                                    ? registry.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix
                                                    : GetMatrix(transform);
                                                drawPickGeometry(entity, worldMatrix, vertexGeo, edgeGeo, PrimitiveTopology::Lines);
                                            }

                                            auto pointView = registry.view<ECS::Components::Transform::Component,
                                                                           ECS::Point::Component>();
                                            for (auto [entity, transform, point] : pointView.each())
                                            {
                                                if (!point.Geometry.IsValid())
                                                    continue;
                                                auto* geo = ctx.GeometryStorage.GetIfValid(point.Geometry);
                                                if (!geo)
                                                    continue;

                                                const glm::mat4 worldMatrix = registry.all_of<ECS::Components::Transform::WorldMatrix>(entity)
                                                    ? registry.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix
                                                    : GetMatrix(transform);
                                                drawPickGeometry(entity, worldMatrix, geo, nullptr, PrimitiveTopology::Points);
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
