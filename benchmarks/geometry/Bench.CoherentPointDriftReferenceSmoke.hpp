// METHOD-015: Coherent Point Drift reference on built-in deterministic fixtures, one per
// variant (rigid similarity, affine map, smooth nonrigid bend), with ground-truth errors.
#pragma once
#include <cstdint>
namespace Intrinsic::Bench::Geometry
{
    struct CoherentPointDriftVariantMetrics
    {
        double RmsError{};        // RMS distance of T(y) to the ground-truth positions
        double InitialRmsError{}; // same before registration
        double Sigma2{};
        double NegativeLogLikelihood{};
        std::uint32_t Iterations{};
        const char* Termination{"none"};
        bool Succeeded{};
    };
    struct CoherentPointDriftReferenceSmokeResult
    {
        CoherentPointDriftVariantMetrics Rigid{}, Affine{}, Nonrigid{};
        double RuntimeMilliseconds{}; // mean over measured iterations, all three variants
        double MaxRmsError{};
        bool Succeeded{};
    };
    CoherentPointDriftReferenceSmokeResult RunCoherentPointDriftReferenceSmoke();
}
