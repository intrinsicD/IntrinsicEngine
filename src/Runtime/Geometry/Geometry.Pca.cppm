module;

#include <span>
#include <glm/glm.hpp>

export module Geometry:Pca;

export namespace Geometry
{
    struct PcaResult
    {
        bool Valid{false};// True if there are more than 2 points;
        bool Flat{false}; // True if there are only exactly 3 points;
        glm::vec3 Mean{0.0f};
        glm::mat3 Eigenvectors{1.0f}; // Columns are eigenvectors
        glm::vec3 Eigenvalues{1.0f}; // Corresponding eigenvalues
    };

    [[nodiscard]] PcaResult ToPca(std::span<const glm::vec3> points); //TODO: implement this
}
