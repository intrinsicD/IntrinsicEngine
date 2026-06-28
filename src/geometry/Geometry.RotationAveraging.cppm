module;

#include <cstdint>
#include <span>
#include <glm/glm.hpp>

export module Geometry.RotationAveraging;

// Geometry.RotationAveraging — means and robust medians on SO(3).
//
// Builds on Geometry.Rotation (Exp/Log/ProjectOnSO3). All routines accept a
// shared options record with an optional per-rotation weight span. Fail-closed
// (GEOM-005/007): invalid inputs return identity plus an explicit status and no
// NaNs; iterative routines report iteration count, convergence, and residual.
export namespace Geometry::Rotation
{
    enum class RotationAverageStatus : std::uint8_t
    {
        Success = 0,
        SingleSample,
        EmptyInput,
        WeightSizeMismatch,
        InvalidWeight,
        NonFiniteInput,
        DegenerateInput,
        InvalidOptions,
        NoConvergence,
    };

    struct RotationAverageOptions
    {
        std::span<const float> Weights{};
        int MaxIterations{100};
        float Tolerance{1e-6f};
        // Disabled when <= 0. When enabled, samples farther than this angular
        // distance from the current estimate are deterministically ignored.
        float OutlierRejectionRadians{0.0f};
    };

    struct RotationAverageResult
    {
        glm::mat3 Rotation{1.0f};
        bool Valid{false};
        int Iterations{0};
        bool Converged{false};
        float ResidualRadians{0.0f};
        RotationAverageStatus Status{RotationAverageStatus::EmptyInput};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == RotationAverageStatus::Success ||
                   Status == RotationAverageStatus::SingleSample;
        }
    };

    // Chordal (L2) mean: Markley quaternion-moment mean, with a polar SO(3)
    // projection fallback if the dense symmetric eigensolver cannot produce a
    // finite unique principal quaternion.
    [[nodiscard]] RotationAverageResult ChordalMean(std::span<const glm::mat3> rotations,
                                                    RotationAverageOptions options = {});

    // Quaternion mean: hemisphere-aligned linear quaternion averaging.
    [[nodiscard]] RotationAverageResult QuaternionMean(std::span<const glm::mat3> rotations,
                                                       RotationAverageOptions options = {});

    // Geodesic / Karcher (Fréchet L2) mean: Riemannian mean via tangent-space
    // (Log/Exp) iteration, seeded from the chordal mean.
    [[nodiscard]] RotationAverageResult KarcherMean(std::span<const glm::mat3> rotations,
                                                    RotationAverageOptions options = {});

    // Geodesic L1 median: Weiszfeld inverse-distance reweighting on SO(3).
    [[nodiscard]] RotationAverageResult GeodesicMedian(std::span<const glm::mat3> rotations,
                                                       RotationAverageOptions options = {});

    // Quaternion L1 median: deterministic Weiszfeld iteration on aligned unit
    // quaternions followed by conversion back to SO(3).
    [[nodiscard]] RotationAverageResult QuaternionMedian(std::span<const glm::mat3> rotations,
                                                        RotationAverageOptions options = {});
}
