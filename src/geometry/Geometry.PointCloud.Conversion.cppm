module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Geometry.PointCloud.Conversion;

export import Geometry.MeshSoup;
export import Geometry.PointCloud;

export namespace Geometry::PointCloud::Conversion
{
    enum class ConversionDiagnosticKind : std::uint8_t
    {
        ValidationDiagnostic,
        DeletedPointsOmitted,
        FacesDropped,
        AttributeRemapSkipped,
    };

    struct ConversionDiagnostic
    {
        ConversionDiagnosticKind Kind{ConversionDiagnosticKind::ValidationDiagnostic};
        MeshSoup::ValidationSeverity Severity{MeshSoup::ValidationSeverity::Error};
        MeshSoup::ValidationDiagnosticKind ValidationKind{MeshSoup::ValidationDiagnosticKind::InvalidIndex};
        MeshSoup::AttributeDomain AttributeDomainValue{MeshSoup::AttributeDomain::Vertex};
        std::string AttributeName;
        std::string Detail;
    };

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    struct ToIndexedMeshResult
    {
        ToIndexedMeshResult() = default;

        MeshSoup::IndexedMesh Mesh{};
        std::vector<ConversionDiagnostic> Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    struct ToPointCloudResult
    {
        ToPointCloudResult() = default;

        Cloud Cloud{};
        std::vector<ConversionDiagnostic> Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    [[nodiscard]] std::string_view ToString(ConversionDiagnosticKind kind) noexcept;

    [[nodiscard]] ToIndexedMeshResult ToIndexedMesh(const PointCloud::Cloud& cloud);

    [[nodiscard]] ToPointCloudResult ToPointCloud(MeshSoup::IndexedMeshView view,
                                                  const MeshSoup::ValidationOptions& options = {});

    [[nodiscard]] ToPointCloudResult ToPointCloud(const MeshSoup::IndexedMesh& mesh,
                                                  const MeshSoup::ValidationOptions& options = {});
}
