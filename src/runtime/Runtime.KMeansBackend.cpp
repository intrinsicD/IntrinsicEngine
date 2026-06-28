module;

#include <optional>
#include <span>

#include <glm/glm.hpp>

module Extrinsic.Runtime.KMeansBackend;

import Extrinsic.RHI.Device;
import Geometry.KMeans;

namespace Extrinsic::Runtime
{
    namespace GK = Geometry::KMeans;

    namespace
    {
        [[nodiscard]] std::optional<GK::KMeansResult> MarkResolvedBackend(
            std::optional<GK::KMeansResult> result,
            const GK::Backend requestedBackend)
        {
            if (!result.has_value())
            {
                return std::nullopt;
            }

            result->RequestedBackend = requestedBackend;
            result->ActualBackend = GK::Backend::CPU;
            result->FellBackToCPU = requestedBackend == GK::Backend::GPU;
            return result;
        }

        [[nodiscard]] GK::KMeansParams CpuFallbackParams(
            const GK::KMeansParams& params)
        {
            GK::KMeansParams cpuParams = params;
            cpuParams.Compute = GK::Backend::CPU;
            return cpuParams;
        }
    }

    std::optional<GK::KMeansResult> ClusterKMeans(
        std::span<const glm::vec3> points,
        const GK::KMeansParams& params,
        RHI::IDevice& device)
    {
        const GK::Backend requestedBackend = params.Compute;
        if (requestedBackend == GK::Backend::GPU && device.IsOperational())
        {
            // GEOM-052 installs the RHI-visible seam only. A real GPU KMeans
            // kernel must be added by a later parity-gated backend task.
        }
        return MarkResolvedBackend(
            GK::Cluster(points, CpuFallbackParams(params)),
            requestedBackend);
    }

    std::optional<GK::KMeansResult> ClusterKMeans(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const GK::KMeansParams& params,
        GK::CpuScratch* cpuScratch,
        RHI::IDevice& device)
    {
        const GK::Backend requestedBackend = params.Compute;
        if (requestedBackend == GK::Backend::GPU && device.IsOperational())
        {
            // GEOM-052 installs the RHI-visible seam only. A real GPU KMeans
            // kernel must be added by a later parity-gated backend task.
        }
        return MarkResolvedBackend(
            GK::Cluster(points, initialCentroids, CpuFallbackParams(params), cpuScratch),
            requestedBackend);
    }
}
