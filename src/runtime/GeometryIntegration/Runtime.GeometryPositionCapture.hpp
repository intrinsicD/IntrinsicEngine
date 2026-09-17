// Finite position snapshots shared by processing operations and clustering.
// Include after Geometry.Properties and standard/GLM declarations.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    [[nodiscard]] bool IsFiniteGeometryPosition(const glm::vec3& position) noexcept;

    // Copies every row in order. Missing/wrong-typed, empty, mis-sized or
    // non-finite storage returns nullopt; deletion masks are not interpreted.
    [[nodiscard]] std::optional<std::vector<glm::vec3>> CollectFiniteGeometryPositions(
        const Geometry::PropertySet& properties, std::string_view positionProperty);
}
}
