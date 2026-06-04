module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

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
}
