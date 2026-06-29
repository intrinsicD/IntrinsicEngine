// GEOM-035 — surface sampling smoke benchmark declaration.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kSurfaceSamplingSmokeBenchmarkId = "geometry.surface_sampling.smoke";
    inline constexpr const char* kSurfaceSamplingSmokeMethod      = "geometry.pointcloud.surface_sampling";
    inline constexpr const char* kSurfaceSamplingSmokeDataset     = "builtin.two_triangle_area_ratio";

    struct SurfaceSamplingSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double SmallTriangleFraction{0.0};
        double ExpectedSmallTriangleFraction{0.0};
        std::size_t WrittenSampleCount{0};
        std::size_t AcceptedTriangleCount{0};
        bool Succeeded{false};
    };

    [[nodiscard]] SurfaceSamplingSmokeMetrics RunSurfaceSamplingSmoke();
}
