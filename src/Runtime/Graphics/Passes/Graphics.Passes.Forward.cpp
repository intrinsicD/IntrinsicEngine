module;

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <array>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

module Graphics:Passes.Forward.Impl;

import :Passes.Forward;

import :Geometry;
import :Material;
import :Components;
import :GPUScene;
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

        const bool enableStage3 = (m_EnableGpuCulling && m_CullPipeline && (m_CullSetLayout != VK_NULL_HANDLE));

        // Stage 1: use the canonical set=2 layout provided by PipelineLibrary.
        // ForwardPass must NOT create/destroy its own layout; it must match the pipeline layout exactly.
        if (m_InstanceSetPool == nullptr)
        {
            if (m_InstanceSetLayout == VK_NULL_HANDLE)
            {
                Core::Log::Error("ForwardPass: Stage 1 instance set layout not set. Did RenderSystem call SetInstanceSetLayout()?");
                return;
            }

            m_InstanceSetPool = std::make_unique<RHI::PersistentDescriptorPool>(*m_Device,
                                                                                /*maxSets*/ 256,
                                                                                /*storageBufferCount*/ 512,
                                                                                /*debugName*/ "ForwardPass.Stage1.Instance");
        }

        // Ensure cull descriptor pool exists.
        if (m_CullSetPool == nullptr)
        {
            // 1 set per frame is enough for now.
            m_CullSetPool = std::make_unique<RHI::PersistentDescriptorPool>(*m_Device,
                                                                            /*maxSets*/ 16,
                                                                            /*storageBufferCount*/ 16 * 5,
                                                                            /*debugName*/ "ForwardPass.Cull");
        }

        const RGResourceHandle backbuffer = ctx.Blackboard.Get("Backbuffer"_id);
        const RGResourceHandle depth = ctx.Blackboard.Get("SceneDepth"_id);
        if (!backbuffer.IsValid() || !depth.IsValid())
            return;

        ctx.Graph.AddPass<PassData>("ForwardPass",
                                    [&ctx, backbuffer, depth](PassData& data, RGBuilder& builder)
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
                                    // IMPORTANT: stored by RenderGraph, so do not capture stack locals by reference.
                                    [this, &ctx, pipeline = m_Pipeline](const PassData&, const RGRegistry&, VkCommandBuffer cmd)
                                    {
                                        // IMPORTANT: this is recorded into a *secondary* command buffer now.
                                        // Do not call ctx.Renderer helpers here (they record on the primary CB).
                                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

                                        VkViewport viewport{};
                                        viewport.x = 0.0f;
                                        viewport.y = 0.0f;
                                        viewport.width = (float)ctx.Resolution.width;
                                        viewport.height = (float)ctx.Resolution.height;
                                        viewport.minDepth = 0.0f;
                                        viewport.maxDepth = 1.0f;

                                        VkRect2D scissor{};
                                        scissor.offset = {0, 0};
                                        scissor.extent = {ctx.Resolution.width, ctx.Resolution.height};

                                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                                        vkCmdSetScissor(cmd, 0, 1, &scissor);

                                        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                0, 1, &ctx.GlobalDescriptorSet,
                                                                1, &dynamicOffset);

                                        // Bind bindless (set=1)
                                        VkDescriptorSet globalTextures = ctx.Bindless.GetGlobalSet();
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                1, 1, &globalTextures,
                                                                0, nullptr);

                                        // -----------------------------------------------------------------
                                        // Collect visible instances (Stage 1: no culling, identity visibility)
                                        // -----------------------------------------------------------------
                                        auto view = ctx.Scene.GetRegistry().view<
                                            ECS::Components::Transform::Component,
                                            ECS::MeshRenderer::Component>();

                                        std::vector<Core::Assets::AssetHandle> batchHandles;
                                        std::vector<Graphics::Material*> batchResults;

                                        struct ResolutionRequest
                                        {
                                            ECS::MeshRenderer::Component* Renderable;
                                        };
                                        std::vector<ResolutionRequest> requests;

                                        // Resolve material assets for all renderables (cheap batched lookup, single shared_lock).
                                        // This avoids stale CachedMaterialHandle when the underlying asset changes or pool slots are reused.
                                        for (auto [entity, transform, renderable] : view.each())
                                        {
                                            if (!renderable.Geometry.IsValid())
                                                continue;

                                            if (!renderable.Material.IsValid())
                                            {
                                                renderable.CachedMaterialHandle = {};
                                                continue;
                                            }

                                            // Force-resolve every frame. The extra BatchResolve is cheap and avoids subtle aliasing
                                            // when assets/materials are created dynamically (e.g. drag-drop spawning).
                                            batchHandles.push_back(renderable.Material);
                                            requests.push_back({&renderable});
                                        }

                                        if (!batchHandles.empty())
                                        {
                                            ctx.AssetManager.BatchResolve<Graphics::Material>(batchHandles, batchResults);
                                            for (size_t i = 0; i < requests.size(); ++i)
                                            {
                                                Graphics::Material* mat = (i < batchResults.size()) ? batchResults[i] : nullptr;
                                                requests[i].Renderable->CachedMaterialHandle = mat ? mat->GetHandle() : Graphics::MaterialHandle{};
                                            }
                                        }

                                        std::vector<RenderPacket> packets;
                                        packets.reserve(view.size_hint());

                                        // Stage 1 buffers are indexed by "Submit order" in packets.
                                        std::vector<RHI::GpuInstanceData> cpuInstances;
                                        cpuInstances.reserve(view.size_hint());
                                        std::vector<uint32_t> cpuVisibility;
                                        cpuVisibility.reserve(view.size_hint());

                                        for (auto [entity, transform, renderable] : view.each())
                                        {
                                            if (!renderable.Geometry.IsValid()) continue;
                                            if (!renderable.CachedMaterialHandle.IsValid()) continue;

                                            const auto* matData = ctx.MaterialSystem.GetData(renderable.CachedMaterialHandle);
                                            if (!matData)
                                            {
                                                renderable.CachedMaterialHandle = {};
                                                continue;
                                            }

                                            uint32_t textureID = matData->AlbedoID;

                                            // DEBUG: trace the actual texture ID used for this entity.
                                            Core::Log::Info("[ForwardPass] Entity={} mat(index={}, gen={}) AlbedoID={}",
                                                           static_cast<uint32_t>(static_cast<entt::id_type>(entity)),
                                                           renderable.CachedMaterialHandle.Index,
                                                           renderable.CachedMaterialHandle.Generation,
                                                           textureID);

                                            const bool isSelected = ctx.Scene.GetRegistry().all_of<
                                                ECS::Components::Selection::SelectedTag>(entity);

                                            glm::mat4 worldMatrix;
                                            if (auto* world = ctx.Scene.GetRegistry().try_get<
                                                ECS::Components::Transform::WorldMatrix>(entity))
                                                worldMatrix = world->Matrix;
                                            else
                                                worldMatrix = ECS::Components::Transform::GetMatrix(transform);

                                            packets.push_back({renderable.Geometry, textureID, worldMatrix, isSelected});

                                            const uint32_t entityID = static_cast<uint32_t>(static_cast<entt::id_type>(entity));
                                            RHI::GpuInstanceData inst{};
                                            inst.Model = worldMatrix;
                                            inst.TextureID = textureID | (isSelected ? 0x80000000u : 0u);
                                            inst.EntityID = entityID;
                                            cpuInstances.push_back(inst);
                                            cpuVisibility.push_back(static_cast<uint32_t>(cpuInstances.size() - 1));
                                        }

                                        // Upload SSBOs for this frame.
                                        // Use the engine-provided frame index so we line up with frames-in-flight.
                                        const uint32_t frame = (ctx.FrameIndex % FRAMES);

                                        const size_t instancesBytes = cpuInstances.size() * sizeof(RHI::GpuInstanceData);
                                        const size_t visibilityBytes = cpuVisibility.size() * sizeof(uint32_t);

                                        // Track current capacity via static shadow arrays.
                                        static size_t s_InstanceCap[FRAMES] = {0, 0};
                                        static size_t s_VisibilityCap[FRAMES] = {0, 0};

                                        if (instancesBytes > s_InstanceCap[frame] || !m_InstanceBuffer[frame])
                                        {
                                            s_InstanceCap[frame] = std::max(instancesBytes, size_t(4));
                                            m_InstanceBuffer[frame] = std::make_unique<RHI::VulkanBuffer>(
                                                *m_Device, s_InstanceCap[frame],
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
                                        }

                                        // NOTE: Stage 1 visibility is CPU-generated (identity mapping) and must be host-visible.
                                        // Stage 3 uses a different GPU-only visibility/remap buffer. Do not share.
                                        if (visibilityBytes > s_VisibilityCap[frame] || !m_Stage1VisibilityBuffer[frame])
                                        {
                                            s_VisibilityCap[frame] = std::max(visibilityBytes, size_t(4));
                                            m_Stage1VisibilityBuffer[frame] = std::make_unique<RHI::VulkanBuffer>(
                                                *m_Device, s_VisibilityCap[frame],
                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
                                        }

                                        if (!cpuInstances.empty())
                                            m_InstanceBuffer[frame]->Write(cpuInstances.data(), instancesBytes);
                                        if (!cpuVisibility.empty())
                                            m_Stage1VisibilityBuffer[frame]->Write(cpuVisibility.data(), visibilityBytes);

                                        // Allocate + update the per-frame instance descriptor set.
                                        // IMPORTANT (Validation): we must not vkUpdateDescriptorSets() on a set that may still be
                                        // in use by an in-flight command buffer unless the binding uses UPDATE_AFTER_BIND/UNUSED_WHILE_PENDING.
                                        // Our stage1 bindings are plain storage buffers, so we use the safe pattern: allocate
                                        // a fresh set per frame-slot.
                                        if (!m_InstanceSetPool)
                                        {
                                            Core::Log::Error("ForwardPass: Persistent descriptor pool not initialized for Stage 1 instance set.");
                                            return;
                                        }

                                        // Always allocate a new set each frame slot to avoid updating an in-flight set.
                                        VkDescriptorSet instanceSet = m_InstanceSetPool->Allocate(m_InstanceSetLayout);
                                        if (instanceSet == VK_NULL_HANDLE)
                                        {
                                            Core::Log::Error("ForwardPass: Failed to allocate Stage 1 instance descriptor set.");
                                            return;
                                        }

                                        // Keep a handle for debugging / optional reuse policies.
                                        m_InstanceSet[frame] = instanceSet;

                                        VkDescriptorBufferInfo instInfo{};
                                        instInfo.buffer = m_InstanceBuffer[frame]->GetHandle();
                                        instInfo.offset = 0;
                                        instInfo.range = instancesBytes == 0 ? 4 : instancesBytes;

                                        VkDescriptorBufferInfo visInfo{};
                                        visInfo.buffer = m_Stage1VisibilityBuffer[frame]->GetHandle();
                                        visInfo.offset = 0;
                                        visInfo.range = visibilityBytes == 0 ? 4 : visibilityBytes;

                                        VkWriteDescriptorSet writes[2]{};
                                        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                        writes[0].dstSet = instanceSet;
                                        writes[0].dstBinding = 0;
                                        writes[0].descriptorCount = 1;
                                        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                        writes[0].pBufferInfo = &instInfo;

                                        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                        writes[1].dstSet = instanceSet;
                                        writes[1].dstBinding = 1;
                                        writes[1].descriptorCount = 1;
                                        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                        writes[1].pBufferInfo = &visInfo;

                                        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 2, writes, 0, nullptr);

                                        // Bind Stage 1 instance buffers at set=2.
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                2, 1, &instanceSet,
                                                                0, nullptr);

                                        // Sort for state changes only (geo/material). Instance index is stable within packets.
                                        std::sort(packets.begin(), packets.end());

                                        // -----------------------------------------------------------------
                                        // Stage 2: build indirect stream (one command per instance)
                                        // -----------------------------------------------------------------
                                        std::vector<VkDrawIndexedIndirectCommand> cpuIndirect;
                                        cpuIndirect.reserve(packets.size());

                                        // We will also record draw-call ranges per geometry run.
                                        struct Run
                                        {
                                            GeometryGpuData* Geo = nullptr;
                                            uint32_t FirstCmd = 0;
                                            uint32_t CmdCount = 0;
                                            VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                        };
                                        std::vector<Run> runs;
                                        runs.reserve(256);

                                        Geometry::GeometryHandle currentGeoHandle{};
                                        GeometryGpuData* currentGeo = nullptr;
                                        Run* currentRun = nullptr;

                                        uint32_t instanceIndex = 0;
                                        for (const auto& packet : packets)
                                        {
                                            if (packet.GeoHandle != currentGeoHandle)
                                            {
                                                currentGeo = ctx.GeometryStorage.GetUnchecked(packet.GeoHandle);
                                                currentGeoHandle = packet.GeoHandle;
                                                currentRun = nullptr;

                                                if (!currentGeo)
                                                {
                                                    ++instanceIndex;
                                                    continue;
                                                }

                                                VkPrimitiveTopology vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                                                switch (currentGeo->GetTopology())
                                                {
                                                case PrimitiveTopology::Points: vkTopo = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
                                                case PrimitiveTopology::Lines: vkTopo = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
                                                case PrimitiveTopology::Triangles:
                                                default: vkTopo = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
                                                }

                                                runs.push_back({.Geo = currentGeo, .FirstCmd = (uint32_t)cpuIndirect.size(), .CmdCount = 0u, .Topology = vkTopo});
                                                currentRun = &runs.back();
                                            }

                                            if (!currentGeo || currentGeo->GetIndexCount() == 0)
                                            {
                                                // Stage 2: only indexed path for now; non-indexed meshes keep the old path later.
                                                ++instanceIndex;
                                                continue;
                                            }

                                            VkDrawIndexedIndirectCommand cmdi{};
                                            cmdi.indexCount = currentGeo->GetIndexCount();
                                            cmdi.instanceCount = 1;
                                            cmdi.firstIndex = 0;
                                            cmdi.vertexOffset = 0;
                                            cmdi.firstInstance = instanceIndex; // drives gl_InstanceIndex
                                            cpuIndirect.push_back(cmdi);
                                            if (currentRun) currentRun->CmdCount++;

                                            ++instanceIndex;
                                        }

                                        // Upload indirect buffer.
                                        const size_t indirectBytes = cpuIndirect.size() * sizeof(VkDrawIndexedIndirectCommand);
                                        static size_t s_IndirectCap[FRAMES] = {0, 0};
                                        if (indirectBytes > s_IndirectCap[frame] || !m_Stage2IndirectIndexedBuffer[frame])
                                        {
                                            s_IndirectCap[frame] = std::max(indirectBytes, size_t(4));
                                            m_Stage2IndirectIndexedBuffer[frame] = std::make_unique<RHI::VulkanBuffer>(
                                                *m_Device, s_IndirectCap[frame],
                                                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                VMA_MEMORY_USAGE_CPU_TO_GPU);
                                        }
                                        if (!cpuIndirect.empty())
                                            m_Stage2IndirectIndexedBuffer[frame]->Write(cpuIndirect.data(), indirectBytes);

                                        // Optional: register buffer in rendergraph for debugging/resource list.
                                        (void)ctx.Graph;

                                        // Barrier: make host writes visible to indirect + vertex stages.
                                        // NOTE: vkCmdPipelineBarrier2 with buffer barriers is NOT allowed inside dynamic rendering.
                                        // Stage 2 uses CPU_TO_GPU buffers, which are host-coherent in our allocator; the extra barrier
                                        // is redundant for correctness here and will be reintroduced in a pre-render pass if needed.
                                        // (See VUID-vkCmdPipelineBarrier2-None-09553/09554.)

                                        // -----------------------------------------------------------------
                                        // Draw phase (Stage 2): indirect per geometry run
                                        // -----------------------------------------------------------------
                                        uint32_t drawCallTelemetryCount = 0;
                                        for (const auto& run : runs)
                                        {
                                            if (!run.Geo || run.CmdCount == 0)
                                                continue;

                                            vkCmdBindIndexBuffer(cmd, run.Geo->GetIndexBuffer()->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
                                            vkCmdSetPrimitiveTopology(cmd, run.Topology);

                                            // Push only per-geometry BDA pointers; instance selection happens via gl_InstanceIndex.
                                            const uint64_t baseAddr = run.Geo->GetVertexBuffer()->GetDeviceAddress();
                                            const auto& layout = run.Geo->GetLayout();
                                            RHI::MeshPushConstants push{
                                                .Model = glm::mat4(1.0f),
                                                .PtrPositions = baseAddr + layout.PositionsOffset,
                                                .PtrNormals = baseAddr + layout.NormalsOffset,
                                                .PtrAux = baseAddr + layout.AuxOffset,
                                                .TextureID = 0,
                                                ._pad = {}
                                            };

                                            vkCmdPushConstants(cmd, pipeline->GetLayout(),
                                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                               0, sizeof(RHI::MeshPushConstants), &push);

                                            vkCmdDrawIndexedIndirect(cmd,
                                                                     m_Stage2IndirectIndexedBuffer[frame]->GetHandle(),
                                                                     run.FirstCmd * sizeof(VkDrawIndexedIndirectCommand),
                                                                     run.CmdCount,
                                                                     sizeof(VkDrawIndexedIndirectCommand));
                                            drawCallTelemetryCount += run.CmdCount;
                                        }

                                        // Approximate telemetry (one command == one instance draw)
                                        if (drawCallTelemetryCount > 0)
                                            Core::Telemetry::TelemetrySystem::Get().RecordDrawCall(drawCallTelemetryCount);

                                        // NOTE: Non-indexed geometry still uses the Stage 1 direct draw path; we can add a second
                                        // indirect stream later if needed.
                                    });

        // ---------------------------------------------------------------------
        // Stage 3 NOTE
        // ---------------------------------------------------------------------
        // Stage 3 is currently under active development (batching + per-geometry draws).
        // v1: enable Stage 3 when GPUScene is available so we can cull + draw from persistent SSBOs.

        // Only run Stage 3 if explicitly enabled and a GPUScene is bound.
        if (!enableStage3 || ctx.GpuScene == nullptr)
            return;

        const VkBuffer gpuSceneBufferHandle = ctx.GpuScene->GetSceneBuffer().GetHandle();
        const VkBuffer gpuBoundsBufferHandle = ctx.GpuScene->GetBoundsBuffer().GetHandle();

        // v1: when Stage 3 is active, we intentionally stop after Stage 1 setup (material resolve and stability checks)
        // and do not execute Stage 2 (CPU-built indirect). This prevents double-drawing.

        // v1: per-frame persistent state for the Stage 3 validation path.
        // NOTE: RenderGraph executes passes on worker threads; keep data trivially-copyable and avoid heap allocations.
        struct Stage3FrameState
        {
            uint32_t InstanceCount = 0;
            Geometry::GeometryHandle GeoHandle{};
            uint32_t IndexCount = 0;
            uint64_t PtrPositions = 0;
            uint64_t PtrNormals = 0;
            uint64_t PtrAux = 0;
            VkBuffer IndexBuffer = VK_NULL_HANDLE;
            VkPrimitiveTopology Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            std::array<glm::vec4, 6> Planes{};
        };

        const uint32_t frame = (ctx.FrameIndex % FRAMES);

        // Build an immutable snapshot *now* (single-threaded), then capture by value in passes.
        Stage3FrameState stage3{};
        {
            // Frustum planes from camera view-projection.
            const glm::mat4 viewProj = ctx.CameraProj * ctx.CameraView;
            const Geometry::Frustum fr = Geometry::Frustum::CreateFromMatrix(viewProj);
            for (int i = 0; i < 6; ++i)
            {
                const auto& p = fr.Planes[i];
                stage3.Planes[i] = glm::vec4(p.Normal, p.Distance);
            }

            // v1 batching: pick the first valid geometry as the global mesh.
            const auto viewEnt = ctx.Scene.GetRegistry().view<ECS::MeshRenderer::Component>();
            for (auto entity : viewEnt)
            {
                const auto& renderable = viewEnt.get<ECS::MeshRenderer::Component>(entity);
                if (renderable.Geometry.IsValid())
                {
                    stage3.GeoHandle = renderable.Geometry;
                    break;
                }
            }

            if (stage3.GeoHandle.IsValid())
            {
                stage3.InstanceCount = ctx.GpuScene->GetActiveCountApprox();

                GeometryGpuData* geo = ctx.GeometryStorage.GetUnchecked(stage3.GeoHandle);
                if (!geo || geo->GetIndexCount() == 0)
                {
                    stage3.GeoHandle = {};
                    stage3.InstanceCount = 0;
                }
                else
                {
                    stage3.IndexCount = geo->GetIndexCount();
                    const uint64_t vbda = geo->GetVertexBuffer()->GetDeviceAddress();
                    const auto& layout = geo->GetLayout();
                    stage3.PtrPositions = vbda + layout.PositionsOffset;
                    stage3.PtrNormals = vbda + layout.NormalsOffset;
                    stage3.PtrAux = vbda + layout.AuxOffset;

                    stage3.IndexBuffer = geo->GetIndexBuffer()->GetHandle();
                    switch (geo->GetTopology())
                    {
                    case PrimitiveTopology::Points: stage3.Topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
                    case PrimitiveTopology::Lines: stage3.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
                    case PrimitiveTopology::Triangles:
                    default: stage3.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
                    }
                }
            }
        }

        // If Stage 3 can't run this frame, bail out without enqueueing GPU cull/draw passes.
        if (stage3.InstanceCount == 0 || !stage3.GeoHandle.IsValid() || stage3.IndexBuffer == VK_NULL_HANDLE)
            return;

        // ---------------------------
        // Pass B: Compute cull
        // ---------------------------
        struct CullPassData
        {
            RGResourceHandle Instances;
            RGResourceHandle Bounds;
            RGResourceHandle Indirect;
            RGResourceHandle Visibility;
            RGResourceHandle Count;
        };

        ctx.Graph.AddPass<CullPassData>("ForwardCull",
                                        [this, &ctx, stage3, frame](CullPassData& data, RGBuilder& builder) mutable
                                        {
                                            const uint32_t fi = frame;
                                            if (ctx.GpuScene == nullptr)
                                                return;

                                            if (stage3.InstanceCount == 0 || !stage3.GeoHandle.IsValid())
                                                return;

                                            const uint32_t maxDraws = stage3.InstanceCount;
                                            const size_t indirectBytes = std::max<size_t>(maxDraws * sizeof(VkDrawIndexedIndirectCommand), 4);
                                            const size_t remapBytes = std::max<size_t>(maxDraws * sizeof(uint32_t), 4);

                                            static size_t s_IndirectCap[FRAMES] = {0, 0};
                                            static size_t s_RemapCap[FRAMES] = {0, 0};

                                            if (!m_DrawCountBuffer[fi])
                                            {
                                                m_DrawCountBuffer[fi] = std::make_unique<RHI::VulkanBuffer>(
                                                    *m_Device, sizeof(uint32_t),
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    VMA_MEMORY_USAGE_GPU_ONLY);
                                            }
                                            if (indirectBytes > s_IndirectCap[fi] || !m_Stage3IndirectIndexedBuffer[fi])
                                            {
                                                s_IndirectCap[fi] = indirectBytes;
                                                m_Stage3IndirectIndexedBuffer[fi] = std::make_unique<RHI::VulkanBuffer>(
                                                    *m_Device, s_IndirectCap[fi],
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    VMA_MEMORY_USAGE_GPU_ONLY);
                                            }
                                            if (remapBytes > s_RemapCap[fi] || !m_VisibilityBuffer[fi])
                                            {
                                                s_RemapCap[fi] = remapBytes;
                                                m_VisibilityBuffer[fi] = std::make_unique<RHI::VulkanBuffer>(
                                                    *m_Device, s_RemapCap[fi],
                                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                    VMA_MEMORY_USAGE_GPU_ONLY);
                                            }

                                            data.Instances = builder.ImportBuffer("GPUScene.Scene"_id, ctx.GpuScene->GetSceneBuffer());
                                            data.Bounds = builder.ImportBuffer("GPUScene.Bounds"_id, ctx.GpuScene->GetBoundsBuffer());
                                            data.Indirect = builder.ImportBuffer("Stage3.Indirect"_id, *m_Stage3IndirectIndexedBuffer[fi]);
                                            data.Visibility = builder.ImportBuffer("Stage3.Visibility"_id, *m_VisibilityBuffer[fi]);
                                            data.Count = builder.ImportBuffer("Stage3.Count"_id, *m_DrawCountBuffer[fi]);

                                            builder.Read(data.Instances, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                            builder.Read(data.Bounds, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

                                            builder.Write(data.Count, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                            builder.Write(data.Indirect, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                            builder.Write(data.Visibility, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                        },
                                        [this, fi = frame, stage3, gpuSceneBufferHandle, gpuBoundsBufferHandle](const CullPassData&, const RGRegistry&, VkCommandBuffer cmd) mutable
                                        {
                                            if (stage3.InstanceCount == 0 || !stage3.GeoHandle.IsValid())
                                                return;

                                            if (!m_DrawCountBuffer[fi] || !m_Stage3IndirectIndexedBuffer[fi] || !m_VisibilityBuffer[fi])
                                                return;

                                            vkCmdFillBuffer(cmd, m_DrawCountBuffer[fi]->GetHandle(), 0, sizeof(uint32_t), 0);

                                            VkDescriptorSet cullSet = m_CullSetPool->Allocate(m_CullSetLayout);
                                            if (cullSet == VK_NULL_HANDLE)
                                                return;

                                            VkDescriptorBufferInfo inst{.buffer = gpuSceneBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};
                                            VkDescriptorBufferInfo bounds{.buffer = gpuBoundsBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};
                                            VkDescriptorBufferInfo indirect{.buffer = m_Stage3IndirectIndexedBuffer[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                            VkDescriptorBufferInfo vis{.buffer = m_VisibilityBuffer[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                            VkDescriptorBufferInfo count{.buffer = m_DrawCountBuffer[fi]->GetHandle(), .offset = 0, .range = sizeof(uint32_t)};

                                            VkWriteDescriptorSet w[5]{};
                                            auto setBuf = [&](uint32_t i, uint32_t binding, const VkDescriptorBufferInfo* info)
                                            {
                                                w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                                w[i].dstSet = cullSet;
                                                w[i].dstBinding = binding;
                                                w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                                w[i].descriptorCount = 1;
                                                w[i].pBufferInfo = info;
                                            };
                                            setBuf(0, 1, &inst);
                                            setBuf(1, 2, &bounds);
                                            setBuf(2, 3, &indirect);
                                            setBuf(3, 4, &vis);
                                            setBuf(4, 5, &count);
                                            vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 5, w, 0, nullptr);

                                            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetHandle());
                                            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetLayout(), 0, 1, &cullSet, 0, nullptr);

                                            struct CullPC
                                            {
                                                glm::vec4 Planes[6];
                                                uint32_t TotalInstanceCount;
                                                uint32_t IndexCount;
                                                uint32_t _pad0;
                                                uint32_t _pad1;
                                            } pc{};

                                            for (int i = 0; i < 6; ++i)
                                                pc.Planes[i] = stage3.Planes[i];
                                            pc.TotalInstanceCount = stage3.InstanceCount;
                                            pc.IndexCount = stage3.IndexCount;

                                            vkCmdPushConstants(cmd, m_CullPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPC), &pc);

                                            const uint32_t wg = 64;
                                            const uint32_t groups = (stage3.InstanceCount + wg - 1) / wg;
                                            vkCmdDispatch(cmd, groups, 1, 1);
                                        });

        // ---------------------------
        // Pass C: Draw (raster)
        // ---------------------------
        ctx.Graph.AddPass<PassData>("ForwardDraw",
                                    [this, &ctx, enableStage3, frame, backbuffer, depth](PassData& data, RGBuilder& builder)
                                    {
                                        RGAttachmentInfo colorInfo{};
                                        colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};
                                        colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo depthInfo{};
                                        depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                        data.Color = builder.WriteColor(backbuffer, colorInfo);
                                        data.Depth = builder.WriteDepth(depth, depthInfo);

                                        if (enableStage3)
                                        {
                                            const uint32_t fi = frame;
                                            if (m_Stage3IndirectIndexedBuffer[fi])
                                            {
                                                auto indirect = builder.ImportBuffer("Stage3.Indirect"_id, *m_Stage3IndirectIndexedBuffer[fi]);
                                                builder.Read(indirect, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                            }
                                            if (m_DrawCountBuffer[fi])
                                            {
                                                auto count = builder.ImportBuffer("Stage3.Count"_id, *m_DrawCountBuffer[fi]);
                                                builder.Read(count, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                            }
                                            if (m_VisibilityBuffer[fi])
                                            {
                                                auto remap = builder.ImportBuffer("Stage3.Visibility"_id, *m_VisibilityBuffer[fi]);
                                                builder.Read(remap, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                            }

                                            // Persistent scene buffer read by vertex shader.
                                            auto scene = builder.ImportBuffer("GPUScene.Scene"_id, ctx.GpuScene->GetSceneBuffer());
                                            builder.Read(scene, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                        }

                                        ctx.Blackboard.Add("SceneColor"_id, data.Color);
                                    },
                                    [this, &ctx, pipeline = m_Pipeline, enableStage3, stage3](const PassData&, const RGRegistry&, VkCommandBuffer cmd) mutable
                                    {
                                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->GetHandle());

                                        VkViewport viewport{};
                                        viewport.x = 0.0f;
                                        viewport.y = 0.0f;
                                        viewport.width = (float)ctx.Resolution.width;
                                        viewport.height = (float)ctx.Resolution.height;
                                        viewport.minDepth = 0.0f;
                                        viewport.maxDepth = 1.0f;

                                        VkRect2D scissor{};
                                        scissor.offset = {0, 0};
                                        scissor.extent = {ctx.Resolution.width, ctx.Resolution.height};

                                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                                        vkCmdSetScissor(cmd, 0, 1, &scissor);

                                        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                0, 1, &ctx.GlobalDescriptorSet,
                                                                1, &dynamicOffset);

                                        // Bind bindless (set=1)
                                        VkDescriptorSet globalTextures = ctx.Bindless.GetGlobalSet();
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                1, 1, &globalTextures,
                                                                0, nullptr);

                                        const uint32_t fi = (ctx.FrameIndex % FRAMES);

                                        if (!enableStage3)
                                            return;
                                        if (stage3.InstanceCount == 0 || stage3.IndexBuffer == VK_NULL_HANDLE)
                                            return;
                                        if (!m_DrawCountBuffer[fi] || !m_Stage3IndirectIndexedBuffer[fi] || !m_VisibilityBuffer[fi])
                                            return;
                                        if (m_InstanceSetLayout == VK_NULL_HANDLE || !m_InstanceSetPool)
                                            return;

                                        // Bind Stage3 instance/visibility (set=2) expected by the forward pipeline.
                                        // Binding 0: instances buffer (GPUScene.Scene)
                                        // Binding 1: visibility/remap buffer (Stage3.Visibility)
                                        VkDescriptorSet instanceSet = m_InstanceSetPool->Allocate(m_InstanceSetLayout);
                                        if (instanceSet == VK_NULL_HANDLE)
                                            return;

                                        VkDescriptorBufferInfo instInfo{};
                                        instInfo.buffer = ctx.GpuScene->GetSceneBuffer().GetHandle();
                                        instInfo.offset = 0;
                                        instInfo.range = VK_WHOLE_SIZE;

                                        VkDescriptorBufferInfo visInfo{};
                                        visInfo.buffer = m_VisibilityBuffer[fi]->GetHandle();
                                        visInfo.offset = 0;
                                        visInfo.range = VK_WHOLE_SIZE;

                                        VkWriteDescriptorSet writes[2]{};
                                        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                        writes[0].dstSet = instanceSet;
                                        writes[0].dstBinding = 0;
                                        writes[0].descriptorCount = 1;
                                        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                        writes[0].pBufferInfo = &instInfo;

                                        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                        writes[1].dstSet = instanceSet;
                                        writes[1].dstBinding = 1;
                                        writes[1].descriptorCount = 1;
                                        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                        writes[1].pBufferInfo = &visInfo;

                                        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 2, writes, 0, nullptr);

                                        vkCmdBindDescriptorSets(cmd,
                                                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                2,
                                                                1,
                                                                &instanceSet,
                                                                0,
                                                                nullptr);

                                        vkCmdBindIndexBuffer(cmd, stage3.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                                        vkCmdSetPrimitiveTopology(cmd, stage3.Topology);

                                        {
                                            RHI::MeshPushConstants push{
                                                .Model = glm::mat4(1.0f),
                                                .PtrPositions = stage3.PtrPositions,
                                                .PtrNormals = stage3.PtrNormals,
                                                .PtrAux = stage3.PtrAux,
                                                .TextureID = 0,
                                                ._pad = {}
                                            };

                                            vkCmdPushConstants(cmd,
                                                               pipeline->GetLayout(),
                                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                               0,
                                                               sizeof(RHI::MeshPushConstants),
                                                               &push);
                                        }

                                        const uint32_t maxDraws = stage3.InstanceCount;
                                        if (maxDraws == 0)
                                            return;

                                        if (vkCmdDrawIndexedIndirectCountKHR)
                                        {
                                            vkCmdDrawIndexedIndirectCountKHR(cmd,
                                                                             m_Stage3IndirectIndexedBuffer[fi]->GetHandle(),
                                                                             0,
                                                                             m_DrawCountBuffer[fi]->GetHandle(),
                                                                             0,
                                                                             maxDraws,
                                                                             sizeof(VkDrawIndexedIndirectCommand));
                                        }
                                        else
                                        {
                                            vkCmdDrawIndexedIndirect(cmd,
                                                                     m_Stage3IndirectIndexedBuffer[fi]->GetHandle(),
                                                                     0,
                                                                     maxDraws,
                                                                     sizeof(VkDrawIndexedIndirectCommand));
                                        }
                                    });

        return;
    }
}
