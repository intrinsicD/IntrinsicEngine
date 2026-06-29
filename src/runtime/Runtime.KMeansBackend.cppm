module;

#include <optional>
#include <span>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.KMeansBackend;

import Extrinsic.RHI.Device;
import Geometry.KMeans;

export namespace Extrinsic::Runtime
{
    [[nodiscard]] std::optional<Geometry::KMeans::KMeansResult> ClusterKMeans(
        std::span<const glm::vec3> points,
        const Geometry::KMeans::KMeansParams& params,
        RHI::IDevice& device);

    [[nodiscard]] std::optional<Geometry::KMeans::KMeansResult> ClusterKMeans(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const Geometry::KMeans::KMeansParams& params,
        Geometry::KMeans::CpuScratch* cpuScratch,
        RHI::IDevice& device);
}
