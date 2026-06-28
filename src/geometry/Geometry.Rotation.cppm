module;

#include <cstdint>
#include <span>
#include <glm/glm.hpp>

export module Geometry.Rotation;

// Geometry.Rotation — SO(3) Lie-group primitives.
//
// Reusable rotation math the engine previously lacked (it only had Kabsch/
// Umeyama buried as private statics in Geometry.Registration). This module is
// the foundation for rotation averaging (GEOM-038) and is also intended to be
// reused by registration.
//
// Conventions: glm column-major matrices; the "axis-angle" / so(3) vector w has
// direction = rotation axis and magnitude = rotation angle (radians).
//
// Numeric/fail-closed contract (GEOM-005/GEOM-007): every routine returns a
// finite sentinel for invalid input. Rotation-valued routines return a valid
// SO(3) matrix (orthonormal, det +1); vector-valued Log returns zero for
// non-finite input.
export namespace Geometry::Rotation
{
    // Skew-symmetric "hat" operator: Hat(w) * v == cross(w, v). Vee is its inverse
    // (axial vector of the skew part of any matrix).
    [[nodiscard]] glm::mat3 Hat(const glm::vec3& w);
    [[nodiscard]] glm::vec3 Vee(const glm::mat3& m);

    // Exponential map so(3) -> SO(3) (Rodrigues), numerically stable near 0.
    [[nodiscard]] glm::mat3 Exp(const glm::vec3& axisAngle);

    // Logarithm map SO(3) -> so(3), domain-clamped; handles angle ~0 and ~pi.
    [[nodiscard]] glm::vec3 Log(const glm::mat3& rotation);

    // Geodesic angle (radians, in [0, pi]) between two rotations.
    [[nodiscard]] float AngularDistance(const glm::mat3& a, const glm::mat3& b);

    // Chordal (Frobenius) distance ||a - b||_F between two rotations.
    [[nodiscard]] float ChordalDistance(const glm::mat3& a, const glm::mat3& b);

    // Deterministic uniform-random rotation (Shoemake) for a given seed.
    [[nodiscard]] glm::mat3 RandomRotation(std::uint64_t seed);

    // Nearest rotation (det +1) to an arbitrary 3x3 matrix. The projection is
    // delegated to Geometry.Linalg's polar decomposition and then determinant
    // corrected to stay in SO(3).
    [[nodiscard]] glm::mat3 ProjectOnSO3(const glm::mat3& m);

    // Optimal rotation aligning `from` onto `to` about the origin (Kabsch/
    // Umeyama, det-corrected). Sizes must match with at least three finite
    // correspondences, else identity.
    [[nodiscard]] glm::mat3 OptimalRotation(std::span<const glm::vec3> from,
                                            std::span<const glm::vec3> to);
    [[nodiscard]] glm::mat3 OptimalRotation(std::span<const glm::vec3> from,
                                            std::span<const glm::vec3> to,
                                            std::span<const float> weights);
    [[nodiscard]] glm::dmat3 OptimalRotation(std::span<const glm::dvec3> from,
                                             std::span<const glm::dvec3> to);
    [[nodiscard]] glm::dmat3 OptimalRotation(std::span<const glm::dvec3> from,
                                             std::span<const glm::dvec3> to,
                                             std::span<const double> weights);
}
