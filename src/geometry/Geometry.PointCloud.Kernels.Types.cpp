module;

#include <string_view>

module Geometry.PointCloud.Kernels.Types;

namespace Geometry::PointCloud::Kernels
{
    std::string_view DebugName(const KernelType kernel) noexcept
    {
        switch (kernel)
        {
        case KernelType::Gaussian: return "gaussian";
        case KernelType::ThetaLop: return "theta_lop";
        case KernelType::WendlandC2: return "wendland_c2";
        }
        return "invalid";
    }

    std::string_view DebugName(const DensityWeightMode mode) noexcept
    {
        switch (mode)
        {
        case DensityWeightMode::Direct: return "direct";
        case DensityWeightMode::Reciprocal: return "reciprocal";
        }
        return "invalid";
    }
}
