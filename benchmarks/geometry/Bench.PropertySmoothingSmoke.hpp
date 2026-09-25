// Analytic cycle-signal workload for the shared property filters.
#pragma once
namespace Intrinsic::Bench::Geometry
{
    struct PropertySmoothingSmokeMetrics
    {
        double RuntimeMilliseconds{}, MaxError{};
        bool Succeeded{};
    };
    [[nodiscard]] PropertySmoothingSmokeMetrics RunPropertySmoothingSmoke();
}
