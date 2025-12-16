module;
#include <span>
#include <glm/fwd.hpp>

export module Runtime.Geometry.MeshUtils;

import Runtime.Graphics.Geometry;

export namespace Runtime::Geometry::MeshUtils
{
    void RecalculateNormals(Graphics::GeometryCpuData& mesh);

    void GenerateUVs(Graphics::GeometryCpuData& mesh);

    void CalculateNormals(std::span<const glm::vec3> positions, std::span<const uint32_t> indices,
                          std::span<glm::vec3> normals);
}
