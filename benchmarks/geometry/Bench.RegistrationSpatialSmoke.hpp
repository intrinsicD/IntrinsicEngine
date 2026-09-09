// CPU ICP comparison with cold target builds and warm target-index reuse.
#pragma once
namespace Intrinsic::Bench::Geometry
{
    struct RegistrationSpatialSmokeResult
    {
        double ReferenceMilliseconds{}, ColdMilliseconds{}, WarmMilliseconds{}, MaxTransformError{};
        unsigned Failures{};
    };
    RegistrationSpatialSmokeResult RunRegistrationSpatialSmoke();
}
