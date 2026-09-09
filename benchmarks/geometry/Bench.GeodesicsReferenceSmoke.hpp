// Bounded analytic grid workload for virtual-source propagation.
#pragma once
#include <cstddef>
namespace Intrinsic::Bench::Geometry
{
    struct GeodesicsReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0};
        double QualityErrorL2{0};
        double MaxAbsoluteError{0};
        std::size_t HalfedgeExpansions{0};
        bool Succeeded{false};
    };
    [[nodiscard]] GeodesicsReferenceSmokeMetrics RunGeodesicsReferenceSmoke();
}
