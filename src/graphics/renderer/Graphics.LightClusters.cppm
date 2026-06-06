module;

#include <cstdint>
#include <span>
#include <vector>

export module Extrinsic.Graphics.LightClusters;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.Graphics.LightSystem;

export namespace Extrinsic::Graphics
{
    inline constexpr std::uint32_t kDefaultClusterTilePx = 80u;
    inline constexpr std::uint32_t kDefaultClusterZSlices = 24u;
    inline constexpr std::uint32_t kClusterGridBuildGroupSize = 64u;
    inline constexpr std::uint32_t kClusterLightAssignmentGroupSize = 64u;
    inline constexpr std::uint32_t kMaxClusterLightsPerCell = 256u;
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

    // Shader-visible std430 header for one froxel cell. Offset is measured in
    // uint light-index elements inside `ClusterLights.Indices`.
    struct alignas(8) ClusterLightCellHeader
    {
        std::uint32_t Offset{0u};
        std::uint32_t Count{0u};
    };
    static_assert(sizeof(ClusterLightCellHeader) == 8u);
    static_assert(alignof(ClusterLightCellHeader) == 8u);

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

    struct ClusterLightAssignmentDispatchPlan
    {
        ClusterGridDesc Desc{};
        std::uint32_t LightCount{0u};
        std::uint32_t MaxLightsPerCell{kMaxClusterLightsPerCell};
        std::uint32_t GroupSize{kClusterLightAssignmentGroupSize};
        std::uint32_t GroupCountX{0u};
        std::uint32_t GroupCountY{1u};
        std::uint32_t GroupCountZ{1u};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Desc.IsValid() && MaxLightsPerCell > 0u && GroupSize > 0u &&
                   GroupCountX > 0u && GroupCountY > 0u && GroupCountZ > 0u;
        }
    };

    struct ClusterLightAssignmentDiagnostics
    {
        std::uint32_t LightClusterOverflowCount{0u};
        std::uint32_t LightsCulledCount{0u};
        std::uint32_t EmptyClusterCount{0u};
    };

    struct ClusterLightAssignmentResult
    {
        std::vector<ClusterLightCellHeader> Headers{};
        std::vector<std::uint32_t> LightIndices{};
        ClusterLightAssignmentDiagnostics Diagnostics{};
        bool Valid{false};
    };

    // Matches `assets/shaders/cluster_grid_build.comp` scalar push layout.
    // BUG-015: the AABB output buffer is reached through a Buffer Device
    // Address carried in push constants (BDA convention), so the leading 8-byte
    // `ClusterGridBDA` precedes the dimension/projection words.
    struct ClusterGridBuildPushConstants
    {
        std::uint64_t ClusterGridBDA{0u};
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
    static_assert(sizeof(ClusterGridBuildPushConstants) == 56u);

    // Matches `assets/shaders/light_cluster_assign.comp` scalar push layout.
    // BUG-015: every storage buffer (grid AABBs, lights, headers, indices,
    // counter) is reached through a Buffer Device Address carried in push
    // constants. The five 8-byte addresses precede the trailing uint params.
    struct ClusterLightAssignmentPushConstants
    {
        std::uint64_t ClusterGridBDA{0u};
        std::uint64_t LightsBDA{0u};
        std::uint64_t HeadersBDA{0u};
        std::uint64_t IndicesBDA{0u};
        std::uint64_t CounterBDA{0u};
        std::uint32_t CellCount{0u};
        std::uint32_t LightCount{0u};
        std::uint32_t MaxLightsPerCell{0u};
        std::uint32_t Reserved0{0u};
    };
    static_assert(sizeof(ClusterLightAssignmentPushConstants) == 56u);

    [[nodiscard]] ClusterGridDesc ComputeClusterGridDesc(
        std::uint32_t renderWidth,
        std::uint32_t renderHeight,
        std::uint32_t clusterTilePx = kDefaultClusterTilePx,
        std::uint32_t slicesZ = kDefaultClusterZSlices) noexcept;

    [[nodiscard]] std::uint64_t ComputeClusterGridAABBBufferSizeBytes(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] RHI::BufferDesc BuildClusterGridAABBBufferDesc(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] std::uint64_t ComputeClusterLightHeaderBufferSizeBytes(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] std::uint64_t ComputeClusterLightIndexBufferSizeBytes(
        const ClusterGridDesc& desc,
        std::uint32_t maxLightsPerCell = kMaxClusterLightsPerCell) noexcept;

    [[nodiscard]] std::uint64_t ComputeClusterLightCounterBufferSizeBytes() noexcept;

    [[nodiscard]] RHI::BufferDesc BuildClusterLightHeaderBufferDesc(
        const ClusterGridDesc& desc) noexcept;

    [[nodiscard]] RHI::BufferDesc BuildClusterLightIndexBufferDesc(
        const ClusterGridDesc& desc,
        std::uint32_t maxLightsPerCell = kMaxClusterLightsPerCell) noexcept;

    [[nodiscard]] RHI::BufferDesc BuildClusterLightCounterBufferDesc() noexcept;

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

    [[nodiscard]] ClusterLightAssignmentDispatchPlan ComputeClusterLightAssignmentDispatchPlan(
        const ClusterGridDesc& desc,
        std::uint32_t lightCount,
        std::uint32_t maxLightsPerCell = kMaxClusterLightsPerCell,
        std::uint32_t groupSize = kClusterLightAssignmentGroupSize) noexcept;

    [[nodiscard]] bool DoesClusterAABBIntersectLight(
        const ClusterGridAABB& cell,
        const LightSnapshot& light) noexcept;

    [[nodiscard]] ClusterLightAssignmentResult AssignLightsToClusters(
        const ClusterGridDesc& desc,
        std::span<const ClusterGridAABB> cells,
        std::span<const LightSnapshot> lights,
        std::uint32_t maxLightsPerCell = kMaxClusterLightsPerCell);

    // Records the backend-neutral compute command shape for rebuilding the
    // cluster AABB buffer. BUG-015: `aabbBufferAddress` is the buffer device
    // address published to the shader through push constants; the handle is
    // retained for the shader-read publication barrier.
    bool RecordClusterGridBuild(RHI::ICommandContext& cmd,
                                RHI::PipelineHandle pipeline,
                                RHI::BufferHandle aabbBuffer,
                                std::uint64_t aabbBufferAddress,
                                const ClusterGridBuildDispatchPlan& plan,
                                const ClusterGridProjection& projection);

    // Records the backend-neutral assignment dispatch. BUG-015: each
    // `*Address` is the buffer device address published to the shader through
    // push constants; the handles are retained for the counter clear and the
    // shader-read publication barriers. This helper clears the shader-visible
    // atomic counter and pins the dispatch plus publication barriers.
    bool RecordClusterLightAssignment(RHI::ICommandContext& cmd,
                                      RHI::PipelineHandle pipeline,
                                      RHI::BufferHandle aabbBuffer,
                                      std::uint64_t aabbBufferAddress,
                                      RHI::BufferHandle lightsBuffer,
                                      std::uint64_t lightsBufferAddress,
                                      RHI::BufferHandle headerBuffer,
                                      std::uint64_t headerBufferAddress,
                                      RHI::BufferHandle indexBuffer,
                                      std::uint64_t indexBufferAddress,
                                      RHI::BufferHandle counterBuffer,
                                      std::uint64_t counterBufferAddress,
                                      const ClusterLightAssignmentDispatchPlan& plan);
}
