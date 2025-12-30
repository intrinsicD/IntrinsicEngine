module;
#include <span>
#include <glm/fwd.hpp>

export module Geometry:MeshUtils;

export namespace Geometry::MeshUtils
{
    int GenerateUVs(std::span<const glm::vec3> positions, std::span<glm::vec4> aux);

    void CalculateNormals(std::span<const glm::vec3> positions, std::span<const uint32_t> indices,
                          std::span<glm::vec3> normals);
}
