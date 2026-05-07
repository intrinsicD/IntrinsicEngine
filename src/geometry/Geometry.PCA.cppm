module;

#include <span>
#include <glm/glm.hpp>

export module Geometry.PCA;

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
