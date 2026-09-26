// Reweighted (IRLS) versus ADMM variational fit on a kNN graph (parity and iteration cost), plus
// second-order (non-local TGV) quality against first-order TV.
#pragma once
#include <cstddef>
namespace Intrinsic::Bench::Geometry
{
    struct VariationalFitSolverSmokeMetrics
    {
        double RuntimeMilliseconds{}, ReweightedRuntimeMilliseconds{};
        double MaxValueDelta{}, RelativeEnergyDelta{}, ExactTvStepError{};
        // Second order (non-local TGV): affine reproduction and RMS error on a noisy ramp with a jump.
        double TgvAffineError{}, TgvRampRmsError{}, TvRampRmsError{};
        std::size_t AdmmIterations{}, ReweightedIterations{}, AdmmFactorizations{}, ReweightedFactorizations{};
        bool Succeeded{};
    };
    [[nodiscard]] VariationalFitSolverSmokeMetrics RunVariationalFitSolverSmoke();
}
