module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.Normals;

import Geometry.KDTree;
import Geometry.Octree;
import Geometry.PointCloud;
import Geometry.Properties;

export namespace Geometry::PointCloud::Normals
{
    inline constexpr std::string_view kDefaultPositionProperty = "v:point";
    inline constexpr std::string_view kDefaultOutputProperty = "v:normal";

    enum class NeighborhoodBackend : std::uint8_t
    {
        KDTree,
        SuppliedKDTree,
        SuppliedOctree,
    };

    enum class OrientationMode : std::uint8_t
    {
        None,
        MinimumSpanningTree,
    };

    enum class RecomputeStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        TooFewFinitePoints,
        InvalidPositionProperty,
        InvalidOutputProperty,
        PropertyTypeConflict,
        CountMismatch,
        SpatialIndexBuildFailed,
        SpatialIndexQueryFailed,
    };

    struct Params
    {
        std::string_view PositionProperty{kDefaultPositionProperty};
        std::string_view OutputProperty{kDefaultOutputProperty};
        std::size_t KNeighbors{15};
        std::size_t MinimumNeighbors{2};
        bool UseRadiusSearch{false};
        float Radius{0.0f};
        OrientationMode Orientation{OrientationMode::MinimumSpanningTree};
        glm::vec3 FallbackNormal{0.0f, 0.0f, 1.0f};
        double DegenerateNormalLengthEpsilon{1.0e-12};
        double CollinearEigenvalueRatioEpsilon{1.0e-5};
        bool SkipDeleted{true};
        KDTreeBuildParams KDTreeBuild{};
        Octree::SplitPolicy OctreePolicy{};
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    struct Diagnostics
    {
        std::size_t PointSlotCount{0};
        std::size_t FinitePointCount{0};
        std::size_t WrittenCount{0};
        std::size_t ValidNormalPointCount{0};
        std::size_t FallbackPointCount{0};
        std::size_t DegenerateNeighborhoodCount{0};
        std::size_t TooFewNeighborCount{0};
        std::size_t CollinearNeighborhoodCount{0};
        std::size_t DuplicatePositionCount{0};
        std::size_t NonFinitePointCount{0};
        std::size_t SkippedDeletedPointCount{0};
        std::size_t SpatialQueryFailureCount{0};
        std::size_t FlippedOrientationCount{0};
        std::size_t KNNVisitedNodeCount{0};
        std::size_t KNNDistanceEvaluationCount{0};
        bool FallbackNormalWasRepaired{false};
    };

    struct EstimateResult
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        NeighborhoodBackend Backend{NeighborhoodBackend::KDTree};
        std::vector<glm::vec3> Normals{};
        Diagnostics Diagnostics{};
    };

    struct PropertySetResult
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        NeighborhoodBackend Backend{NeighborhoodBackend::KDTree};
        Property<glm::vec3> Normals{};
        Diagnostics Diagnostics{};
    };

    struct Result
    {
        RecomputeStatus Status{RecomputeStatus::Success};
        NeighborhoodBackend Backend{NeighborhoodBackend::KDTree};
        VertexProperty<glm::vec3> Normals{};
        Diagnostics Diagnostics{};
    };

    [[nodiscard]] std::string_view DebugName(NeighborhoodBackend backend) noexcept;
    [[nodiscard]] std::string_view DebugName(OrientationMode mode) noexcept;
    [[nodiscard]] std::string_view DebugName(RecomputeStatus status) noexcept;

    [[nodiscard]] std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                                         const Params& params = {});

    [[nodiscard]] std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                                         const KDTree& index,
                                                         const Params& params = {});

    [[nodiscard]] std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                                         const Octree& index,
                                                         const Params& params = {});

    [[nodiscard]] PropertySetResult Recompute(Vertices& vertices,
                                              const Params& params = {});

    [[nodiscard]] PropertySetResult Recompute(Vertices& vertices,
                                              ConstProperty<glm::vec3> positions,
                                              const Params& params = {});

    [[nodiscard]] PropertySetResult Recompute(Vertices& vertices,
                                              ConstProperty<glm::vec3> positions,
                                              const KDTree& index,
                                              const Params& params = {});

    [[nodiscard]] PropertySetResult Recompute(Vertices& vertices,
                                              ConstProperty<glm::vec3> positions,
                                              const Octree& index,
                                              const Params& params = {});

    [[nodiscard]] Result Recompute(Cloud& cloud,
                                   const Params& params = {});

    [[nodiscard]] Result Recompute(Cloud& cloud,
                                   const KDTree& index,
                                   const Params& params = {});

    [[nodiscard]] Result Recompute(Cloud& cloud,
                                   const Octree& index,
                                   const Params& params = {});
} // namespace Geometry::PointCloud::Normals
