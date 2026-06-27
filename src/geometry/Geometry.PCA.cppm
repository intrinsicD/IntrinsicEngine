module;

#include <span>
#include <glm/glm.hpp>

export module Geometry.PCA;

export namespace Geometry::PCA
{
    // Result of the closed-form symmetric 3x3 eigendecomposition.
    // Eigenvalues are sorted descending (Eigenvalues.x >= .y >= .z) and
    // non-negative-clamped; Eigenvectors[k] is the unit eigenvector for
    // Eigenvalues[k], gram-schmidt re-orthonormalized into a right-handed frame.
    struct Eigen3
    {
        glm::dvec3 Eigenvalues{0.0};
        glm::dvec3 Eigenvectors[3]{
            glm::dvec3{1.0, 0.0, 0.0},
            glm::dvec3{0.0, 1.0, 0.0},
            glm::dvec3{0.0, 0.0, 1.0},
        };
    };

    // Closed-form symmetric 3x3 eigensolver for the matrix whose upper triangle
    // is (a00 a01 a02 / · a11 a12 / · · a22). Deterministic, allocation-free, and
    // fail-closed: degenerate/isotropic inputs return the identity frame with the
    // (repeated) mean eigenvalue. Shared by PCA and the curvature-tensor decomposition.
    [[nodiscard]] Eigen3 SymmetricEigen3(
        double a00, double a01, double a02,
        double a11, double a12, double a22);
}

export namespace Geometry
{
    struct PCAResult
    {
        bool Valid{false}; // True if at least one finite sample contributed.
        bool Flat{false}; // True if the point set is numerically rank deficient.
        glm::vec3 Mean{0.0f};
        glm::mat3 Eigenvectors{1.0f}; // Columns are eigenvectors
        glm::vec3 Eigenvalues{1.0f}; // Corresponding eigenvalues
    };

    [[nodiscard]] PCAResult ToPCA(std::span<const glm::vec3> points);
}
