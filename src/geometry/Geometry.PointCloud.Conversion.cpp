module;

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.PointCloud.Conversion;

import Geometry.Properties;

namespace Geometry::PointCloud::Conversion
{
    namespace
    {
        void AppendValidationDiagnostics(std::vector<ConversionDiagnostic>& diagnostics,
                                         const MeshSoup::ValidationResult& validation)
        {
            for (const MeshSoup::ValidationDiagnostic& source : validation.Diagnostics)
            {
                diagnostics.push_back(ConversionDiagnostic{
                    .Kind = ConversionDiagnosticKind::ValidationDiagnostic,
                    .Severity = source.Severity,
                    .ValidationKind = source.Kind,
                    .AttributeDomainValue = source.AttributeDomainValue,
                    .AttributeName = source.AttributeName,
                    .Detail = std::string{MeshSoup::ToString(source.Kind)},
                });
            }
        }

        [[nodiscard]] bool HasErrors(std::span<const ConversionDiagnostic> diagnostics) noexcept
        {
            return std::ranges::any_of(diagnostics, [](const ConversionDiagnostic& diagnostic) {
                return diagnostic.Severity == MeshSoup::ValidationSeverity::Error;
            });
        }

        void AppendDeletedPointsWarning(std::vector<ConversionDiagnostic>& diagnostics, std::size_t omitted)
        {
            diagnostics.push_back(ConversionDiagnostic{
                .Kind = ConversionDiagnosticKind::DeletedPointsOmitted,
                .Severity = MeshSoup::ValidationSeverity::Warning,
                .AttributeDomainValue = MeshSoup::AttributeDomain::Vertex,
                .AttributeName = "v:point",
                .Detail = "cloud-to-soup conversion omitted " + std::to_string(omitted) +
                          " deleted point(s); arbitrary property remapping is skipped until garbage collection compacts storage",
            });
        }

        void AppendFacesDroppedWarning(std::vector<ConversionDiagnostic>& diagnostics, std::size_t dropped)
        {
            diagnostics.push_back(ConversionDiagnostic{
                .Kind = ConversionDiagnosticKind::FacesDropped,
                .Severity = MeshSoup::ValidationSeverity::Warning,
                .AttributeDomainValue = MeshSoup::AttributeDomain::Face,
                .AttributeName = "soup faces",
                .Detail = "soup-to-cloud conversion dropped " + std::to_string(dropped) +
                          " face(s); point clouds do not carry topology",
            });
        }

        void AppendAttributeRemapWarning(std::vector<ConversionDiagnostic>& diagnostics,
                                         MeshSoup::AttributeDomain domain,
                                         std::string_view detail)
        {
            diagnostics.push_back(ConversionDiagnostic{
                .Kind = ConversionDiagnosticKind::AttributeRemapSkipped,
                .Severity = MeshSoup::ValidationSeverity::Warning,
                .AttributeDomainValue = domain,
                .AttributeName = "attribute",
                .Detail = std::string{detail},
            });
        }

        [[nodiscard]] bool IsBuiltInCloudPointProperty(std::string_view name) noexcept
        {
            return name == "v:point" || name == "p:deleted" ||
                   name == "p:normal" || name == "p:color" || name == "p:radius";
        }

        [[nodiscard]] bool HasUserCloudPointProperties(const PointCloud::Cloud& cloud)
        {
            const auto names = cloud.PointProperties().Properties();
            return std::ranges::any_of(names, [](std::string_view name) {
                return !IsBuiltInCloudPointProperty(name);
            });
        }
    }

    bool ToIndexedMeshResult::Succeeded() const noexcept
    {
        return std::ranges::none_of(this->Diagnostics, [](const ConversionDiagnostic& diagnostic) {
            return diagnostic.Severity == MeshSoup::ValidationSeverity::Error;
        });
    }

    bool ToPointCloudResult::Succeeded() const noexcept
    {
        return std::ranges::none_of(this->Diagnostics, [](const ConversionDiagnostic& diagnostic) {
            return diagnostic.Severity == MeshSoup::ValidationSeverity::Error;
        });
    }

    std::string_view ToString(ConversionDiagnosticKind kind) noexcept
    {
        switch (kind)
        {
        case ConversionDiagnosticKind::ValidationDiagnostic:
            return "ValidationDiagnostic";
        case ConversionDiagnosticKind::DeletedPointsOmitted:
            return "DeletedPointsOmitted";
        case ConversionDiagnosticKind::FacesDropped:
            return "FacesDropped";
        case ConversionDiagnosticKind::AttributeRemapSkipped:
            return "AttributeRemapSkipped";
        }
        return "Unknown";
    }

    ToIndexedMeshResult ToIndexedMesh(const PointCloud::Cloud& cloud)
    {
        ToIndexedMeshResult result;

        const std::size_t storageSize = cloud.VerticesSize();
        std::size_t omittedDeleted = 0u;
        for (std::size_t i = 0u; i < storageSize; ++i)
        {
            const VertexHandle handle = PointCloud::Cloud::Handle(i);
            if (cloud.IsDeleted(handle))
            {
                ++omittedDeleted;
                continue;
            }
            static_cast<void>(result.Mesh.AddVertex(cloud.Position(handle)));
        }

        if (omittedDeleted > 0u)
        {
            AppendDeletedPointsWarning(result.Diagnostics, omittedDeleted);
        }

        if (cloud.HasNormals())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "cloud-to-soup conversion preserves v:point; p:normal stays on the source cloud");
        }
        if (cloud.HasColors())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "cloud-to-soup conversion preserves v:point; p:color stays on the source cloud");
        }
        if (cloud.HasRadii())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "cloud-to-soup conversion preserves v:point; p:radius stays on the source cloud");
        }
        if (HasUserCloudPointProperties(cloud))
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "cloud-to-soup conversion preserves v:point; generic point properties remain on the source cloud");
        }

        return result;
    }

    ToPointCloudResult ToPointCloud(MeshSoup::IndexedMeshView view,
                                    const MeshSoup::ValidationOptions& options)
    {
        ToPointCloudResult result;

        const MeshSoup::ValidationResult validation = MeshSoup::Validate(view, options);
        AppendValidationDiagnostics(result.Diagnostics, validation);
        if (HasErrors(result.Diagnostics))
        {
            return result;
        }

        for (const glm::vec3& position : view.Positions)
        {
            static_cast<void>(result.Cloud.AddPoint(position));
        }

        if (!view.Faces.empty())
        {
            AppendFacesDroppedWarning(result.Diagnostics, view.Faces.size());
        }

        if (view.VertexProperties != nullptr && view.VertexProperties->Properties().size() > 1u)
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "soup-to-cloud conversion preserves v:point; generic vertex properties remain on the source soup");
        }
        if (view.FaceProperties != nullptr && !view.FaceProperties->Properties().empty())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Face,
                                        "soup-to-cloud conversion drops topology; face properties remain on the source soup");
        }
        if (view.CornerProperties != nullptr && !view.CornerProperties->Properties().empty())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Corner,
                                        "point clouds have no corner-property domain");
        }

        return result;
    }

    ToPointCloudResult ToPointCloud(const MeshSoup::IndexedMesh& mesh,
                                    const MeshSoup::ValidationOptions& options)
    {
        return ToPointCloud(MeshSoup::BorrowView(mesh), options);
    }
}
