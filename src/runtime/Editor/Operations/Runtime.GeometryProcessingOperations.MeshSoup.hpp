// Triangle-soup capture shared by UV regeneration and mesh reconstruction.
// Include after EditorCommon, GeometrySources and Geometry.MeshSoup imports;
// only consumers that materialize a soup need its complete value type.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    struct MeshSoupFromGeometrySourcesResult
    {
        Geometry::MeshSoup::IndexedMesh Mesh{};
        std::vector<std::uint32_t> SourceFaceForSoupFace{};
        EditorCommandStatus Status{
            EditorCommandStatus::NoChange};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == EditorCommandStatus::Applied;
        }
    };

    [[nodiscard]] MeshSoupFromGeometrySourcesResult BuildMeshSoupFromGeometrySources(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::string_view positionProperty = ECS::Components::GeometrySources::PropertyNames::kPosition);
}
}
