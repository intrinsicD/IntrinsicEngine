module;

#include <string_view>

module Extrinsic.Runtime.PointCloudConsolidationConfig;

namespace Extrinsic::Runtime
{
    std::string_view StableToken(
        const PointCloudConsolidationBackend backend) noexcept
    {
        switch (backend)
        {
        case PointCloudConsolidationBackend::VulkanLBVH: return "vulkan_lbvh";
        case PointCloudConsolidationBackend::CpuLBVH: return "cpu_lbvh";
        case PointCloudConsolidationBackend::None: return "none";
        case PointCloudConsolidationBackend::CpuReference:
            return "cpu_reference";
        case PointCloudConsolidationBackend::VulkanCompute:
            return "gpu_vulkan_compute";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationStrategy strategy) noexcept
    {
        switch (strategy)
        {
        case PointCloudConsolidationStrategy::Lop: return "lop";
        case PointCloudConsolidationStrategy::Wlop: return "wlop";
        case PointCloudConsolidationStrategy::Clop: return "clop";
        case PointCloudConsolidationStrategy::Ear: return "ear";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationNormalSource source) noexcept
    {
        switch (source)
        {
        case PointCloudConsolidationNormalSource::AuthoredOrEstimate:
            return "authored_or_estimate";
        case PointCloudConsolidationNormalSource::RequireAuthored:
            return "require_authored";
        }
        return {};
    }

    std::string_view StableToken(
        const PointCloudConsolidationSupportRadiusMode mode) noexcept
    {
        switch (mode)
        {
        case PointCloudConsolidationSupportRadiusMode::Auto: return "auto";
        case PointCloudConsolidationSupportRadiusMode::Manual:
            return "manual";
        }
        return {};
    }
}
