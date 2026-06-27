module;

#include <span>
#include <glm/glm.hpp>

export module Geometry.RotationAveraging;

// Geometry.RotationAveraging — means and robust medians on SO(3).
//
// Builds on Geometry.Rotation (Exp/Log/ProjectOnSO3). All routines accept an
// optional per-rotation weight span (size must match the rotation span, else it
// is ignored). Fail-closed (GEOM-005/007): an empty input yields an invalid
// result (identity); iterative routines report iteration count + convergence.
export namespace Geometry::Rotation
{
    struct RotationAverageResult
    {
        glm::mat3 Rotation{1.0f};
        bool Valid{false};
        int Iterations{0};
        bool Converged{false};
    };

    // Chordal (L2) mean: the rotation minimizing sum w_i ||R - R_i||_F^2, i.e.
    // the SO(3) projection of the weighted arithmetic mean of the matrices.
    [[nodiscard]] glm::mat3 ChordalMean(std::span<const glm::mat3> rotations,
                                        std::span<const float> weights = {});

    // Quaternion mean: hemisphere-aligned linear quaternion averaging.
    [[nodiscard]] glm::mat3 QuaternionMean(std::span<const glm::mat3> rotations,
                                           std::span<const float> weights = {});

    // Geodesic / Karcher (Fréchet L2) mean: Riemannian mean via tangent-space
    // (Log/Exp) iteration, seeded from the chordal mean.
    [[nodiscard]] RotationAverageResult KarcherMean(std::span<const glm::mat3> rotations,
                                                    std::span<const float> weights = {},
                                                    int maxIterations = 100,
                                                    float tolerance = 1e-6f);

    // Geodesic L1 median: Weiszfeld inverse-distance reweighting on SO(3).
    [[nodiscard]] RotationAverageResult GeodesicMedian(std::span<const glm::mat3> rotations,
                                                       std::span<const float> weights = {},
                                                       int maxIterations = 100,
                                                       float tolerance = 1e-6f);
}
