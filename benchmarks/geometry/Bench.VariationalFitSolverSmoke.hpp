// Reweighted (IRLS) versus ADMM variational fit on a kNN graph: parity and iteration cost.
#pragma once
#include <cstddef>
namespace Intrinsic::Bench::Geometry
{
    struct VariationalFitSolverSmokeMetrics
    {
        double RuntimeMilliseconds{}, ReweightedRuntimeMilliseconds{};
        double MaxValueDelta{}, RelativeEnergyDelta{}, ExactTvStepError{};
        std::size_t AdmmIterations{}, ReweightedIterations{}, AdmmFactorizations{}, ReweightedFactorizations{};
        bool Succeeded{};
    };
    [[nodiscard]] VariationalFitSolverSmokeMetrics RunVariationalFitSolverSmoke();
}
