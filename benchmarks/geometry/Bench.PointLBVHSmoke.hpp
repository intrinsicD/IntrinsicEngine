// Deterministic LBVH query correctness/timing workload used by the smoke runner.
#pragma once
namespace Intrinsic::Bench::Geometry
{
    struct PointLBVHSmokeResult
    {
        double RuntimeMilliseconds{}, MaxDistanceError{};
        unsigned Mismatches{};
    };
    PointLBVHSmokeResult RunPointLBVHSmoke();
    PointLBVHSmokeResult RunLopLBVHSmoke();
    struct PointLBVHKnnSmokeResult
    {
        double BuildMilliseconds{}, ReferenceMilliseconds{}, WarmMilliseconds{};
        double NormalReferenceMilliseconds{}, NormalLbvhMilliseconds{}, MaxNormalError{};
        double MaxDistanceError{};
        unsigned Mismatches{};
    };
    PointLBVHKnnSmokeResult RunPointLBVHKnnSmoke();
} // namespace Intrinsic::Bench::Geometry
