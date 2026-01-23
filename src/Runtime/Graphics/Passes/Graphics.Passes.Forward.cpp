module;

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics:Passes.Forward.Impl;

import :Passes.Forward;

import :Geometry;
import :Material;
import :Components;
import Graphics;
import Core;
import ECS;
import RHI;

using namespace Core::Hash;

namespace Graphics::Passes
{
    void ForwardPass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_Pipeline)
            return;

        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
            return;

        ctx.Graph.AddPass<PassData>("ForwardPass",
                                    [&](PassData& data, RGBuilder& builder)
                                    {
                                        RGAttachmentInfo colorInfo{};
                                        colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};
                                        colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo depthInfo{};
                                        depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                        data.Color = builder.WriteColor(backbuffer, colorInfo);
                                        data.Depth = builder.WriteDepth(depth, depthInfo);

                                        ctx.Blackboard.Add("SceneColor"_id, data.Color);
                                    },
                                    [&, pipeline = m_Pipeline](const PassData&, const RGRegistry&, VkCommandBuffer cmd)
                                    {
                                        ctx.Renderer.BindPipeline(*pipeline);
                                        ctx.Renderer.SetViewport(ctx.Resolution.width, ctx.Resolution.height);

                                        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.
                                            GlobalCameraDynamicOffset);
                                        vkCmdBindDescriptorSets(
                                            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipeline->GetLayout(),
                                            0, 1, &ctx.GlobalDescriptorSet,
                                            1, &dynamicOffset);

                                        VkDescriptorSet globalTextures = ctx.Bindless.GetGlobalSet();
                                        vkCmdBindDescriptorSets(
                                            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            pipeline->GetLayout(),
                                            1, 1, &globalTextures,
                                            0, nullptr);

                                        auto view = ctx.Scene.GetRegistry().view<
                                            ECS::Components::Transform::Component,
                                            ECS::MeshRenderer::Component>();

                                        std::vector<Core::Assets::AssetHandle> batchHandles;
                                        std::vector<Graphics::Material*> batchResults;
                                        std::vector<size_t> entityToBatchIndex;
                                        // Map entity iteration index to batch index

                                        // We need a temporary structure to map back to the renderable component
                                        struct ResolutionRequest
                                        {
                                            ECS::MeshRenderer::Component* Renderable;
                                        };
                                        std::vector<ResolutionRequest> requests;

                                        // Pre-pass: Identify what needs resolving
                                        for (auto [entity, transform, renderable] : view.each())
                                        {
                                            if (renderable.Geometry.IsValid() &&
                                                !renderable.CachedMaterialHandle.IsValid() &&
                                                renderable.Material.IsValid())
                                            {
                                                batchHandles.push_back(renderable.Material);
                                                requests.push_back({&renderable});
                                            }
                                        }

                                        // 2. Single Lock Batch Resolve
                                        if (!batchHandles.empty())
                                        {
                                            // O(1) Lock Acquisition
                                            ctx.AssetManager.BatchResolve<Graphics::Material>(
                                                batchHandles, batchResults);

                                            // 3. Update Components
                                            for (size_t i = 0; i < requests.size(); ++i)
                                            {
                                                Graphics::Material* mat = batchResults[i];
                                                if (mat)
                                                {
                                                    requests[i].Renderable->CachedMaterialHandle = mat->GetHandle();
                                                }
                                            }
                                        }

                                        std::vector<RenderPacket> packets;
                                        packets.reserve(view.size_hint());

                                        for (auto [entity, transform, renderable] : view.each())
                                        {
                                            // 1. Basic Geometry Validation
                                            if (!renderable.Geometry.IsValid()) continue;

                                            // 2. Material Validation
                                            // We rely entirely on the cached handle being updated in Phase 2.
                                            if (!renderable.CachedMaterialHandle.IsValid()) continue;

                                            // 3. Fetch Material Data (O(1) Pool Lookup via StrongHandle)
                                            const auto* matData = ctx.MaterialSystem.GetData(
                                                renderable.CachedMaterialHandle);

                                            if (!matData)
                                            {
                                                // Handle stale cache: If MaterialSystem destroyed the material (e.g. unload),
                                                // invalidate our cache so Phase 1 picks it up again next frame.
                                                renderable.CachedMaterialHandle = {};
                                                continue;
                                            }

                                            // 4. Build Packet
                                            uint32_t textureID = matData->AlbedoID;
                                            const bool isSelected = ctx.Scene.GetRegistry().all_of<
                                                ECS::Components::Selection::SelectedTag>(entity);

                                            glm::mat4 worldMatrix;
                                            if (auto* world = ctx.Scene.GetRegistry().try_get<
                                                ECS::Components::Transform::WorldMatrix>(entity))
                                                worldMatrix = world->Matrix;
                                            else
                                                worldMatrix = ECS::Components::Transform::GetMatrix(transform);

                                            packets.push_back({
                                                renderable.Geometry, textureID, worldMatrix, isSelected
                                            });
                                        }

                                        std::sort(packets.begin(), packets.end());

                                        Geometry::GeometryHandle currentGeoHandle{};
                                        GeometryGpuData* currentGeo = nullptr;

                                        for (const auto& packet : packets)
                                        {
                                            if (packet.GeoHandle != currentGeoHandle)
                                            {
                                                currentGeo = ctx.GeometryStorage.GetUnchecked(packet.GeoHandle);
                                                currentGeoHandle = packet.GeoHandle;
                                                if (!currentGeo) continue;

                                                /* Removed because were using BDA now
                                                auto* vBuf = currentGeo->GetVertexBuffer()->GetHandle();
                                                const auto& layout = currentGeo->GetLayout();
                                                VkBuffer vBuffers[] = {vBuf, vBuf, vBuf};
                                                VkDeviceSize offsets[] = {layout.PositionsOffset, layout.NormalsOffset, layout.AuxOffset};
                                                vkCmdBindVertexBuffers(cmd, 0, 3, vBuffers, offsets);*/

                                                if (currentGeo->GetIndexCount() > 0)
                                                {
                                                    // Note: If using indexed drawing with BDA, you still bind the index buffer
                                                    // strictly for the Input Assembler, OR you read indices manually in the shader (pure mesh shading style).
                                                    // For now, keep BindIndexBuffer, remove BindVertexBuffers.
                                                    vkCmdBindIndexBuffer(
                                                        cmd, currentGeo->GetIndexBuffer()->GetHandle(), 0,
                                                        VK_INDEX_TYPE_UINT32);
                                                }

                                                VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                switch (currentGeo->GetTopology())
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
                                            }

                                            if (!currentGeo) continue;

                                            uint64_t baseAddr = currentGeo->GetVertexBuffer()->GetDeviceAddress();
                                            const auto& layout = currentGeo->GetLayout();
                                            RHI::MeshPushConstants push{
                                                .Model = packet.Transform,
                                                .PtrPositions = baseAddr + layout.PositionsOffset,
                                                .PtrNormals = baseAddr + layout.NormalsOffset,
                                                .PtrAux = baseAddr + layout.AuxOffset,
                                                .TextureID = packet.TextureID,
                                                ._pad = {}
                                            };

                                            if (packet.IsSelected)
                                                push.TextureID = packet.TextureID | 0x80000000u;

                                            vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                               VK_SHADER_STAGE_VERTEX_BIT |
                                                               VK_SHADER_STAGE_FRAGMENT_BIT,
                                                               0, sizeof(RHI::MeshPushConstants), &push);

                                            if (currentGeo->GetIndexCount() > 0)
                                            {
                                                vkCmdDrawIndexed(cmd, currentGeo->GetIndexCount(), 1, 0, 0, 0);
                                                Core::Telemetry::TelemetrySystem::Get().RecordDrawCall(
                                                    currentGeo->GetIndexCount() / 3);
                                            }
                                            else
                                            {
                                                const uint32_t vertCount = static_cast<uint32_t>(currentGeo->GetLayout()
                                                    .PositionsSize / sizeof(glm::vec3));
                                                vkCmdDraw(cmd, vertCount, 1, 0, 0);
                                                Core::Telemetry::TelemetrySystem::Get().RecordDrawCall(0);
                                            }
                                        }
                                    });
    }
}
