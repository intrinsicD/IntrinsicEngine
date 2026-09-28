// METHOD-049: optimized Coherent Point Drift backends against the METHOD-015 reference on
// built-in fixtures (rigid, 1000 points; nonrigid, 300 points).
#pragma once
namespace Intrinsic::Bench::Geometry
{
    struct CoherentPointDriftAcceleratedSmokeResult
    {
        double ReferenceMilliseconds{};   // rigid reference run
        double DenseMilliseconds{};
        double TruncatedMilliseconds{};
        double AutoMilliseconds{};
        double MaxRigidParity{};          // max point difference of dense/truncated/auto to the reference
        double TruncatedErrorBound{};     // reported max relative denominator error
        double FullNonrigidMilliseconds{};
        double LowRankMilliseconds{};
        double LowRankParity{};           // max point difference of low rank 60 to the full solve
        double KernelApproximationError{};
        double RuntimeMilliseconds{};     // all runs
        bool Succeeded{};
    };
    CoherentPointDriftAcceleratedSmokeResult RunCoherentPointDriftAcceleratedSmoke();
}
