module;

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <limits>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"

module Graphics.Passes.Picking;


import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Components;
import Graphics.Geometry;
import Graphics.ShaderRegistry;
import Geometry.HalfedgeMesh;
import Geometry.MeshUtils;
import Core.Filesystem;
import Core.Hash;
import Core.Logging;
import ECS;
import RHI;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    namespace
    {
        // Unified MRT push constants for all pick pipelines.
        // 112 bytes — fits within Vulkan's guaranteed 128-byte minimum.
        struct PickMRTPushConsts
        {
            glm::mat4 Model{};         // 64
            uint64_t PtrPositions{};   // 8
            uint64_t PtrAux{};         // 8  (PtrEdges for lines, unused for points)
            uint64_t PtrPrimitiveFaceIds{}; // 8 (surface triangle -> authoritative mesh face)
            uint32_t EntityID{};       // 4
            uint32_t PrimitiveBase{};  // 4
            float    PickWidth{};      // 4  (line width for line pick, point size for point pick)
            float    ViewportWidth{};  // 4
            float    ViewportHeight{}; // 4
            uint32_t _pad{};           // 4
        };                           // total: 112
        static_assert(sizeof(PickMRTPushConsts) == 112);

        // Legacy single-output push constants (for fallback pipeline).
        struct PickLegacyPushConsts
        {
            glm::mat4 Model{};
            uint64_t PtrPositions{};
            uint64_t PtrNormals{};
            uint64_t PtrAux{};
            uint32_t EntityID{};
            uint32_t _pad[3]{};
        };

        [[nodiscard]] const Geometry::Halfedge::Mesh* ResolvePickingMesh(const entt::registry& registry, entt::entity entity)
        {
            if (const auto* meshData = registry.try_get<ECS::Mesh::Data>(entity))
            {
                if (meshData->MeshRef)
                    return meshData->MeshRef.get();
            }

            if (const auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity))
            {
                if (collider->CollisionRef && collider->CollisionRef->SourceMesh)
                    return collider->CollisionRef->SourceMesh.get();
            }

            return nullptr;
        }

        void BuildTriangleFaceIds(const Geometry::Halfedge::Mesh& mesh, std::vector<uint32_t>& triangleFaceIds)
        {
            std::vector<glm::vec3> positions;
            std::vector<uint32_t> indices;
            Geometry::MeshUtils::ExtractIndexedTriangles(mesh, positions, indices, nullptr, &triangleFaceIds);
        }
    }

    void PickingPass::AddPasses(RenderPassContext& ctx)
    {
        // Need at least the legacy pipeline.
        if (!m_Pipeline && !m_MeshPickPipeline)
            return;

        if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
            return;
        if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
            return;
        if (ctx.GlobalDescriptorSet == VK_NULL_HANDLE)
            return;

        const RGResourceHandle depth = ctx.Blackboard.Get(RenderResource::SceneDepth);
        const RGResourceHandle entityId = ctx.Blackboard.Get(RenderResource::EntityId);
        if (!depth.IsValid() || !entityId.IsValid())
            return;

        // PrimitiveId is optional. When present, UINT_MAX is the invalid
        // sentinel so primitive index 0 remains selectable.
        const RGResourceHandle primitiveId = ctx.Blackboard.Get(RenderResource::PrimitiveId);
        const bool hasMRT = primitiveId.IsValid() && m_MeshPickPipeline != nullptr;

        RGResourceHandle pickEntityHandle = entityId;
        RGResourceHandle pickPrimHandle = primitiveId;

        std::unordered_map<uint32_t, uint64_t> faceIdByGeoIndex;
        {
            auto& registry = ctx.Scene.GetRegistry();
            auto surfaceView = registry.view<ECS::Surface::Component>();
            std::vector<uint32_t> triangleFaceIds;

            for (auto [entity, surface] : surfaceView.each())
            {
                if (!surface.Geometry.IsValid() || faceIdByGeoIndex.contains(surface.Geometry.Index))
                    continue;

                auto* geo = ctx.GeometryStorage.GetIfValid(surface.Geometry);
                if (!geo || geo->GetTopology() != PrimitiveTopology::Triangles)
                    continue;

                const Geometry::Halfedge::Mesh* mesh = ResolvePickingMesh(registry, entity);
                if (mesh == nullptr)
                    continue;

                BuildTriangleFaceIds(*mesh, triangleFaceIds);
                if (triangleFaceIds.empty())
                    continue;

                const uint32_t gpuTriangleCount = (geo->GetIndexCount() > 0)
                    ? (geo->GetIndexCount() / 3u)
                    : static_cast<uint32_t>((geo->GetLayout().PositionsSize / sizeof(glm::vec3)) / 3u);
                if (gpuTriangleCount != triangleFaceIds.size())
                    continue;

                if (const uint64_t addr = EnsureFaceIdBuffer(surface.Geometry.Index,
                                                             triangleFaceIds.data(),
                                                             static_cast<uint32_t>(triangleFaceIds.size()));
                    addr != 0)
                {
                    faceIdByGeoIndex[surface.Geometry.Index] = addr;
                }
            }
        }

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

                                            if (hasMRT)
                                            {
                                                RGAttachmentInfo primInfo{};
                                                primInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                                primInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
                                                primInfo.ClearValue.color.uint32[0] = std::numeric_limits<uint32_t>::max();
                                                data.PrimIdBuffer = builder.WriteColor(primitiveId, primInfo);
                                            }

                                            data.Depth = builder.WriteDepth(depth, depthInfo);
                                        },
                                        [&, hasMRT,
                                         faceIdByGeoIndex = std::move(faceIdByGeoIndex),
                                         legacyPipeline = m_Pipeline,
                                         meshPipeline = m_MeshPickPipeline,
                                         linePipeline = m_LinePickPipeline,
                                         pointPipeline = m_PointPickPipeline]
                                        (const PickPassData&, const RGRegistry&, VkCommandBuffer cmd)
                                        {
                                            if (ctx.Resolution.width == 0 || ctx.Resolution.height == 0)
                                                return;
                                            if (ctx.Resolution.width == ~0u || ctx.Resolution.height == ~0u)
                                                return;
                                            if (ctx.GlobalDescriptorSet == VK_NULL_HANDLE)
                                                return;

                                            SetViewportScissor(cmd, ctx.Resolution);

                                            auto& registry = ctx.Scene.GetRegistry();

                                            auto getPickId = [&](entt::entity entity) -> uint32_t
                                            {
                                                if (auto* pid = registry.try_get<ECS::Components::Selection::PickID>(entity))
                                                    return pid->Value;
                                                return 0u;
                                            };

                                            auto getWorldMatrix = [&](entt::entity entity, const ECS::Components::Transform::Component& transform) -> glm::mat4
                                            {
                                                if (registry.all_of<ECS::Components::Transform::WorldMatrix>(entity))
                                                    return registry.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix;
                                                return GetMatrix(transform);
                                            };

                                            auto bindPipelineAndSets = [&](RHI::GraphicsPipeline* pipeline)
                                            {
                                                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                  pipeline->GetHandle());
                                                const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                                vkCmdBindDescriptorSets(
                                                    cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                    pipeline->GetLayout(),
                                                    0, 1, &ctx.GlobalDescriptorSet,
                                                    1, &dynamicOffset);
                                            };

                                            // ==========================================================
                                            // Surface (mesh triangles) — MRT pipeline when available.
                                            // ==========================================================
                                            {
                                                RHI::GraphicsPipeline* surfPipeline = (hasMRT && meshPipeline) ? meshPipeline : legacyPipeline;
                                                if (!surfPipeline)
                                                    goto skip_surfaces;

                                                bindPipelineAndSets(surfPipeline);
                                                 vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

                                                auto surfaceView = registry.view<ECS::Components::Transform::Component,
                                                                                 ECS::Surface::Component>();
                                                for (auto [entity, transform, surface] : surfaceView.each())
                                                {
                                                    if (!surface.Geometry.IsValid())
                                                        continue;
                                                    auto* geo = ctx.GeometryStorage.GetIfValid(surface.Geometry);
                                                    if (!geo || !geo->GetVertexBuffer())
                                                        continue;

                                                    const glm::mat4 worldMatrix = getWorldMatrix(entity, transform);
                                                    const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                                                    const auto& layout = geo->GetLayout();

                                                    if (hasMRT && meshPipeline)
                                                    {
                                                        const auto faceIdIt = faceIdByGeoIndex.find(surface.Geometry.Index);
                                                        const PickMRTPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrAux = 0,
                                                            .PtrPrimitiveFaceIds = (faceIdIt != faceIdByGeoIndex.end()) ? faceIdIt->second : 0,
                                                            .EntityID = getPickId(entity),
                                                            .PrimitiveBase = 0,
                                                            .PickWidth = 0.0f,
                                                            .ViewportWidth = static_cast<float>(ctx.Resolution.width),
                                                            .ViewportHeight = static_cast<float>(ctx.Resolution.height),
                                                            ._pad = 0,
                                                        };
                                                        vkCmdPushConstants(cmd, meshPipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);
                                                    }
                                                    else
                                                    {
                                                        const PickLegacyPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrNormals = 0,
                                                            .PtrAux = 0,
                                                            .EntityID = getPickId(entity),
                                                            ._pad = {},
                                                        };
                                                        vkCmdPushConstants(cmd, legacyPipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);
                                                    }

                                                    if (geo->GetIndexCount() > 0 && geo->GetIndexBuffer())
                                                    {
                                                        vkCmdBindIndexBuffer(cmd, geo->GetIndexBuffer()->GetHandle(),
                                                                             0, VK_INDEX_TYPE_UINT32);
                                                        vkCmdDrawIndexed(cmd, geo->GetIndexCount(), 1, 0, 0, 0);
                                                    }
                                                    else
                                                    {
                                                        const uint32_t vertCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));
                                                        vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                    }
                                                }
                                            }
                                            skip_surfaces:

                                            // ==========================================================
                                            // Lines (graph edges, wireframe) — MRT line pick or legacy.
                                            // ==========================================================
                                            {
                                                RHI::GraphicsPipeline* linePickPl = (hasMRT && linePipeline) ? linePipeline : legacyPipeline;
                                                if (!linePickPl)
                                                    goto skip_lines;

                                                bindPipelineAndSets(linePickPl);
                                                 vkCmdSetPrimitiveTopology(cmd,
                                                                           (hasMRT && linePipeline)
                                                                               ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                                                               : VK_PRIMITIVE_TOPOLOGY_LINE_LIST);

                                                auto lineView = registry.view<ECS::Components::Transform::Component,
                                                                              ECS::Line::Component>();
                                                for (auto [entity, transform, line] : lineView.each())
                                                {
                                                    // Domain policy:
                                                    // - Mesh entities: pick via surface-face IDs only.
                                                    // - Graph entities: pick via edge IDs.
                                                    // - Point-cloud entities: pick via point IDs.
                                                    const bool isMeshEntity = registry.all_of<ECS::MeshCollider::Component>(entity)
                                                                           || registry.all_of<ECS::Mesh::Data>(entity);
                                                    const bool isGraphEntity = registry.all_of<ECS::Graph::Data>(entity);
                                                    if (isMeshEntity || !isGraphEntity)
                                                        continue;

                                                    if (!line.Geometry.IsValid() || !line.EdgeView.IsValid())
                                                        continue;

                                                    auto* vertexGeo = ctx.GeometryStorage.GetIfValid(line.Geometry);
                                                    auto* edgeGeo = ctx.GeometryStorage.GetIfValid(line.EdgeView);
                                                    if (!vertexGeo || !edgeGeo)
                                                        continue;

                                                    const glm::mat4 worldMatrix = getWorldMatrix(entity, transform);
                                                    const uint64_t baseAddr = vertexGeo->GetVertexBuffer()->GetDeviceAddress();
                                                    const auto& layout = vertexGeo->GetLayout();

                                                    if (hasMRT && linePipeline)
                                                    {
                                                        // Line pick: PtrAux = edge buffer BDA, PickWidth = line width for thick-line expansion.
                                                        const uint64_t edgeAddr = edgeGeo->GetIndexBuffer()
                                                            ? edgeGeo->GetIndexBuffer()->GetDeviceAddress()
                                                            : 0;
                                                        const PickMRTPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrAux = edgeAddr,
                                                            .PtrPrimitiveFaceIds = 0,
                                                            .EntityID = getPickId(entity),
                                                            .PrimitiveBase = 0,
                                                            .PickWidth = line.Width,
                                                            .ViewportWidth = static_cast<float>(ctx.Resolution.width),
                                                            .ViewportHeight = static_cast<float>(ctx.Resolution.height),
                                                            ._pad = 0,
                                                        };
                                                        vkCmdPushConstants(cmd, linePipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);
                                                        // Line pick pipeline uses vertex-amplified quads: 6 vertices per segment.
                                                        vkCmdDraw(cmd, line.EdgeCount * 6, 1, 0, 0);
                                                    }
                                                    else
                                                    {
                                                        // Legacy path: bind index buffer, draw as line list.
                                                        if (edgeGeo->GetIndexCount() > 0 && edgeGeo->GetIndexBuffer())
                                                        {
                                                            vkCmdBindIndexBuffer(cmd, edgeGeo->GetIndexBuffer()->GetHandle(),
                                                                                 0, VK_INDEX_TYPE_UINT32);
                                                        }

                                                        const PickLegacyPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrNormals = 0,
                                                            .PtrAux = 0,
                                                            .EntityID = getPickId(entity),
                                                            ._pad = {},
                                                        };
                                                        vkCmdPushConstants(cmd, legacyPipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);

                                                        if (edgeGeo->GetIndexCount() > 0)
                                                            vkCmdDrawIndexed(cmd, edgeGeo->GetIndexCount(), 1, 0, 0, 0);
                                                    }
                                                }
                                            }
                                            skip_lines:

                                            // ==========================================================
                                            // Points (point clouds, graph nodes) — MRT point pick or legacy.
                                            // ==========================================================
                                            {
                                                RHI::GraphicsPipeline* pointPickPl = (hasMRT && pointPipeline) ? pointPipeline : legacyPipeline;
                                                if (!pointPickPl)
                                                    goto skip_points;

                                                bindPipelineAndSets(pointPickPl);
                                                 vkCmdSetPrimitiveTopology(cmd,
                                                                           (hasMRT && pointPipeline)
                                                                               ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                                                               : VK_PRIMITIVE_TOPOLOGY_POINT_LIST);

                                                auto pointView = registry.view<ECS::Components::Transform::Component,
                                                                               ECS::Point::Component>();
                                                for (auto [entity, transform, point] : pointView.each())
                                                {
                                                    // Domain policy:
                                                    // - Graph entities resolve via edge IDs (line domain),
                                                    //   so skip graph node point-domain writes.
                                                    // - Mesh entities resolve via surface-face IDs.
                                                    // - Point-cloud entities resolve via point IDs.
                                                    const bool isMeshEntity = registry.all_of<ECS::MeshCollider::Component>(entity)
                                                                           || registry.all_of<ECS::Mesh::Data>(entity);
                                                    const bool isGraphEntity = registry.all_of<ECS::Graph::Data>(entity);
                                                    const bool isPointCloudEntity = registry.all_of<ECS::PointCloud::Data>(entity);
                                                    if (isMeshEntity || isGraphEntity || !isPointCloudEntity)
                                                        continue;

                                                    if (!point.Geometry.IsValid())
                                                        continue;
                                                    auto* geo = ctx.GeometryStorage.GetIfValid(point.Geometry);
                                                    if (!geo || !geo->GetVertexBuffer())
                                                        continue;

                                                    const glm::mat4 worldMatrix = getWorldMatrix(entity, transform);
                                                    const uint64_t baseAddr = geo->GetVertexBuffer()->GetDeviceAddress();
                                                    const auto& layout = geo->GetLayout();
                                                    const uint32_t vertCount = static_cast<uint32_t>(layout.PositionsSize / sizeof(glm::vec3));

                                                    if (hasMRT && pointPipeline)
                                                    {
                                                        const PickMRTPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrAux = 0,
                                                            .PtrPrimitiveFaceIds = 0,
                                                            .EntityID = getPickId(entity),
                                                            .PrimitiveBase = 0,
                                                            .PickWidth = point.Size,
                                                            .ViewportWidth = static_cast<float>(ctx.Resolution.width),
                                                            .ViewportHeight = static_cast<float>(ctx.Resolution.height),
                                                            ._pad = 0,
                                                        };
                                                        vkCmdPushConstants(cmd, pointPipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);
                                                        // Point pick pipeline uses billboard quads: 6 vertices per point.
                                                        vkCmdDraw(cmd, vertCount * 6, 1, 0, 0);
                                                    }
                                                    else
                                                    {

                                                        const PickLegacyPushConsts push{
                                                            .Model = worldMatrix,
                                                            .PtrPositions = baseAddr + layout.PositionsOffset,
                                                            .PtrNormals = 0,
                                                            .PtrAux = 0,
                                                            .EntityID = getPickId(entity),
                                                            ._pad = {},
                                                        };
                                                        vkCmdPushConstants(cmd, legacyPipeline->GetLayout(),
                                                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                                           0, sizeof(push), &push);
                                                        vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                    }
                                                }
                                            }
                                            skip_points:;
                                        });

        // =================================================================
        // PickCopy — readback both EntityID and PrimitiveID to CPU buffer.
        // =================================================================
        ctx.Graph.AddPass<PickCopyPassData>("PickCopy",
                                             [&, hasMRT](PickCopyPassData& data, RGBuilder& builder)
                                             {
                                                 if (!ctx.PickRequest.Pending)
                                                     return;

                                                 data.IdBuffer = builder.Read(pickEntityHandle,
                                                                              VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                              VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);

                                                 if (hasMRT)
                                                 {
                                                     data.PrimIdBuffer = builder.Read(pickPrimHandle,
                                                                                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                                      VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
                                                 }
                                             },
                                             [&, hasMRT](const PickCopyPassData& data, const RGRegistry& reg,
                                                 VkCommandBuffer cmd)
                                             {
                                                 if (!ctx.PickRequest.Pending)
                                                     return;
                                                 if (!data.IdBuffer.IsValid())
                                                     return;
                                                 if (!ctx.PickReadbackBuffer)
                                                     return;

                                                 const VkBuffer dst = ctx.PickReadbackBuffer->GetHandle();
                                                 if (dst == VK_NULL_HANDLE)
                                                     return;

                                                 const uint32_t srcW = ctx.Resolution.width;
                                                 const uint32_t srcH = ctx.Resolution.height;
                                                 if (srcW == 0 || srcH == 0)
                                                     return;

                                                 const uint32_t x = std::min(ctx.PickRequest.X, srcW - 1u);
                                                 const uint32_t y = std::min(ctx.PickRequest.Y, srcH - 1u);

                                                 // Copy EntityID pixel to offset 0.
                                                 {
                                                     const VkImage img = reg.GetImage(data.IdBuffer);
                                                     if (img == VK_NULL_HANDLE)
                                                         return;

                                                     VkBufferImageCopy region{};
                                                     region.bufferOffset = 0;
                                                     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                                     region.imageSubresource.layerCount = 1;
                                                     region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
                                                     region.imageExtent = {1, 1, 1};

                                                     vkCmdCopyImageToBuffer(cmd, img,
                                                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                                           dst, 1, &region);
                                                 }

                                                 // Copy PrimitiveID pixel to offset 4 (second uint32).
                                                 if (hasMRT && data.PrimIdBuffer.IsValid())
                                                 {
                                                     const VkImage primImg = reg.GetImage(data.PrimIdBuffer);
                                                     if (primImg == VK_NULL_HANDLE)
                                                         return;

                                                     VkBufferImageCopy region{};
                                                     region.bufferOffset = sizeof(uint32_t);
                                                     region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                                                     region.imageSubresource.layerCount = 1;
                                                     region.imageOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), 0};
                                                     region.imageExtent = {1, 1, 1};

                                                     vkCmdCopyImageToBuffer(cmd, primImg,
                                                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                                           dst, 1, &region);
                                                 }
                                             });
    }

    uint64_t PickingPass::EnsureFaceIdBuffer(uint32_t geoIndex,
                                             const uint32_t* faceIds,
                                             uint32_t triangleCount)
    {
        return EnsurePerEntityBuffer<uint32_t>(
            *m_Device, m_FaceIdBuffers, geoIndex, faceIds, triangleCount, "PickingPass");
    }
}
