module;

#include <cstdint>

export module Extrinsic.Graphics.LightClusters;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t kDefaultClusterTilePx = 80u;
    inline constexpr std::uint32_t kDefaultClusterZSlices = 24u;
    inline constexpr std::uint32_t kClusterGridBuildGroupSize = 64u;
    inline constexpr std::uint32_t kInvalidClusterCellIndex = 0xFFFF'FFFFu;

    // GRAPHICS-039A — froxel grid dimensions from the clustered-light planning
    // contract. XY tiles scale with viewport size; Z stays fixed and logarithmic.
    struct ClusterGridDesc
    {
        std::uint32_t RenderWidth{0u};
        std::uint32_t RenderHeight{0u};
        std::uint32_t ClusterTilePx{kDefaultClusterTilePx};
        std::uint32_t TilesX{0u};
        std::uint32_t TilesY{0u};
        std::uint32_t SlicesZ{kDefaultClusterZSlices};
        std::uint32_t CellCount{0u};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return RenderWidth > 0u && RenderHeight > 0u && ClusterTilePx > 0u &&
                   TilesX > 0u && TilesY > 0u && SlicesZ > 0u && CellCount > 0u;
        }
        [[nodiscard]] bool operator==(const ClusterGridDesc&) const noexcept = default;
    };

    // Shader-visible std430 AABB cell. `MinZ`/`MaxZ` use right-handed view space:
    // camera-forward depths are negative (`MinZ = -far`, `MaxZ = -near`).
    struct alignas(16) ClusterGridAABB
    {
        float MinX{0.f};
        float MinY{0.f};
        float MinZ{0.f};
        float PadMin{0.f};
        float MaxX{0.f};
        float MaxY{0.f};
        float MaxZ{0.f};
        float PadMax{0.f};
    };
    static_assert(sizeof(ClusterGridAABB) == 32u);
    static_assert(alignof(ClusterGridAABB) == 16u);

    struct ClusterGridProjection
    {
        // Perspective projection scale terms matching a symmetric projection:
        // `clip.x = view.x * ProjectionScaleX / -view.z` and same for Y.
        float ProjectionScaleX{0.f};
        float ProjectionScaleY{0.f};
        float NearZ{0.f};
        float FarZ{0.f};

        [[nodiscard]] bool IsValid() const noexcept;
    };

    struct ClusterSliceMapping
    {
        std::uint32_t Slice{0u};
        bool InRange{false};
    };

    struct ClusterDepthSlice
    {
        float NearZ{0.f};
        float FarZ{0.f};
        bool Valid{false};
    };

    struct ClusterGridBuildDispatchPlan
    {
        ClusterGridDesc Desc{};
        std::uint32_t GroupSize{kClusterGridBuildGroupSize};
        std::uint32_t GroupCountX{0u};
        std::uint32_t GroupCountY{1u};
        std::uint32_t GroupCountZ{1u};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Desc.IsValid() && GroupSize > 0u && GroupCountX > 0u &&
                   GroupCountY > 0u && GroupCountZ > 0u;
        }
    };

    // Matches `assets/shaders/cluster_grid_build.comp` std430 push layout.
    struct ClusterGridBuildPushConstants
    {
        std::uint32_t RenderWidth{0u};
        std::uint32_t RenderHeight{0u};
        std::uint32_t TilesX{0u};
        std::uint32_t TilesY{0u};
        std::uint32_t SlicesZ{0u};
        std::uint32_t CellCount{0u};
        float NearZ{0.f};
        float FarZ{0.f};
        float ProjectionScaleX{0.f};
        float ProjectionScaleY{0.f};
        std::uint32_t ClusterTilePx{0u};
        std::uint32_t Reserved0{0u};
    };
    static_assert(sizeof(ClusterGridBuildPushConstants) == 48u);

    [[nodiscard]] ClusterGridDesc ComputeClusterGridDesc(
        std::uint32_t renderWidth,
        std::uint32_t renderHeight,
        std::uint32_t clusterTilePx = kDefaultClusterTilePx,
        std::uint32_t slicesZ = kDefaultClusterZSlices) noexcept;

    [[nodiscard]] std::uint64_t ComputeClusterGridAABBBufferSizeBytes(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] RHI::BufferDesc BuildClusterGridAABBBufferDesc(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] ClusterGridProjection BuildClusterGridProjectionFromVerticalFov(
        float verticalFovRadians,
        float aspectRatio,
        float nearZ,
        float farZ) noexcept;

    [[nodiscard]] ClusterSliceMapping MapViewZToClusterSlice(
        float positiveViewZ,
        const ClusterGridProjection& projection,
        std::uint32_t slicesZ = kDefaultClusterZSlices) noexcept;

    [[nodiscard]] ClusterDepthSlice ComputeClusterDepthSlice(
        const ClusterGridDesc& desc,
        const ClusterGridProjection& projection,
        std::uint32_t sliceZ) noexcept;

    [[nodiscard]] std::uint32_t ComputeClusterCellIndex(
        const ClusterGridDesc& desc,
        std::uint32_t tileX,
        std::uint32_t tileY,
        std::uint32_t sliceZ) noexcept;

    [[nodiscard]] ClusterGridAABB ComputeClusterCellAABB(
        const ClusterGridDesc& desc,
        const ClusterGridProjection& projection,
        std::uint32_t tileX,
        std::uint32_t tileY,
        std::uint32_t sliceZ) noexcept;

    [[nodiscard]] ClusterGridBuildDispatchPlan ComputeClusterGridBuildDispatchPlan(
        const ClusterGridDesc& desc,
        std::uint32_t groupSize = kClusterGridBuildGroupSize) noexcept;

    // Records the backend-neutral compute command shape for rebuilding the
    // cluster AABB buffer. The concrete backend owns descriptor publication.
    bool RecordClusterGridBuild(RHI::ICommandContext& cmd,
                                RHI::PipelineHandle pipeline,
                                RHI::BufferHandle aabbBuffer,
                                const ClusterGridBuildDispatchPlan& plan,
                                const ClusterGridProjection& projection);
}
