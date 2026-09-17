// Radial-kernel choices and copied density diagnostics for config and result consumers.
module;

#include <cstddef>
#include <cstdint>
#include <string_view>

export module Geometry.PointCloud.Kernels.Types;

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

    struct DensityWeightDiagnostics
    {
        std::size_t PointCount{0u};
        std::size_t QueryCount{0u};
        std::size_t NeighborContributionCount{0u};
        std::size_t EmptyNeighborhoodCount{0u};
        bool UsedSuppliedIndex{false};
        bool UsedSuppliedNeighborhoods{false};
    };

    [[nodiscard]] std::string_view DebugName(
        KernelType kernel) noexcept;
    [[nodiscard]] std::string_view DebugName(
        DensityWeightMode mode) noexcept;
}
