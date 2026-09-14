// Shared face-ring validation and mesh snapshots used by runtime geometry operations.
// Include after editor/common, geometry-source and HalfedgeMesh imports.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
        enum class MeshFaceRingStatus : std::uint8_t
        {
            Triangulate,
            Skip,
            Invalid,
        };

        [[nodiscard]] MeshFaceRingStatus BuildMeshFaceRing(
            const std::vector<std::uint32_t>& faceHalfedges,
            const std::vector<std::uint32_t>& halfedgeFaces,
            const std::vector<std::uint32_t>& nextHalfedges,
            const std::vector<std::uint32_t>& toVertices,
            const std::size_t faceIndex,
            const std::uint32_t vertexCount,
            std::vector<std::uint32_t>& outRing);
        struct MeshForVertexNormalsResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<std::uint32_t> SourceFaceForMeshFace{};
            EditorCommandStatus Status{
                EditorCommandStatus::NoChange};
            Core::ErrorCode Error{Core::ErrorCode::Success};
            std::string Diagnostic{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == EditorCommandStatus::Applied;
            }
        };
    [[nodiscard]] MeshForVertexNormalsResult BuildHalfedgeMeshForVertexNormalRecompute(
        const ECS::Components::GeometrySources::ConstSourceView&, std::string_view positionProperty);
}
}
