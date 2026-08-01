module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.Kernels;

import Geometry.KDTree;

export namespace Geometry::PointCloud::Kernels
{
    enum class KernelType : std::uint8_t
    {
        Gaussian = 0,
        ThetaLop,
        WendlandC2,
    };

    enum class DensityWeightMode : std::uint8_t
    {
        Direct = 0,
        Reciprocal,
    };

    enum class DensityWeightStatus : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        InvalidSupportRadius,
        InvalidKernel,
        InvalidMode,
        NonFiniteInput,
        ResourceLimit,
        SpatialIndexBuildFailed,
        SpatialIndexMismatch,
        SpatialQueryFailed,
        EmptyNeighborhood,
        NumericalFailure,
    };

    struct DensityWeightDiagnostics
    {
        std::size_t PointCount{0u};
        std::size_t QueryCount{0u};
        std::size_t NeighborContributionCount{0u};
        std::size_t EmptyNeighborhoodCount{0u};
        bool UsedSuppliedIndex{false};
    };

    struct DensityWeightResult
    {
        DensityWeightStatus Status{
            DensityWeightStatus::EmptyInput};
        std::vector<float> Weights{};
        DensityWeightDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == DensityWeightStatus::Success;
        }
    };

    [[nodiscard]] std::string_view DebugName(
        KernelType kernel) noexcept;
    [[nodiscard]] std::string_view DebugName(
        DensityWeightMode mode) noexcept;
    [[nodiscard]] std::string_view DebugName(
        DensityWeightStatus status) noexcept;

    // Compactly supported radial weight over 0 <= r < h. The Gaussian uses
    // sigma=h/4, exp(-r^2/(2 sigma^2)); ThetaLop uses the LOP/WLOP convention
    // exp(-r^2/(h/4)^2); and WendlandC2 uses
    // (1-r/h)^4 (1+4r/h). Every kernel is exactly zero at and beyond h.
    // distanceSquared and h are expressed in the same coordinate units.
    [[nodiscard]] std::optional<double> Weight(
        double distanceSquared,
        double supportRadius,
        KernelType kernel) noexcept;

    // Stable WLOP linear repulsion potential eta(r,h)=-r/h and derivative
    // d eta/dr=-1/h. Both have finite r->0 limits. Projection owners combine
    // the derivative magnitude with their directional term; no 1/r is hidden
    // in this seam.
    [[nodiscard]] std::optional<double> Repulsion(
        double distance,
        double supportRadius) noexcept;
    [[nodiscard]] std::optional<double> RepulsionDerivative(
        double distance,
        double supportRadius) noexcept;

    // Compute 1 + sum_{j!=i} kernel(||p_i-p_j||^2,h), or its reciprocal,
    // in point-index order. Accumulation is double precision and output is
    // float. An isolated point makes the whole operation fail closed with
    // EmptyNeighborhood and no published weights.
    [[nodiscard]] DensityWeightResult ComputeDensityWeights(
        std::span<const glm::vec3> points,
        double supportRadius,
        KernelType kernel = KernelType::ThetaLop,
        DensityWeightMode mode = DensityWeightMode::Direct);

    // The supplied index must have been built from the same ordered point
    // coordinates. Mismatch or an unbuilt index fails closed.
    [[nodiscard]] DensityWeightResult ComputeDensityWeights(
        std::span<const glm::vec3> points,
        const Geometry::KDTree& index,
        double supportRadius,
        KernelType kernel = KernelType::ThetaLop,
        DensityWeightMode mode = DensityWeightMode::Direct);
}
