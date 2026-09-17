// Shared face-ring validation, source snapshots and stored-topology signatures.
// Include after core/error, editor/common, geometry-source and HalfedgeMesh imports;
// the global module fragment supplies integers, optional, string/view, vector and GLM types.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
        struct MeshProcessingSourceResult
        {
            Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<glm::vec3> BeforePositions{};
            std::vector<bool> DeletedVertices{};
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

        [[nodiscard]] MeshProcessingSourceResult BuildHalfedgeMeshForProcessing(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            std::string_view operationName,
            std::string_view positionProperty = ECS::Components::GeometrySources::PropertyNames::kPosition);


        [[nodiscard]] std::vector<glm::vec3> ExtractMeshPositions(
            const Geometry::HalfedgeMesh::Mesh& mesh);

        // `std::nullopt` means the stored topology could not be read at all, so
        // no two readings may be treated as equal.
        [[nodiscard]] std::optional<std::uint64_t> MeshTopologyValueSignature(
            const ECS::Components::GeometrySources::ConstSourceView& view);

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
