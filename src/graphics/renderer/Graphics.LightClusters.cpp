module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Extrinsic.Graphics.LightClusters;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] constexpr std::uint32_t CeilDiv(const std::uint32_t value,
                                                       const std::uint32_t divisor) noexcept
        {
            return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
        }

        [[nodiscard]] bool IsFinitePositive(const float value) noexcept
        {
            return std::isfinite(value) && value > 0.f;
        }

        void IncludeCorner(ClusterGridAABB& out,
                           const float ndcX,
                           const float ndcY,
                           const float positiveViewZ,
                           const ClusterGridProjection& projection) noexcept
        {
            const float x = ndcX * positiveViewZ / projection.ProjectionScaleX;
            const float y = ndcY * positiveViewZ / projection.ProjectionScaleY;
            const float z = -positiveViewZ;
            out.MinX = std::min(out.MinX, x);
            out.MinY = std::min(out.MinY, y);
            out.MinZ = std::min(out.MinZ, z);
            out.MaxX = std::max(out.MaxX, x);
            out.MaxY = std::max(out.MaxY, y);
            out.MaxZ = std::max(out.MaxZ, z);
        }

        [[nodiscard]] glm::vec3 AABBMin(const ClusterGridAABB& cell) noexcept
        {
            return {cell.MinX, cell.MinY, cell.MinZ};
        }

        [[nodiscard]] glm::vec3 AABBMax(const ClusterGridAABB& cell) noexcept
        {
            return {cell.MaxX, cell.MaxY, cell.MaxZ};
        }

        [[nodiscard]] bool IsFiniteVec3(const glm::vec3 v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] glm::vec3 NormalizeOrZero(const glm::vec3 v) noexcept
        {
            const float len2 = glm::dot(v, v);
            if (!std::isfinite(len2) || len2 <= 1e-12f)
            {
                return glm::vec3{0.f};
            }
            return v / std::sqrt(len2);
        }

        [[nodiscard]] float Clamp01(const float value) noexcept
        {
            return std::clamp(value, 0.f, 1.f);
        }

        [[nodiscard]] constexpr std::uint32_t ClampMaxLightsPerCell(const std::uint32_t value) noexcept
        {
            return std::min(value, kMaxClusterLightsPerCell);
        }

        [[nodiscard]] float DistanceSquaredPointAABB(const glm::vec3 point,
                                                     const glm::vec3 minV,
                                                     const glm::vec3 maxV) noexcept
        {
            const glm::vec3 closest{
                std::clamp(point.x, minV.x, maxV.x),
                std::clamp(point.y, minV.y, maxV.y),
                std::clamp(point.z, minV.z, maxV.z),
            };
            const glm::vec3 delta = point - closest;
            return glm::dot(delta, delta);
        }

        [[nodiscard]] bool IsValidAABB(const ClusterGridAABB& cell) noexcept
        {
            return std::isfinite(cell.MinX) && std::isfinite(cell.MinY) &&
                   std::isfinite(cell.MinZ) && std::isfinite(cell.MaxX) &&
                   std::isfinite(cell.MaxY) && std::isfinite(cell.MaxZ) &&
                   cell.MinX <= cell.MaxX && cell.MinY <= cell.MaxY && cell.MinZ <= cell.MaxZ;
        }

        [[nodiscard]] bool SphereIntersectsAABB(const glm::vec3 center,
                                                const float radius,
                                                const ClusterGridAABB& cell) noexcept
        {
            if (!IsFiniteVec3(center) || !IsFinitePositive(radius) || !IsValidAABB(cell))
            {
                return false;
            }
            const float d2 = DistanceSquaredPointAABB(center, AABBMin(cell), AABBMax(cell));
            return d2 <= radius * radius;
        }

        [[nodiscard]] bool ConservativeSpotIntersectsAABB(const LightSnapshot& light,
                                                          const ClusterGridAABB& cell) noexcept
        {
            if (!SphereIntersectsAABB(light.Position, light.Range, cell))
            {
                return false;
            }

            const glm::vec3 dir = NormalizeOrZero(light.Direction);
            if (glm::dot(dir, dir) <= 0.f)
            {
                return true;
            }

            const glm::vec3 minV = AABBMin(cell);
            const glm::vec3 maxV = AABBMax(cell);
            const glm::vec3 center = (minV + maxV) * 0.5f;
            const glm::vec3 halfExtent = (maxV - minV) * 0.5f;
            const float radius = glm::length(halfExtent);
            const glm::vec3 toCenter = center - light.Position;
            const float centerDistance2 = glm::dot(toCenter, toCenter);
            if (!std::isfinite(centerDistance2))
            {
                return false;
            }
            if (centerDistance2 <= radius * radius)
            {
                return true;
            }

            const float axial = glm::dot(toCenter, dir);
            if (axial + radius < 0.f || axial - radius > light.Range)
            {
                return false;
            }

            const float outerCos = Clamp01(light.OuterConeCos);
            if (outerCos <= 0.f)
            {
                return true;
            }

            const float sinOuter = std::sqrt(std::max(0.f, 1.f - outerCos * outerCos));
            const float radial2 = std::max(0.f, centerDistance2 - axial * axial);
            const float radial = std::sqrt(radial2);
            const float coneRadiusAtCenter = std::max(0.f, axial) * (sinOuter / outerCos);

            // Bounding-sphere SAT approximation: include uncertain cells by
            // inflating the cone radius by the cell sphere radius. This can
            // over-include, but it does not drop a contributing spot light.
            return radial <= coneRadiusAtCenter + radius;
        }
    }

    bool ClusterGridProjection::IsValid() const noexcept
    {
        return IsFinitePositive(ProjectionScaleX) && IsFinitePositive(ProjectionScaleY) &&
               IsFinitePositive(NearZ) && IsFinitePositive(FarZ) && FarZ > NearZ;
    }

    ClusterGridDesc ComputeClusterGridDesc(const std::uint32_t renderWidth,
                                           const std::uint32_t renderHeight,
                                           const std::uint32_t clusterTilePx,
                                           const std::uint32_t slicesZ) noexcept
    {
        if (renderWidth == 0u || renderHeight == 0u || clusterTilePx == 0u || slicesZ == 0u)
        {
            return ClusterGridDesc{};
        }

        ClusterGridDesc desc{};
        desc.RenderWidth = renderWidth;
        desc.RenderHeight = renderHeight;
        desc.ClusterTilePx = clusterTilePx;
        desc.TilesX = CeilDiv(renderWidth, clusterTilePx);
        desc.TilesY = CeilDiv(renderHeight, clusterTilePx);
        desc.SlicesZ = slicesZ;
        desc.CellCount = desc.TilesX * desc.TilesY * desc.SlicesZ;
        return desc;
    }

    std::uint64_t ComputeClusterGridAABBBufferSizeBytes(const ClusterGridDesc& desc) noexcept
    {
        if (!desc.IsValid())
        {
            return 0u;
        }
        return static_cast<std::uint64_t>(desc.CellCount) * sizeof(ClusterGridAABB);
    }

    RHI::BufferDesc BuildClusterGridAABBBufferDesc(const ClusterGridDesc& desc) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = ComputeClusterGridAABBBufferSizeBytes(desc),
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc,
            .HostVisible = false,
            .DebugName = "ClusterGrid.AABBs",
        };
    }

    std::uint64_t ComputeClusterLightHeaderBufferSizeBytes(const ClusterGridDesc& desc) noexcept
    {
        if (!desc.IsValid())
        {
            return 0u;
        }
        return static_cast<std::uint64_t>(desc.CellCount) * sizeof(ClusterLightCellHeader);
    }

    std::uint64_t ComputeClusterLightIndexBufferSizeBytes(const ClusterGridDesc& desc,
                                                          const std::uint32_t maxLightsPerCell) noexcept
    {
        if (!desc.IsValid() || maxLightsPerCell == 0u)
        {
            return 0u;
        }
        const std::uint32_t cappedMaxLightsPerCell = ClampMaxLightsPerCell(maxLightsPerCell);
        return static_cast<std::uint64_t>(desc.CellCount) *
               static_cast<std::uint64_t>(cappedMaxLightsPerCell) *
               sizeof(std::uint32_t);
    }

    std::uint64_t ComputeClusterLightCounterBufferSizeBytes() noexcept
    {
        return sizeof(std::uint32_t);
    }

    RHI::BufferDesc BuildClusterLightHeaderBufferDesc(const ClusterGridDesc& desc) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = ComputeClusterLightHeaderBufferSizeBytes(desc),
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = "ClusterLights.Headers",
        };
    }

    RHI::BufferDesc BuildClusterLightIndexBufferDesc(const ClusterGridDesc& desc,
                                                     const std::uint32_t maxLightsPerCell) noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = ComputeClusterLightIndexBufferSizeBytes(desc, maxLightsPerCell),
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = "ClusterLights.Indices",
        };
    }

    RHI::BufferDesc BuildClusterLightCounterBufferDesc() noexcept
    {
        return RHI::BufferDesc{
            .SizeBytes = ComputeClusterLightCounterBufferSizeBytes(),
            .Usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc | RHI::BufferUsage::TransferDst,
            .HostVisible = false,
            .DebugName = "ClusterLights.Counter",
        };
    }

    ClusterGridProjection BuildClusterGridProjectionFromVerticalFov(const float verticalFovRadians,
                                                                    const float aspectRatio,
                                                                    const float nearZ,
                                                                    const float farZ) noexcept
    {
        if (!IsFinitePositive(verticalFovRadians) || !IsFinitePositive(aspectRatio) ||
            !IsFinitePositive(nearZ) || !IsFinitePositive(farZ) || farZ <= nearZ)
        {
            return ClusterGridProjection{};
        }

        const float halfFovTan = std::tan(verticalFovRadians * 0.5f);
        if (!IsFinitePositive(halfFovTan))
        {
            return ClusterGridProjection{};
        }

        return ClusterGridProjection{
            .ProjectionScaleX = 1.f / (halfFovTan * aspectRatio),
            .ProjectionScaleY = 1.f / halfFovTan,
            .NearZ = nearZ,
            .FarZ = farZ,
        };
    }

    ClusterSliceMapping MapViewZToClusterSlice(const float positiveViewZ,
                                               const ClusterGridProjection& projection,
                                               const std::uint32_t slicesZ) noexcept
    {
        if (!projection.IsValid() || slicesZ == 0u || !IsFinitePositive(positiveViewZ))
        {
            return ClusterSliceMapping{};
        }

        if (positiveViewZ > projection.FarZ)
        {
            return ClusterSliceMapping{.Slice = slicesZ - 1u, .InRange = false};
        }

        const float clampedZ = std::max(positiveViewZ, projection.NearZ);
        const float logRange = std::log(projection.FarZ / projection.NearZ);
        if (!IsFinitePositive(logRange))
        {
            return ClusterSliceMapping{};
        }

        const float normalized = std::log(clampedZ / projection.NearZ) / logRange;
        std::uint32_t slice = static_cast<std::uint32_t>(std::floor(normalized * static_cast<float>(slicesZ)));
        if (slice >= slicesZ)
        {
            slice = slicesZ - 1u;
        }
        return ClusterSliceMapping{.Slice = slice, .InRange = true};
    }

    ClusterDepthSlice ComputeClusterDepthSlice(const ClusterGridDesc& desc,
                                               const ClusterGridProjection& projection,
                                               const std::uint32_t sliceZ) noexcept
    {
        if (!desc.IsValid() || !projection.IsValid() || sliceZ >= desc.SlicesZ)
        {
            return ClusterDepthSlice{};
        }

        const float logRange = std::log(projection.FarZ / projection.NearZ);
        if (!IsFinitePositive(logRange))
        {
            return ClusterDepthSlice{};
        }

        const float invSlices = 1.f / static_cast<float>(desc.SlicesZ);
        const float nearZ = projection.NearZ * std::exp(logRange * static_cast<float>(sliceZ) * invSlices);
        const float farZ = (sliceZ + 1u == desc.SlicesZ)
            ? projection.FarZ
            : projection.NearZ * std::exp(logRange * static_cast<float>(sliceZ + 1u) * invSlices);

        if (!IsFinitePositive(nearZ) || !IsFinitePositive(farZ) || farZ <= nearZ)
        {
            return ClusterDepthSlice{};
        }
        return ClusterDepthSlice{.NearZ = nearZ, .FarZ = farZ, .Valid = true};
    }

    std::uint32_t ComputeClusterCellIndex(const ClusterGridDesc& desc,
                                          const std::uint32_t tileX,
                                          const std::uint32_t tileY,
                                          const std::uint32_t sliceZ) noexcept
    {
        if (!desc.IsValid() || tileX >= desc.TilesX || tileY >= desc.TilesY || sliceZ >= desc.SlicesZ)
        {
            return kInvalidClusterCellIndex;
        }
        return (sliceZ * desc.TilesY + tileY) * desc.TilesX + tileX;
    }

    ClusterGridAABB ComputeClusterCellAABB(const ClusterGridDesc& desc,
                                           const ClusterGridProjection& projection,
                                           const std::uint32_t tileX,
                                           const std::uint32_t tileY,
                                           const std::uint32_t sliceZ) noexcept
    {
        if (ComputeClusterCellIndex(desc, tileX, tileY, sliceZ) == kInvalidClusterCellIndex ||
            !projection.IsValid())
        {
            return ClusterGridAABB{};
        }

        const ClusterDepthSlice depth = ComputeClusterDepthSlice(desc, projection, sliceZ);
        if (!depth.Valid)
        {
            return ClusterGridAABB{};
        }

        const std::uint64_t pixelMinX =
            static_cast<std::uint64_t>(tileX) * static_cast<std::uint64_t>(desc.ClusterTilePx);
        const std::uint64_t pixelMinY =
            static_cast<std::uint64_t>(tileY) * static_cast<std::uint64_t>(desc.ClusterTilePx);
        const std::uint64_t pixelMaxX = std::min(
            static_cast<std::uint64_t>(tileX + 1u) * static_cast<std::uint64_t>(desc.ClusterTilePx),
            static_cast<std::uint64_t>(desc.RenderWidth));
        const std::uint64_t pixelMaxY = std::min(
            static_cast<std::uint64_t>(tileY + 1u) * static_cast<std::uint64_t>(desc.ClusterTilePx),
            static_cast<std::uint64_t>(desc.RenderHeight));

        const float ndcMinX = (2.f * static_cast<float>(pixelMinX) / static_cast<float>(desc.RenderWidth)) - 1.f;
        const float ndcMaxX = (2.f * static_cast<float>(pixelMaxX) / static_cast<float>(desc.RenderWidth)) - 1.f;
        const float ndcMinY = 1.f - (2.f * static_cast<float>(pixelMaxY) / static_cast<float>(desc.RenderHeight));
        const float ndcMaxY = 1.f - (2.f * static_cast<float>(pixelMinY) / static_cast<float>(desc.RenderHeight));

        ClusterGridAABB out{};
        out.MinX = out.MinY = out.MinZ = std::numeric_limits<float>::max();
        out.MaxX = out.MaxY = out.MaxZ = -std::numeric_limits<float>::max();

        for (const float z : {depth.NearZ, depth.FarZ})
        {
            IncludeCorner(out, ndcMinX, ndcMinY, z, projection);
            IncludeCorner(out, ndcMaxX, ndcMinY, z, projection);
            IncludeCorner(out, ndcMinX, ndcMaxY, z, projection);
            IncludeCorner(out, ndcMaxX, ndcMaxY, z, projection);
        }
        return out;
    }

    ClusterGridBuildDispatchPlan ComputeClusterGridBuildDispatchPlan(const ClusterGridDesc& desc,
                                                                     const std::uint32_t groupSize) noexcept
    {
        ClusterGridBuildDispatchPlan plan{};
        plan.Desc = desc;
        plan.GroupSize = groupSize;
        if (!desc.IsValid() || groupSize == 0u)
        {
            return plan;
        }

        plan.GroupCountX = CeilDiv(desc.CellCount, groupSize);
        plan.GroupCountY = 1u;
        plan.GroupCountZ = 1u;
        return plan;
    }

    ClusterLightAssignmentDispatchPlan ComputeClusterLightAssignmentDispatchPlan(
        const ClusterGridDesc& desc,
        const std::uint32_t lightCount,
        const std::uint32_t maxLightsPerCell,
        const std::uint32_t groupSize) noexcept
    {
        ClusterLightAssignmentDispatchPlan plan{};
        plan.Desc = desc;
        plan.LightCount = lightCount;
        plan.GroupSize = groupSize;
        if (!desc.IsValid() || maxLightsPerCell == 0u || groupSize == 0u)
        {
            return plan;
        }

        plan.MaxLightsPerCell = ClampMaxLightsPerCell(maxLightsPerCell);
        plan.GroupCountX = CeilDiv(desc.CellCount, groupSize);
        plan.GroupCountY = 1u;
        plan.GroupCountZ = 1u;
        return plan;
    }

    bool DoesClusterAABBIntersectLight(const ClusterGridAABB& cell,
                                       const LightSnapshot& light) noexcept
    {
        switch (light.LightType)
        {
        case LightSnapshot::Type::Directional:
            return false;
        case LightSnapshot::Type::Point:
            return SphereIntersectsAABB(light.Position, light.Range, cell);
        case LightSnapshot::Type::Spot:
            return ConservativeSpotIntersectsAABB(light, cell);
        }
        return false;
    }

    ClusterLightAssignmentResult AssignLightsToClusters(const ClusterGridDesc& desc,
                                                        std::span<const ClusterGridAABB> cells,
                                                        std::span<const LightSnapshot> lights,
                                                        const std::uint32_t maxLightsPerCell)
    {
        ClusterLightAssignmentResult result{};
        if (!desc.IsValid() || cells.size() < desc.CellCount || maxLightsPerCell == 0u)
        {
            return result;
        }

        const std::uint32_t cappedMaxLightsPerCell = ClampMaxLightsPerCell(maxLightsPerCell);
        result.Headers.resize(desc.CellCount);
        result.LightIndices.reserve(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(desc.CellCount) * cappedMaxLightsPerCell,
            static_cast<std::uint64_t>(lights.size()) * desc.CellCount));

        std::vector<bool> lightAssigned(lights.size(), false);
        for (std::uint32_t cellIndex = 0u; cellIndex < desc.CellCount; ++cellIndex)
        {
            ClusterLightCellHeader& header = result.Headers[cellIndex];
            header.Offset = static_cast<std::uint32_t>(result.LightIndices.size());

            for (std::uint32_t lightIndex = 0u; lightIndex < lights.size(); ++lightIndex)
            {
                const LightSnapshot& light = lights[lightIndex];
                if (!DoesClusterAABBIntersectLight(cells[cellIndex], light))
                {
                    continue;
                }

                lightAssigned[lightIndex] = true;
                if (header.Count < cappedMaxLightsPerCell)
                {
                    result.LightIndices.push_back(lightIndex);
                    ++header.Count;
                }
                else
                {
                    ++result.Diagnostics.LightClusterOverflowCount;
                }
            }

            if (header.Count == 0u)
            {
                ++result.Diagnostics.EmptyClusterCount;
            }
        }

        for (std::uint32_t lightIndex = 0u; lightIndex < lights.size(); ++lightIndex)
        {
            const LightSnapshot& light = lights[lightIndex];
            if (light.LightType != LightSnapshot::Type::Directional && !lightAssigned[lightIndex])
            {
                ++result.Diagnostics.LightsCulledCount;
            }
        }

        result.Valid = true;
        return result;
    }

    bool RecordClusterGridBuild(RHI::ICommandContext& cmd,
                                const RHI::PipelineHandle pipeline,
                                const RHI::BufferHandle aabbBuffer,
                                const ClusterGridBuildDispatchPlan& plan,
                                const ClusterGridProjection& projection)
    {
        if (!pipeline.IsValid() || !aabbBuffer.IsValid() || !plan.IsValid() || !projection.IsValid())
        {
            return false;
        }

        const ClusterGridBuildPushConstants pc{
            .RenderWidth = plan.Desc.RenderWidth,
            .RenderHeight = plan.Desc.RenderHeight,
            .TilesX = plan.Desc.TilesX,
            .TilesY = plan.Desc.TilesY,
            .SlicesZ = plan.Desc.SlicesZ,
            .CellCount = plan.Desc.CellCount,
            .NearZ = projection.NearZ,
            .FarZ = projection.FarZ,
            .ProjectionScaleX = projection.ProjectionScaleX,
            .ProjectionScaleY = projection.ProjectionScaleY,
            .ClusterTilePx = plan.Desc.ClusterTilePx,
            .Reserved0 = 0u,
        };

        cmd.BindPipeline(pipeline);
        cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
        cmd.Dispatch(plan.GroupCountX, plan.GroupCountY, plan.GroupCountZ);

        const RHI::BufferBarrierDesc barrier{
            .Buffer = aabbBuffer,
            .BeforeAccess = RHI::MemoryAccess::ShaderWrite,
            .AfterAccess = RHI::MemoryAccess::ShaderRead,
        };
        cmd.SubmitBarriers(RHI::BarrierBatchDesc{
            .BufferBarriers = std::span<const RHI::BufferBarrierDesc>{&barrier, 1u},
        });
        return true;
    }

    bool RecordClusterLightAssignment(RHI::ICommandContext& cmd,
                                      const RHI::PipelineHandle pipeline,
                                      const RHI::BufferHandle aabbBuffer,
                                      const RHI::BufferHandle lightsBuffer,
                                      const RHI::BufferHandle headerBuffer,
                                      const RHI::BufferHandle indexBuffer,
                                      const RHI::BufferHandle counterBuffer,
                                      const ClusterLightAssignmentDispatchPlan& plan)
    {
        if (!pipeline.IsValid() || !aabbBuffer.IsValid() || !lightsBuffer.IsValid() ||
            !headerBuffer.IsValid() || !indexBuffer.IsValid() || !counterBuffer.IsValid() ||
            !plan.IsValid())
        {
            return false;
        }

        const ClusterLightAssignmentPushConstants pc{
            .CellCount = plan.Desc.CellCount,
            .LightCount = plan.LightCount,
            .MaxLightsPerCell = plan.MaxLightsPerCell,
            .Reserved0 = 0u,
        };

        cmd.FillBuffer(counterBuffer, 0u, ComputeClusterLightCounterBufferSizeBytes(), 0u);
        const RHI::BufferBarrierDesc counterClearBarrier{
            .Buffer = counterBuffer,
            .BeforeAccess = RHI::MemoryAccess::TransferWrite,
            .AfterAccess = RHI::MemoryAccess::ShaderWrite,
        };
        cmd.SubmitBarriers(RHI::BarrierBatchDesc{
            .BufferBarriers = std::span<const RHI::BufferBarrierDesc>{&counterClearBarrier, 1u},
        });

        cmd.BindPipeline(pipeline);
        cmd.PushConstants(&pc, static_cast<std::uint32_t>(sizeof(pc)), 0u);
        cmd.Dispatch(plan.GroupCountX, plan.GroupCountY, plan.GroupCountZ);

        const RHI::BufferBarrierDesc barriers[] = {
            RHI::BufferBarrierDesc{
                .Buffer = headerBuffer,
                .BeforeAccess = RHI::MemoryAccess::ShaderWrite,
                .AfterAccess = RHI::MemoryAccess::ShaderRead,
            },
            RHI::BufferBarrierDesc{
                .Buffer = indexBuffer,
                .BeforeAccess = RHI::MemoryAccess::ShaderWrite,
                .AfterAccess = RHI::MemoryAccess::ShaderRead,
            },
            RHI::BufferBarrierDesc{
                .Buffer = counterBuffer,
                .BeforeAccess = RHI::MemoryAccess::ShaderWrite,
                .AfterAccess = RHI::MemoryAccess::ShaderRead,
            },
        };
        cmd.SubmitBarriers(RHI::BarrierBatchDesc{
            .BufferBarriers = std::span<const RHI::BufferBarrierDesc>{barriers, 3u},
        });
        return true;
    }
}
