// Analytic path-graph workloads for constrained harmonic, biharmonic and label fields.
#pragma once
#include <cstddef>
namespace Intrinsic::Bench::Geometry
{
    struct HarmonicFieldSmokeMetrics
    {
        double RuntimeMilliseconds{}, MaxError{};
        std::size_t MislabeledRows{};
        bool Succeeded{};
    };
    [[nodiscard]] HarmonicFieldSmokeMetrics RunHarmonicFieldSmoke();
}
