module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Geometry.Mesh.Conversion;

export import Geometry.HalfedgeMesh;
export import Geometry.MeshSoup;

export namespace Geometry::Mesh::Conversion
{
    enum class ConversionDiagnosticKind : std::uint8_t
    {
        ValidationDiagnostic,
        AddFaceFailed,
        DeletedElementsOmitted,
        AttributeRemapSkipped,
    };

    struct ConversionDiagnostic
    {
        ConversionDiagnosticKind Kind{ConversionDiagnosticKind::ValidationDiagnostic};
        MeshSoup::ValidationSeverity Severity{MeshSoup::ValidationSeverity::Error};
        MeshSoup::ValidationDiagnosticKind ValidationKind{MeshSoup::ValidationDiagnosticKind::InvalidIndex};
        std::size_t FaceIndex{static_cast<std::size_t>(-1)};
        MeshSoup::Index VertexIndex{static_cast<MeshSoup::Index>(-1)};
        MeshSoup::AttributeDomain AttributeDomainValue{MeshSoup::AttributeDomain::Vertex};
        std::string AttributeName;
        std::string Detail;
    };

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    struct ToHalfedgeMeshResult
    {
        ToHalfedgeMeshResult() = default;

        HalfedgeMesh::Mesh Mesh{};
        std::vector<ConversionDiagnostic> Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    struct ToIndexedMeshResult
    {
        ToIndexedMeshResult() = default;

        MeshSoup::IndexedMesh Mesh{};
        std::vector<ConversionDiagnostic> Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    [[nodiscard]] std::string_view ToString(ConversionDiagnosticKind kind) noexcept;

    [[nodiscard]] ToHalfedgeMeshResult ToHalfedgeMesh(MeshSoup::IndexedMeshView view,
                                                      const MeshSoup::ValidationOptions& options = {});

    [[nodiscard]] ToHalfedgeMeshResult ToHalfedgeMesh(const MeshSoup::IndexedMesh& mesh,
                                                      const MeshSoup::ValidationOptions& options = {});

    [[nodiscard]] ToIndexedMeshResult ToIndexedMesh(const HalfedgeMesh::Mesh& mesh);
}
