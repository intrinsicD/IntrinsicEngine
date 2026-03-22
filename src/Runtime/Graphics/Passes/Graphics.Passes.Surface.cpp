module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <array>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

// Optional: enable extremely verbose per-entity material/texture tracing.
// #define INTRINSIC_SURFACEPASS_TRACE_TEXTURES

module Graphics.Passes.Surface;


import Graphics.RenderPipeline;
import Graphics.RenderGraph;
import Graphics.Geometry;
import Graphics.Material;
import Graphics.Components;
import Graphics.GPUScene;
import Graphics.ShaderRegistry;

import Geometry.Frustum;
import Geometry.Handle;
import Geometry.Validation;
import Geometry.Overlap;
import Geometry.Sphere;

import Core.Filesystem;
import Core.Hash;
import Core.Logging;

import ECS;

import RHI.Buffer;
import RHI.CommandUtils;
import RHI.PersistentDescriptors;
import RHI.Pipeline;
import RHI.Types;

#include "Graphics.PassUtils.hpp"

using namespace Core::Hash;

namespace Graphics::Passes
{
    void SurfacePass::AddPasses(RenderPassContext& ctx)
    {
        if (!m_Pipeline)
            return;

        const RGResourceHandle depth = ctx.Blackboard.Get(RenderResource::SceneDepth);
        if (!depth.IsValid())
            return;

        // Lazily init descriptor pools (single growing pool per category).
        // With FRAMES=3 matching VulkanDevice::MAX_FRAMES_IN_FLIGHT, each frame slot
        // maps to a unique buffer/descriptor slot — no aliasing across in-flight frames.
        if (m_InstanceSetPool == nullptr)
        {
            if (m_InstanceSetLayout == VK_NULL_HANDLE)
            {
                Core::Log::Error("SurfacePass: Stage 1 instance set layout not set.");
                return;
            }

            m_InstanceSetPool = std::make_unique<RHI::PersistentDescriptorPool>(
                *m_Device,
                SurfacePassConstants::kInstancePoolMaxSets,
                SurfacePassConstants::kInstancePoolStorageBuffers,
                "SurfacePass.Stage1.Instance");
        }

        if (m_CullSetPool == nullptr)
        {
            m_CullSetPool = std::make_unique<RHI::PersistentDescriptorPool>(
                *m_Device,
                SurfacePassConstants::kCullPoolMaxSets,
                SurfacePassConstants::kCullPoolStorageBuffers,
                "SurfacePass.Cull");
        }

        DrawStream stream = BuildDrawStream(ctx);

        // Deferred-backed paths write the G-buffer MRT set instead of SceneColorHDR.
        if (UsesDeferredComposition(ctx.Recipe.LightingPath) && m_GBufferPipeline)
        {
            const RGResourceHandle normal   = ctx.Blackboard.Get(RenderResource::SceneNormal);
            const RGResourceHandle albedo   = ctx.Blackboard.Get(RenderResource::Albedo);
            const RGResourceHandle material = ctx.Blackboard.Get(RenderResource::Material0);

            if (normal.IsValid() && albedo.IsValid() && material.IsValid())
            {
                AddGBufferRasterPass(ctx, normal, albedo, material, depth, std::move(stream));
                return;
            }
            // Fall through to forward path if G-buffer resources are missing.
            Core::Log::Warn("SurfacePass: Deferred-backed path requested but G-buffer resources missing — falling back to forward.");
        }

        // Forward path: write directly to SceneColorHDR.
        const RGResourceHandle sceneColor = ctx.Blackboard.Get(RenderResource::SceneColorHDR);
        if (!sceneColor.IsValid())
            return;

        AddRasterPass(ctx, sceneColor, depth, std::move(stream));
    }


    Graphics::Passes::SurfacePass::DrawStream Graphics::Passes::SurfacePass::BuildDrawStream(RenderPassContext& ctx)
    {
         DrawStream out{};

        const bool canGpu = m_EnableGpuCulling && (ctx.GpuScene != nullptr) && (m_CullPipeline != nullptr) && (m_CullSetLayout != VK_NULL_HANDLE);
        if (!canGpu)
            return out; // CPU path not yet ported into draw-stream.

        // -----------------------------------------------------------------
        // Dense geometry batching (per-frame)
        // -----------------------------------------------------------------
        struct DenseGeo
        {
            Geometry::GeometryHandle Handle{};
            GeometryGpuData* Geo = nullptr;
        };

        std::vector<DenseGeo> dense;
        dense.reserve(256);

        uint32_t maxHandleIndex = 0;

        // Build unique geometry list from ECS (queries Surface::Component).
        {
            auto view = ctx.Scene.GetRegistry().view<ECS::Surface::Component>();
            for (auto entity : view)
            {
                const auto& sc = view.get<ECS::Surface::Component>(entity);
                if (!sc.Geometry.IsValid())
                    continue;

                maxHandleIndex = std::max(maxHandleIndex, sc.Geometry.Index);

                bool seen = false;
                for (const DenseGeo& g : dense)
                {
                    if (g.Handle == sc.Geometry) { seen = true; break; }
                }
                if (seen)
                    continue;

                GeometryGpuData* geo = ctx.GeometryStorage.GetIfValid(sc.Geometry);
                if (!geo || geo->GetIndexCount() == 0 || !geo->GetIndexBuffer() || !geo->GetVertexBuffer())
                    continue;

                dense.push_back({.Handle = sc.Geometry, .Geo = geo});
            }
        }

         // IMPORTANT: ensure deterministic ordering.
        // Dense IDs are used to slice packed indirect/visibility buffers.
        // If dense order changes frame-to-frame, geometry will read the wrong slice → flicker.
        std::sort(dense.begin(), dense.end(), [](const DenseGeo& a, const DenseGeo& b)
        {
            return a.Handle.Index < b.Handle.Index;
        });

        const uint32_t geometryCount = static_cast<uint32_t>(dense.size());
        if (geometryCount == 0)
            return out;

        // Build a dense routing table: GeometryHandle.Index -> DenseGeoId.
        // This allows GPUScene instances to store the stable handle index (sparse), while the culler produces packed per-geometry streams.
        std::vector<uint32_t> handleToDense;
        handleToDense.assign(static_cast<size_t>(maxHandleIndex) + 1u, GPUSceneConstants::kPreserveGeometryId);
        for (uint32_t denseId = 0; denseId < geometryCount; ++denseId)
            handleToDense[dense[denseId].Handle.Index] = denseId;

        {
            uint32_t invalidGeometryHandle = 0;
            uint32_t invalidGeometryMapping = 0;

            auto view = ctx.Scene.GetRegistry().view<ECS::Surface::Component>();
            for (auto entity : view)
            {
                const auto& sc = view.get<ECS::Surface::Component>(entity);
                if (!sc.Geometry.IsValid())
                {
                    ++invalidGeometryHandle;
                    continue;
                }

                if (sc.Geometry.Index >= handleToDense.size())
                {
                    ++invalidGeometryMapping;
                    continue;
                }

                if (handleToDense[sc.Geometry.Index] == GPUSceneConstants::kPreserveGeometryId)
                    ++invalidGeometryMapping;
            }

            if ((invalidGeometryHandle + invalidGeometryMapping) > 0)
            {
                Core::Log::Warn("ForwardCull: invalid geometry handles={}, unmapped geometry IDs={} (frame={})",
                               invalidGeometryHandle, invalidGeometryMapping, ctx.FrameIndex);
            }
        }

        const uint32_t frame = (ctx.FrameIndex % FRAMES);

        const uint32_t requiredMapCount = static_cast<uint32_t>(handleToDense.size());
        const size_t requiredMapBytes = std::max<size_t>(size_t(requiredMapCount) * sizeof(uint32_t), GPUSceneConstants::kMinSSBOSize);

        // Allocate / resize per-frame buffers.
        // Each frame slot's buffer is checked independently against the required size.
        // A shared capacity scalar would only resize the current frame's buffer; other
        // in-flight frames would keep stale smaller buffers, causing overflow and flicker.
        const auto needsResize = [](const std::unique_ptr<RHI::VulkanBuffer>& buf, size_t requiredBytes) -> bool
        {
            return !buf || buf->GetSizeBytes() < requiredBytes;
        };

        // Upload mapping buffer (CPU->GPU)
        if (needsResize(m_Stage3HandleToDense[frame], requiredMapBytes))
        {
            m_Stage3HandleToDense[frame] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                requiredMapBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }
        m_Stage3HandleToDense[frame]->Write(handleToDense.data(), requiredMapBytes);

        // Conservative capacity: allow up to the active GPUScene span per geometry.
        // This avoids overflow for now; we can tighten later by tracking per-geometry instance counts.
        const uint32_t maxDrawsPerGeometry = std::max<uint32_t>(ctx.GpuScene->GetActiveCountApprox(), 1u);

        const size_t geoIndexCountBytes = std::max<size_t>(geometryCount * sizeof(uint32_t), 16);
        const size_t drawCountsBytes = std::max<size_t>(geometryCount * sizeof(uint32_t), 16);
        const size_t packedCapacity = static_cast<size_t>(geometryCount) * static_cast<size_t>(maxDrawsPerGeometry);
        const size_t packedIndirectBytes = std::max<size_t>(packedCapacity * sizeof(VkDrawIndexedIndirectCommand), GPUSceneConstants::kMinSSBOSize);
        const size_t packedVisibilityBytes = std::max<size_t>(packedCapacity * sizeof(uint32_t), GPUSceneConstants::kMinSSBOSize);

        if (needsResize(m_Stage3GeometryIndexCount[frame], geoIndexCountBytes))
        {
            m_Stage3GeometryIndexCount[frame] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                geoIndexCountBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU);
        }

        if (needsResize(m_Stage3DrawCountsPacked[frame], drawCountsBytes))
        {
            m_Stage3DrawCountsPacked[frame] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                drawCountsBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);
        }

        // Reallocation is required whenever this specific frame-slot's buffer was built with
        // different parameters — both growth AND shrinkage must retrigger it. Using per-slot
        // trackers (instead of a single shared max) ensures every slot re-syncs independently
        // after scene complexity changes, which is the root cause of the flicker.
        const bool needPackedResize = needsResize(m_Stage3IndirectPacked[frame], packedIndirectBytes) ||
                                      needsResize(m_Stage3VisibilityPacked[frame], packedVisibilityBytes) ||
                                      m_Stage3LastGeometryCount[frame] != geometryCount ||
                                      m_Stage3LastMaxDrawsPerGeometry[frame] != maxDrawsPerGeometry;

        if (needPackedResize)
        {
            m_Stage3IndirectPacked[frame] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                packedIndirectBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            m_Stage3VisibilityPacked[frame] = std::make_unique<RHI::VulkanBuffer>(
                *m_Device,
                packedVisibilityBytes,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_GPU_ONLY);

            // Freshly allocated GPU-only buffers contain undefined memory. Clear on allocation so
            // any over-read slot returns 0 (zero drawCount = nothing drawn) rather than garbage.
        }

        // Record exact values for this slot (not max) so any future change to either parameter
        // also triggers a reallocation rather than silently reusing a mismatched buffer.
        m_Stage3LastGeometryCount[frame] = geometryCount;
        m_Stage3LastMaxDrawsPerGeometry[frame] = maxDrawsPerGeometry;

        // Upload geometry index counts table.
        std::vector<uint32_t> cpuIndexCounts;
        cpuIndexCounts.reserve(geometryCount);
        for (const DenseGeo& g : dense)
            cpuIndexCounts.push_back(g.Geo->GetIndexCount());
        m_Stage3GeometryIndexCount[frame]->Write(cpuIndexCounts.data(), geometryCount * sizeof(uint32_t));

        // Enqueue multi-geometry compute culling.
        {
            const VkBuffer gpuSceneBufferHandle = ctx.GpuScene->GetSceneBuffer().GetHandle();
            const VkBuffer gpuBoundsBufferHandle = ctx.GpuScene->GetBoundsBuffer().GetHandle();

            struct CullPassData
            {
                RGResourceHandle Instances;
                RGResourceHandle Bounds;
                RGResourceHandle GeoIndexCount;
                RGResourceHandle HandleToDense;
                RGResourceHandle Indirect;
                RGResourceHandle Visibility;
                RGResourceHandle DrawCounts;
            };

            // Snapshot frustum.
            std::array<glm::vec4, 6> planes{};
            {
                const glm::mat4 viewProj = ctx.CameraProj * ctx.CameraView;
                const Geometry::Frustum fr = Geometry::Frustum::CreateFromMatrix(viewProj);
                for (int i = 0; i < 6; ++i)
                {
                    const auto& p = fr.Planes[i];
                    planes[i] = glm::vec4(p.Normal, p.Distance);
                }
            }

            const uint32_t totalInstanceCount = ctx.GpuScene->GetActiveCountApprox();

            ctx.Graph.AddPass<CullPassData>("ForwardCull.MultiGeo",
                                            [this, &ctx, frame](CullPassData& data, RGBuilder& builder) mutable
                                            {
                                                const uint32_t fi = frame;
                                                data.Instances = builder.ImportBuffer("GPUScene.Scene"_id, ctx.GpuScene->GetSceneBuffer());
                                                data.Bounds = builder.ImportBuffer("GPUScene.Bounds"_id, ctx.GpuScene->GetBoundsBuffer());
                                                data.GeoIndexCount = builder.ImportBuffer("Stage3.GeoIndexCount"_id, *m_Stage3GeometryIndexCount[fi]);
                                                data.HandleToDense = builder.ImportBuffer("Stage3.HandleToDense"_id, *m_Stage3HandleToDense[fi]);
                                                data.Indirect = builder.ImportBuffer("Stage3.IndirectPacked"_id, *m_Stage3IndirectPacked[fi]);
                                                data.Visibility = builder.ImportBuffer("Stage3.VisibilityPacked"_id, *m_Stage3VisibilityPacked[fi]);
                                                data.DrawCounts = builder.ImportBuffer("Stage3.DrawCounts"_id, *m_Stage3DrawCountsPacked[fi]);

                                                builder.Read(data.Instances, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                                builder.Read(data.Bounds, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                                builder.Read(data.GeoIndexCount, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                                builder.Read(data.HandleToDense, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

                                                builder.Write(data.Indirect, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                                builder.Write(data.Visibility, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                                builder.Write(data.DrawCounts, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                                            },
                                            [this,
                                             fi = frame,
                                             planes,
                                             totalInstanceCount,
                                             geometryCount,
                                             maxDrawsPerGeometry,
                                             gpuSceneBufferHandle,
                                             gpuBoundsBufferHandle,
                                             needPackedResize](const CullPassData&, const RGRegistry&, VkCommandBuffer cmd) mutable
                                            {
                                                if (!m_Stage3GeometryIndexCount[fi] || !m_Stage3IndirectPacked[fi] || !m_Stage3VisibilityPacked[fi] || !m_Stage3DrawCountsPacked[fi])
                                                    return;

                                                // Reset drawCounts to 0 for the entire buffer.
                                                vkCmdFillBuffer(cmd, m_Stage3DrawCountsPacked[fi]->GetHandle(), 0, VK_WHOLE_SIZE, 0);

                                                // If we reallocated packed outputs this frame, clear them too.
                                                if (needPackedResize)
                                                {
                                                    vkCmdFillBuffer(cmd, m_Stage3IndirectPacked[fi]->GetHandle(), 0, VK_WHOLE_SIZE, 0);
                                                    vkCmdFillBuffer(cmd, m_Stage3VisibilityPacked[fi]->GetHandle(), 0, VK_WHOLE_SIZE, 0);
                                                }

                                                VkDescriptorSet cullSet = m_CullSet[fi];
                                                if (cullSet == VK_NULL_HANDLE)
                                                {
                                                    cullSet = m_CullSetPool->Allocate(m_CullSetLayout);
                                                    m_CullSet[fi] = cullSet;
                                                }
                                                if (cullSet == VK_NULL_HANDLE)
                                                    return;

                                                VkDescriptorBufferInfo inst{.buffer = gpuSceneBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo bounds{.buffer = gpuBoundsBufferHandle, .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo geoIdx{.buffer = m_Stage3GeometryIndexCount[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo map{.buffer = m_Stage3HandleToDense[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo indirect{.buffer = m_Stage3IndirectPacked[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo vis{.buffer = m_Stage3VisibilityPacked[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo counts{.buffer = m_Stage3DrawCountsPacked[fi]->GetHandle(), .offset = 0, .range = VK_WHOLE_SIZE};

                                                VkWriteDescriptorSet w[7]{};
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
                                                setBuf(2, 3, &geoIdx);
                                                setBuf(3, 4, &map);
                                                setBuf(4, 5, &indirect);
                                                setBuf(5, 6, &vis);
                                                setBuf(6, 7, &counts);

                                                vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 7, w, 0, nullptr);

                                                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetHandle());
                                                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_CullPipeline->GetLayout(), 0, 1, &cullSet, 0, nullptr);

                                                struct CullPC
                                                {
                                                    glm::vec4 Planes[6];
                                                    uint32_t TotalInstanceCount;
                                                    uint32_t GeometryCount;
                                                    uint32_t MaxDrawsPerGeometry;
                                                    uint32_t DebugFlags;
                                                } pc{};

                                                for (int i = 0; i < 6; ++i)
                                                    pc.Planes[i] = planes[i];
                                                pc.TotalInstanceCount = totalInstanceCount;
                                                pc.GeometryCount = geometryCount;
                                                pc.MaxDrawsPerGeometry = maxDrawsPerGeometry;

                                                vkCmdPushConstants(cmd, m_CullPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPC), &pc);

                                                const uint32_t groups = (totalInstanceCount + SurfacePassConstants::kCullWorkgroupSize - 1) / SurfacePassConstants::kCullWorkgroupSize;
                                                vkCmdDispatch(cmd, groups, 1, 1);
                                            });
        }

        // -----------------------------------------------------------------
        // Resolve per-face attribute buffers from Surface components.
        // Maps geometry handle index → face attribute BDA address.
        // First entity with CachedFaceColors for a geometry wins.
        // -----------------------------------------------------------------
        std::unordered_map<uint32_t, uint64_t> faceAttrByGeoIndex;
        {
            auto visView = ctx.Scene.GetRegistry().view<ECS::Surface::Component>();
            for (auto [entity, sc] : visView.each())
            {
                if (!sc.Geometry.IsValid() || sc.CachedFaceColors.empty() || !sc.ShowPerFaceColors)
                    continue;

                // Only process the first entity per geometry (all instances share face colors).
                if (faceAttrByGeoIndex.contains(sc.Geometry.Index))
                    continue;

                const uint32_t faceCount = static_cast<uint32_t>(sc.CachedFaceColors.size());
                const uint64_t addr = EnsureFaceAttrBuffer(
                    sc.Geometry.Index, sc.CachedFaceColors.data(), faceCount);
                if (addr != 0)
                    faceAttrByGeoIndex[sc.Geometry.Index] = addr;
            }
        }

        // -----------------------------------------------------------------
        // Resolve per-vertex attribute buffers from Surface components.
        // Maps geometry handle index → vertex attribute BDA address.
        // First entity with CachedVertexColors for a geometry wins.
        // Also tracks which geometries want nearest-vertex (Voronoi) rendering
        // and centroid-based Voronoi (PtrCentroids).
        // -----------------------------------------------------------------
        std::unordered_map<uint32_t, uint64_t> vertexAttrByGeoIndex;
        std::unordered_map<uint32_t, bool> nearestVertexByGeoIndex;
        std::unordered_map<uint32_t, uint64_t> centroidByGeoIndex;
        {
            auto visView = ctx.Scene.GetRegistry().view<ECS::Surface::Component>();
            for (auto [entity, sc] : visView.each())
            {
                if (!sc.Geometry.IsValid() || sc.CachedVertexColors.empty() || !sc.ShowPerVertexColors)
                    continue;

                if (vertexAttrByGeoIndex.contains(sc.Geometry.Index))
                    continue;

                // When centroid data is available, upload vertex labels (not colors)
                // to PtrVertexAttr and centroid entries to PtrCentroids.  The shader
                // uses labels to index into the centroid buffer for true Voronoi.
                const bool hasCentroidVoronoi =
                    sc.UseNearestVertexColors
                    && !sc.CachedCentroids.empty()
                    && !sc.CachedVertexLabels.empty()
                    && sc.CachedVertexLabels.size() == sc.CachedVertexColors.size();

                if (hasCentroidVoronoi)
                {
                    // Upload vertex labels as the vertex attribute buffer.
                    const uint32_t vertexCount = static_cast<uint32_t>(sc.CachedVertexLabels.size());
                    const uint64_t labelAddr = EnsureVertexAttrBuffer(
                        sc.Geometry.Index, sc.CachedVertexLabels.data(), vertexCount);
                    if (labelAddr != 0)
                    {
                        vertexAttrByGeoIndex[sc.Geometry.Index] = labelAddr;
                        nearestVertexByGeoIndex[sc.Geometry.Index] = true;

                        const uint32_t k = static_cast<uint32_t>(sc.CachedCentroids.size());
                        const uint64_t centAddr = EnsureCentroidBuffer(
                            sc.Geometry.Index, sc.CachedCentroids.data(), k);
                        if (centAddr != 0)
                            centroidByGeoIndex[sc.Geometry.Index] = centAddr;
                    }
                }
                else
                {
                    // Standard path: upload packed ABGR colors.
                    const uint32_t vertexCount = static_cast<uint32_t>(sc.CachedVertexColors.size());
                    const uint64_t addr = EnsureVertexAttrBuffer(
                        sc.Geometry.Index, sc.CachedVertexColors.data(), vertexCount);
                    if (addr != 0)
                    {
                        vertexAttrByGeoIndex[sc.Geometry.Index] = addr;
                        nearestVertexByGeoIndex[sc.Geometry.Index] = sc.UseNearestVertexColors;
                    }
                }
            }
        }

        // Build draw batches: one per geometry, slicing packed buffers.
        // NOTE: VisibilityBuffer is bound at offset 0 (alignment), and we pass VisibilityBase (element index)
        // via push constants so the vertex shader indexes the packed table correctly.
        for (uint32_t gi = 0; gi < geometryCount; ++gi)
        {
            const DenseGeo& g = dense[gi];

            DrawBatch b{};
            b.GeoHandle = g.Handle;

            b.IndexBuffer = g.Geo->GetIndexBuffer()->GetHandle();
            b.IndexCount = g.Geo->GetIndexCount();

            const uint64_t vbda = g.Geo->GetVertexBuffer()->GetDeviceAddress();
            const auto& layout = g.Geo->GetLayout();
            b.PtrPositions = vbda + layout.PositionsOffset;
            b.PtrNormals = vbda + layout.NormalsOffset;
            b.PtrAux = vbda + layout.AuxOffset;

            switch (g.Geo->GetTopology())
            {
            case PrimitiveTopology::Points: b.Topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
            case PrimitiveTopology::Lines: b.Topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
            case PrimitiveTopology::Triangles:
            default: b.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
            }

            b.PointSizePx = 1.0f;
            if (b.Topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST)
            {
                if (b.GeoHandle.IsValid() && b.GeoHandle.Index < m_PointSizePxByHandleIndex.size())
                    b.PointSizePx = m_PointSizePxByHandleIndex[b.GeoHandle.Index];
                else
                    b.PointSizePx = 4.0f;
            }

            // Per-face attribute buffer (0 = standard shading).
            auto faceIt = faceAttrByGeoIndex.find(g.Handle.Index);
            b.PtrFaceAttr = (faceIt != faceAttrByGeoIndex.end()) ? faceIt->second : 0;

            // Per-vertex attribute buffer (0 = no per-vertex colors).
            auto vertexIt = vertexAttrByGeoIndex.find(g.Handle.Index);
            b.PtrVertexAttr = (vertexIt != vertexAttrByGeoIndex.end()) ? vertexIt->second : 0;

            // Index buffer BDA for nearest-vertex Voronoi rendering.
            // Only set when vertex attrs exist AND nearest-vertex mode is active.
            {
                auto nearestIt = nearestVertexByGeoIndex.find(g.Handle.Index);
                if (nearestIt != nearestVertexByGeoIndex.end() && nearestIt->second
                    && b.PtrVertexAttr != 0 && g.Geo->GetIndexBuffer())
                {
                    b.PtrIndices = g.Geo->GetIndexBuffer()->GetDeviceAddress();
                }
            }

            // Centroid buffer BDA for centroid-based Voronoi rendering.
            {
                auto centIt = centroidByGeoIndex.find(g.Handle.Index);
                if (centIt != centroidByGeoIndex.end())
                    b.PtrCentroids = centIt->second;
            }

            // Packed buffers.
            b.InstanceBuffer = &ctx.GpuScene->GetSceneBuffer();
            b.VisibilityBuffer = m_Stage3VisibilityPacked[frame].get();
            b.IndirectBuffer = m_Stage3IndirectPacked[frame].get();
            b.CountBuffer = m_Stage3DrawCountsPacked[frame].get();
            b.MaxDraws = maxDrawsPerGeometry;

            // Slice offsets.
            b.IndirectOffsetBytes = static_cast<VkDeviceSize>(gi) * static_cast<VkDeviceSize>(maxDrawsPerGeometry) * sizeof(VkDrawIndexedIndirectCommand);
            b.VisibilityBase = gi * maxDrawsPerGeometry; // Element index, not byte offset
            b.CountOffsetBytes = static_cast<VkDeviceSize>(gi) * sizeof(uint32_t);

            out.Batches.push_back(b);
        }

        return out;
    }

    void Graphics::Passes::SurfacePass::AddRasterPass(RenderPassContext& ctx, RGResourceHandle sceneColor, RGResourceHandle depth, DrawStream&& stream)
    {
        // Single raster pass consuming the draw stream. Even with no batches, we still
        // clear/write the canonical HDR scene target so downstream post-processing has
        // a valid producer in empty-scene frames.
        ctx.Graph.AddPass<PassData>("ForwardRaster",
                                    [this, &ctx, sceneColor, depth, fi = ctx.FrameIndex % FRAMES](PassData& data, RGBuilder& builder)
                                    {
                                        RGAttachmentInfo colorInfo{};
                                        colorInfo.ClearValue = {{{0.1f, 0.3f, 0.6f, 1.0f}}};
                                        colorInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        colorInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo depthInfo{};
                                        depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                        data.Color = builder.WriteColor(sceneColor, colorInfo);
                                        data.Depth = builder.WriteDepth(depth, depthInfo);

                                        // Declare dependencies on the GPU-culled buffers to enforce
                                        // compute→graphics barriers between ForwardCull and ForwardRaster.
                                        if (m_Stage3IndirectPacked[fi])
                                        {
                                            auto indirect = builder.ImportBuffer("Stage3.IndirectPacked"_id, *m_Stage3IndirectPacked[fi]);
                                            builder.Read(indirect, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                        }
                                        if (m_Stage3VisibilityPacked[fi])
                                        {
                                            auto visibility = builder.ImportBuffer("Stage3.VisibilityPacked"_id, *m_Stage3VisibilityPacked[fi]);
                                            builder.Read(visibility, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                        }
                                        if (m_Stage3DrawCountsPacked[fi])
                                        {
                                            auto counts = builder.ImportBuffer("Stage3.DrawCounts"_id, *m_Stage3DrawCountsPacked[fi]);
                                            builder.Read(counts, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                        }
                                        if (ctx.GpuScene)
                                        {
                                            auto instances = builder.ImportBuffer("GPUScene.Scene"_id, ctx.GpuScene->GetSceneBuffer());
                                            builder.Read(instances, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                        }
                                    },
                                    [this, &ctx, pipeline = m_Pipeline, stream = std::move(stream), fi = ctx.FrameIndex % FRAMES](const PassData&, const RGRegistry&, VkCommandBuffer cmd) mutable
                                    {
                                        if (stream.Batches.empty())
                                            return;

                                        // NOTE: We bind pipelines per-batch based on topology.
                                        // Vulkan restricts dynamic topology switches unless dynamicPrimitiveTopologyUnrestricted is enabled.

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

                                        // Bind global sets using the base forward pipeline layout.
                                        // (All forward variants share the same descriptor set layouts + push constant range.)
                                        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                0, 1, &ctx.GlobalDescriptorSet,
                                                                1, &dynamicOffset);

                                        VkDescriptorSet globalTextures = ctx.Bindless.GetGlobalSet();
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                pipeline->GetLayout(),
                                                                1, 1, &globalTextures,
                                                                0, nullptr);

                                        // Reuse one persistent instance descriptor set per frame slot.
                                        VkDescriptorSet instanceSet = m_InstanceSet[fi];
                                        if (instanceSet == VK_NULL_HANDLE)
                                        {
                                            instanceSet = m_InstanceSetPool->Allocate(m_InstanceSetLayout);
                                            m_InstanceSet[fi] = instanceSet;
                                            if (instanceSet == VK_NULL_HANDLE)
                                                return;
                                        }
                                        VkBuffer currentInstBuffer = VK_NULL_HANDLE;
                                        VkBuffer currentVisBuffer = VK_NULL_HANDLE;

                                        const RHI::GraphicsPipeline* currentPipeline = nullptr;

                                        for (const DrawBatch& b : stream.Batches)
                                        {
                                            if (!b.InstanceBuffer || !b.VisibilityBuffer || !b.IndirectBuffer)
                                                continue;

                                            const RHI::GraphicsPipeline* desired = [&]() -> const RHI::GraphicsPipeline*
                                            {
                                                switch (b.Topology)
                                                {
                                                case VK_PRIMITIVE_TOPOLOGY_LINE_LIST: return m_LinePipeline ? m_LinePipeline : m_Pipeline;
                                                case VK_PRIMITIVE_TOPOLOGY_POINT_LIST: return m_PointPipeline ? m_PointPipeline : m_Pipeline;
                                                case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
                                                default: return m_Pipeline;
                                                }
                                            }();

                                            if (!desired)
                                                continue;

                                            if (desired != currentPipeline)
                                            {
                                                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desired->GetHandle());
                                                currentPipeline = desired;
                                            }

                                            const VkBuffer instHandle = b.InstanceBuffer->GetHandle();
                                            const VkBuffer visHandle = b.VisibilityBuffer->GetHandle();

                                            if (instHandle != currentInstBuffer || visHandle != currentVisBuffer)
                                            {
                                                VkDescriptorBufferInfo instInfo{.buffer = instHandle, .offset = 0, .range = VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo visInfo{.buffer = visHandle, .offset = 0, .range = VK_WHOLE_SIZE};

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
                                                currentInstBuffer = instHandle;
                                                currentVisBuffer = visHandle;
                                            }

                                            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                    desired->GetLayout(),
                                                                    2, 1, &instanceSet,
                                                                    0, nullptr);

                                            vkCmdBindIndexBuffer(cmd, b.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                                            vkCmdSetPrimitiveTopology(cmd, b.Topology);

                                            // NOTE: Forward point rendering uses fixed pixel-size points via gl_PointSize.
                                            // We default to 4px for visibility; later we can plumb per-entity UI values.
                                            RHI::MeshPushConstants push{
                                                .Model = glm::mat4(1.0f),
                                                .PtrPositions = b.PtrPositions,
                                                .PtrNormals = b.PtrNormals,
                                                .PtrAux = b.PtrAux,
                                                .VisibilityBase = b.VisibilityBase,
                                                .PointSizePx = (b.Topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) ? b.PointSizePx : 1.0f,
                                                .PtrFaceAttr = b.PtrFaceAttr,
                                                .PtrVertexAttr = b.PtrVertexAttr,
                                                .PtrIndices = b.PtrIndices,
                                                .PtrCentroids = b.PtrCentroids
                                            };
                                            vkCmdPushConstants(cmd, desired->GetLayout(),
                                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                               0, sizeof(RHI::MeshPushConstants), &push);

                                            const uint32_t maxDraws = b.MaxDraws;
                                            if (maxDraws == 0)
                                                continue;

                                            const VkDeviceSize indirectSize = b.IndirectBuffer->GetSizeBytes();
                                            if (indirectSize <= b.IndirectOffsetBytes)
                                                continue;

                                            const VkDeviceSize availableBytes = indirectSize - b.IndirectOffsetBytes;
                                            const uint32_t maxDrawsInSlice = static_cast<uint32_t>(availableBytes / sizeof(VkDrawIndexedIndirectCommand));
                                            const uint32_t safeMaxDraws = std::min(maxDraws, maxDrawsInSlice);
                                            if (safeMaxDraws == 0)
                                                continue;

                                            if (b.CountBuffer && vkCmdDrawIndexedIndirectCountKHR)
                                            {
                                                const VkDeviceSize countSize = b.CountBuffer->GetSizeBytes();
                                                if (countSize <= b.CountOffsetBytes)
                                                    continue;

                                                vkCmdDrawIndexedIndirectCountKHR(cmd,
                                                                                 b.IndirectBuffer->GetHandle(),
                                                                                 b.IndirectOffsetBytes,
                                                                                 b.CountBuffer->GetHandle(),
                                                                                 b.CountOffsetBytes,
                                                                                 safeMaxDraws,
                                                                                 sizeof(VkDrawIndexedIndirectCommand));
                                            }
                                            else
                                            {
                                                vkCmdDrawIndexedIndirect(cmd,
                                                                         b.IndirectBuffer->GetHandle(),
                                                                         b.IndirectOffsetBytes,
                                                                         safeMaxDraws,
                                                                         sizeof(VkDrawIndexedIndirectCommand));
                                            }
                                        }
                                    });
    }

    // =========================================================================
    // G-Buffer Raster Pass (deferred path)
    // =========================================================================
    void Graphics::Passes::SurfacePass::AddGBufferRasterPass(
        RenderPassContext& ctx,
        RGResourceHandle normal, RGResourceHandle albedo,
        RGResourceHandle material, RGResourceHandle depth,
        DrawStream&& stream)
    {
        // G-buffer pass: writes to 3 MRT color targets + depth.
        // Even with no batches, we still clear/write so downstream composition
        // has valid producers in empty-scene frames.
        ctx.Graph.AddPass<GBufferPassData>("GBufferRaster",
                                    [this, &ctx, normal, albedo, material, depth, fi = ctx.FrameIndex % FRAMES]
                                    (GBufferPassData& data, RGBuilder& builder)
                                    {
                                        RGAttachmentInfo normalInfo{};
                                        normalInfo.ClearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
                                        normalInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        normalInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo albedoInfo{};
                                        albedoInfo.ClearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
                                        albedoInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        albedoInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo materialInfo{};
                                        materialInfo.ClearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
                                        materialInfo.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                        materialInfo.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;

                                        RGAttachmentInfo depthInfo{};
                                        depthInfo.ClearValue.depthStencil = {1.0f, 0};

                                        data.Normal   = builder.WriteColor(normal, normalInfo);
                                        data.Albedo   = builder.WriteColor(albedo, albedoInfo);
                                        data.Material = builder.WriteColor(material, materialInfo);
                                        data.Depth    = builder.WriteDepth(depth, depthInfo);

                                        // Declare dependencies on GPU-culled buffers (same as forward).
                                        if (m_Stage3IndirectPacked[fi])
                                        {
                                            auto indirect = builder.ImportBuffer("Stage3.IndirectPacked"_id, *m_Stage3IndirectPacked[fi]);
                                            builder.Read(indirect, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                        }
                                        if (m_Stage3VisibilityPacked[fi])
                                        {
                                            auto visibility = builder.ImportBuffer("Stage3.VisibilityPacked"_id, *m_Stage3VisibilityPacked[fi]);
                                            builder.Read(visibility, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                        }
                                        if (m_Stage3DrawCountsPacked[fi])
                                        {
                                            auto counts = builder.ImportBuffer("Stage3.DrawCounts"_id, *m_Stage3DrawCountsPacked[fi]);
                                            builder.Read(counts, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
                                        }
                                        if (ctx.GpuScene)
                                        {
                                            auto instances = builder.ImportBuffer("GPUScene.Scene"_id, ctx.GpuScene->GetSceneBuffer());
                                            builder.Read(instances, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
                                        }
                                    },
                                    [this, &ctx, gbufPipeline = m_GBufferPipeline, stream = std::move(stream), fi = ctx.FrameIndex % FRAMES]
                                    (const GBufferPassData&, const RGRegistry&, VkCommandBuffer cmd) mutable
                                    {
                                        if (stream.Batches.empty())
                                            return;

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

                                        // Bind global sets using the G-buffer pipeline layout.
                                        // G-buffer pipeline shares the same descriptor set layouts as forward.
                                        const uint32_t dynamicOffset = static_cast<uint32_t>(ctx.GlobalCameraDynamicOffset);
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                gbufPipeline->GetLayout(),
                                                                0, 1, &ctx.GlobalDescriptorSet,
                                                                1, &dynamicOffset);

                                        VkDescriptorSet globalTextures = ctx.Bindless.GetGlobalSet();
                                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                gbufPipeline->GetLayout(),
                                                                1, 1, &globalTextures,
                                                                0, nullptr);

                                        VkDescriptorSet instanceSet = m_InstanceSet[fi];
                                        if (instanceSet == VK_NULL_HANDLE)
                                        {
                                            instanceSet = m_InstanceSetPool->Allocate(m_InstanceSetLayout);
                                            m_InstanceSet[fi] = instanceSet;
                                            if (instanceSet == VK_NULL_HANDLE)
                                                return;
                                        }
                                        VkBuffer currentInstBuffer = VK_NULL_HANDLE;
                                        VkBuffer currentVisBuffer = VK_NULL_HANDLE;

                                        const RHI::GraphicsPipeline* currentPipeline = nullptr;

                                        for (const DrawBatch& b : stream.Batches)
                                        {
                                            if (!b.InstanceBuffer || !b.VisibilityBuffer || !b.IndirectBuffer)
                                                continue;

                                            // Only triangle batches go through the G-buffer pipeline.
                                            // Lines and points fall back to the forward pipeline (they
                                            // shouldn't appear here but guard defensively).
                                            const RHI::GraphicsPipeline* desired = gbufPipeline;
                                            if (b.Topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                                                continue; // Skip non-triangle topology in G-buffer pass.

                                            if (!desired)
                                                continue;

                                            if (desired != currentPipeline)
                                            {
                                                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, desired->GetHandle());
                                                currentPipeline = desired;
                                            }

                                            const VkBuffer instHandle = b.InstanceBuffer->GetHandle();
                                            const VkBuffer visHandle = b.VisibilityBuffer->GetHandle();

                                            if (instHandle != currentInstBuffer || visHandle != currentVisBuffer)
                                            {
                                                VkDescriptorBufferInfo instInfo{instHandle, 0, VK_WHOLE_SIZE};
                                                VkDescriptorBufferInfo visInfo{visHandle, 0, VK_WHOLE_SIZE};
                                                VkWriteDescriptorSet writes[2]{};
                                                writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                                writes[0].dstSet = instanceSet;
                                                writes[0].dstBinding = 0;
                                                writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                                writes[0].descriptorCount = 1;
                                                writes[0].pBufferInfo = &instInfo;
                                                writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                                                writes[1].dstSet = instanceSet;
                                                writes[1].dstBinding = 1;
                                                writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                                writes[1].descriptorCount = 1;
                                                writes[1].pBufferInfo = &visInfo;
                                                vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 2, writes, 0, nullptr);

                                                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                                        desired->GetLayout(),
                                                                        2, 1, &instanceSet, 0, nullptr);

                                                currentInstBuffer = instHandle;
                                                currentVisBuffer = visHandle;
                                            }

                                            RHI::MeshPushConstants pc{};
                                            pc.Model = glm::mat4(1.0f);
                                            pc.PtrPositions = b.PtrPositions;
                                            pc.PtrNormals = b.PtrNormals;
                                            pc.PtrAux = 0;
                                            pc.VisibilityBase = b.VisibilityBase;
                                            pc.PointSizePx = b.PointSizePx;
                                            pc.PtrFaceAttr = b.PtrFaceAttr;
                                            pc.PtrVertexAttr = b.PtrVertexAttr;
                                            pc.PtrIndices = b.PtrIndices;
                                            pc.PtrCentroids = b.PtrCentroids;

                                            vkCmdPushConstants(cmd, desired->GetLayout(),
                                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                                               0, sizeof(pc), &pc);

                                            if (b.IndexBuffer != VK_NULL_HANDLE)
                                                vkCmdBindIndexBuffer(cmd, b.IndexBuffer, 0, VK_INDEX_TYPE_UINT32);

                                            const uint32_t safeMaxDraws = std::max(b.MaxDraws, 1u);
                                            if (b.CountBuffer)
                                            {
                                                vkCmdDrawIndexedIndirectCount(cmd,
                                                                             b.IndirectBuffer->GetHandle(),
                                                                             b.IndirectOffsetBytes,
                                                                             b.CountBuffer->GetHandle(),
                                                                             b.CountOffsetBytes,
                                                                             safeMaxDraws,
                                                                             sizeof(VkDrawIndexedIndirectCommand));
                                            }
                                            else
                                            {
                                                vkCmdDrawIndexedIndirect(cmd,
                                                                         b.IndirectBuffer->GetHandle(),
                                                                         b.IndirectOffsetBytes,
                                                                         safeMaxDraws,
                                                                         sizeof(VkDrawIndexedIndirectCommand));
                                            }
                                        }
                                    });
    }

    // =========================================================================
    // Stage 3: GPU-driven culling with single shared geometry
    // =========================================================================
    void Graphics::Passes::SurfacePass::AddStage3Passes(RenderPassContext& ctx,
                                      RGResourceHandle /*backbuffer*/,
                                      RGResourceHandle /*depth*/,
                                      Geometry::GeometryHandle singleGeometry)
    {
        const VkBuffer gpuSceneBufferHandle = ctx.GpuScene->GetSceneBuffer().GetHandle();
        const VkBuffer gpuBoundsBufferHandle = ctx.GpuScene->GetBoundsBuffer().GetHandle();


        // Per-frame persistent state for the Stage 3 validation path.
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

            // Use the pre-validated single geometry passed from AddPasses()
            stage3.GeoHandle = singleGeometry;

            if (stage3.GeoHandle.IsValid())
            {
                stage3.InstanceCount = ctx.GpuScene->GetActiveCountApprox();

                GeometryGpuData* geo = ctx.GeometryStorage.GetIfValid(stage3.GeoHandle);
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
                                            const size_t indirectBytes = std::max<size_t>(maxDraws * sizeof(VkDrawIndexedIndirectCommand), GPUSceneConstants::kMinSSBOSize);
                                            const size_t remapBytes = std::max<size_t>(maxDraws * sizeof(uint32_t), GPUSceneConstants::kMinSSBOSize);

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

                                            VkDescriptorSet cullSet = m_CullSet[fi];
                                            if (cullSet == VK_NULL_HANDLE)
                                            {
                                                cullSet = m_CullSetPool->Allocate(m_CullSetLayout);
                                                m_CullSet[fi] = cullSet;
                                            }
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

                                            const uint32_t groups = (stage3.InstanceCount + SurfacePassConstants::kCullWorkgroupSize - 1) / Graphics::Passes::SurfacePassConstants::kCullWorkgroupSize;
                                            vkCmdDispatch(cmd, groups, 1, 1);
                                        });

        // NOTE: No raster pass here.
        // The unified forward path records exactly ONE raster pass (ForwardRaster) that consumes
        // the produced indirect/count/visibility buffers.
    }

    void Graphics::Passes::SurfacePass::AddStage1And2Passes(RenderPassContext& ctx,
                                          RGResourceHandle /*backbuffer*/,
                                          RGResourceHandle /*depth*/)
    {
        (void)ctx;
        // CPU producer is implemented directly in BuildDrawStream().
        // This function is kept only as an ABI-stable stub while we transition away from the old design.
        // Intentionally empty.
    }

    // =========================================================================
    // EnsureFaceAttrBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable per-face attribute buffer
    // for a geometry. Data is an array of packed ABGR uint32_t, one per face.
    // Returns the buffer device address, or 0 on failure.

    uint64_t SurfacePass::EnsureFaceAttrBuffer(
        uint32_t geoIndex,
        const uint32_t* colorData,
        uint32_t faceCount)
    {
        return EnsurePerEntityBuffer<uint32_t>(
            *m_Device, m_FaceAttrBuffers, geoIndex, colorData, faceCount, "SurfacePass");
    }

    // =========================================================================
    // EnsureVertexAttrBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable per-vertex attribute buffer
    // for a geometry. Same pattern as EnsureFaceAttrBuffer.

    uint64_t SurfacePass::EnsureVertexAttrBuffer(
        uint32_t geoIndex,
        const uint32_t* colorData,
        uint32_t vertexCount)
    {
        return EnsurePerEntityBuffer<uint32_t>(
            *m_Device, m_VertexAttrBuffers, geoIndex, colorData, vertexCount, "SurfacePass");
    }

    // =========================================================================
    // EnsureCentroidBuffer
    // =========================================================================
    // Creates or updates a persistent BDA-addressable centroid buffer for a
    // geometry. Data is an array of {vec3 pos, uint32_t packedColor} entries.

    uint64_t SurfacePass::EnsureCentroidBuffer(
        uint32_t geoIndex,
        const ECS::Surface::Component::CentroidEntry* entries,
        uint32_t centroidCount)
    {
        // Convert from ECS struct to GPU-packed struct.
        std::vector<GpuCentroidEntry> gpu(centroidCount);
        for (uint32_t i = 0; i < centroidCount; ++i)
        {
            gpu[i].x = entries[i].Position.x;
            gpu[i].y = entries[i].Position.y;
            gpu[i].z = entries[i].Position.z;
            gpu[i].packedColor = entries[i].PackedColor;
        }
        return EnsurePerEntityBuffer<GpuCentroidEntry>(
            *m_Device, m_CentroidBuffers, geoIndex, gpu.data(), centroidCount, "SurfacePass");
    }

    // -----------------------------------------------------------------
    // Transient triangle submission (three-pass architecture)
    // -----------------------------------------------------------------

    void SurfacePass::SubmitTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                                     const glm::vec3& normal, uint32_t color)
    {
        if (!Geometry::Validation::IsFinite(a) || !Geometry::Validation::IsFinite(b) ||
            !Geometry::Validation::IsFinite(c) || !Geometry::Validation::IsFinite(normal))
            return;

        m_TransientVertices.push_back({a, color, normal, 0.0f});
        m_TransientVertices.push_back({b, color, normal, 0.0f});
        m_TransientVertices.push_back({c, color, normal, 0.0f});
    }

    void SurfacePass::ResetTransient()
    {
        m_TransientVertices.clear();
    }
}
