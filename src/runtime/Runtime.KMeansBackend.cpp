module;

#include <cstdint>
#include <optional>
#include <span>

#include <glm/glm.hpp>

module Extrinsic.Runtime.KMeansBackend;

import Extrinsic.RHI.Device;
import Extrinsic.Runtime.KMeansGpuBackend;
import Geometry.KMeans;

namespace Extrinsic::Runtime
{
    namespace GK = Geometry::KMeans;

    namespace
    {
        // Route a Backend::GPU request through the GEOM-056 resolve seam. The
        // recorded Lloyd loop lands in later slices, so this reports whether an
        // operational GPU path exists and, for now, always recommends the CPU
        // reference fallback. Returns true when GPU execution is available.
        [[nodiscard]] bool TryResolveKMeansGpu(
            std::span<const glm::vec3> points,
            const GK::KMeansParams& params,
            RHI::IDevice& device)
        {
            const KMeansGpuResolveDesc resolveDesc{
                .Device = &device,
                .Plan = KMeansGpuPlanDesc{
                    .PointCount = static_cast<std::uint32_t>(points.size()),
                    .ClusterCount = params.ClusterCount,
                    .MaxIterations = params.MaxIterations,
                    .GroupSize = kKMeansGpuGroupSize,
                    .ConvergenceTolerance = params.ConvergenceTolerance,
                },
            };
            const KMeansGpuResolveResult resolved = ResolveKMeansGpuRequest(resolveDesc);
            return resolved.GpuExecutionAvailable;
        }
    }

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
        if (requestedBackend == GK::Backend::GPU &&
            TryResolveKMeansGpu(points, params, device))
        {
            // GEOM-056 later slices execute the recorded Lloyd loop here when
            // the resolve seam reports an operational GPU path. Until then the
            // resolve returns false and we fall through to the CPU reference.
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
        if (requestedBackend == GK::Backend::GPU &&
            TryResolveKMeansGpu(points, params, device))
        {
            // GEOM-056 later slices execute the recorded Lloyd loop here when
            // the resolve seam reports an operational GPU path. Until then the
            // resolve returns false and we fall through to the CPU reference.
        }
        return MarkResolvedBackend(
            GK::Cluster(points, initialCentroids, CpuFallbackParams(params), cpuScratch),
            requestedBackend);
    }
}
