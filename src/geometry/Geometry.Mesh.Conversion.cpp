module;

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.Mesh.Conversion;

import Geometry.Properties;

namespace Geometry::Mesh::Conversion
{
    namespace
    {
        constexpr MeshSoup::Index kInvalidIndex = static_cast<MeshSoup::Index>(-1);

        void AppendValidationDiagnostics(std::vector<ConversionDiagnostic>& diagnostics,
                                         const MeshSoup::ValidationResult& validation)
        {
            for (const MeshSoup::ValidationDiagnostic& source : validation.Diagnostics)
            {
                diagnostics.push_back(ConversionDiagnostic{
                    .Kind = ConversionDiagnosticKind::ValidationDiagnostic,
                    .Severity = source.Severity,
                    .ValidationKind = source.Kind,
                    .FaceIndex = source.FaceIndex,
                    .VertexIndex = source.VertexIndex,
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

        void AppendDeletedElementsWarning(std::vector<ConversionDiagnostic>& diagnostics, std::string_view detail)
        {
            diagnostics.push_back(ConversionDiagnostic{
                .Kind = ConversionDiagnosticKind::DeletedElementsOmitted,
                .Severity = MeshSoup::ValidationSeverity::Warning,
                .AttributeDomainValue = MeshSoup::AttributeDomain::Vertex,
                .AttributeName = "vertex index",
                .Detail = std::string{detail},
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
    }

    bool ToHalfedgeMeshResult::Succeeded() const noexcept
    {
        return std::ranges::none_of(this->Diagnostics, [](const ConversionDiagnostic& diagnostic) {
            return diagnostic.Severity == MeshSoup::ValidationSeverity::Error;
        });
    }

    bool ToIndexedMeshResult::Succeeded() const noexcept
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
        case ConversionDiagnosticKind::AddFaceFailed:
            return "AddFaceFailed";
        case ConversionDiagnosticKind::DeletedElementsOmitted:
            return "DeletedElementsOmitted";
        case ConversionDiagnosticKind::AttributeRemapSkipped:
            return "AttributeRemapSkipped";
        }
        return "Unknown";
    }

    ToHalfedgeMeshResult ToHalfedgeMesh(MeshSoup::IndexedMeshView view,
                                        const MeshSoup::ValidationOptions& options)
    {
        ToHalfedgeMeshResult result;
        const MeshSoup::ValidationResult validation = MeshSoup::Validate(view, options);
        AppendValidationDiagnostics(result.Diagnostics, validation);
        if (HasErrors(result.Diagnostics))
        {
            return result;
        }

        std::vector<VertexHandle> vertices;
        vertices.reserve(view.Positions.size());
        for (const glm::vec3& position : view.Positions)
        {
            vertices.push_back(result.Mesh.AddVertex(position));
        }

        std::vector<VertexHandle> faceVertices;
        for (std::size_t faceIndex = 0u; faceIndex < view.Faces.size(); ++faceIndex)
        {
            const MeshSoup::PolygonFace& face = view.Faces[faceIndex];
            faceVertices.clear();
            faceVertices.reserve(face.Indices.size());
            for (const MeshSoup::Index index : face.Indices)
            {
                faceVertices.push_back(vertices[index]);
            }

            if (!result.Mesh.AddFace(faceVertices))
            {
                result.Diagnostics.push_back(ConversionDiagnostic{
                    .Kind = ConversionDiagnosticKind::AddFaceFailed,
                    .Severity = MeshSoup::ValidationSeverity::Error,
                    .FaceIndex = faceIndex,
                    .AttributeDomainValue = MeshSoup::AttributeDomain::Face,
                    .AttributeName = "face vertex index",
                    .Detail = "HalfedgeMesh::Mesh rejected the face topology",
                });
                return result;
            }
        }

        if (view.VertexProperties != nullptr && view.VertexProperties->Properties().size() > 1u)
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "soup-to-halfedge conversion preserves v:point; generic vertex properties remain on the source soup");
        }
        if (view.FaceProperties != nullptr && !view.FaceProperties->Properties().empty())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Face,
                                        "soup-to-halfedge conversion builds topology; generic face properties remain on the source soup");
        }
        if (view.CornerProperties != nullptr && !view.CornerProperties->Properties().empty())
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Corner,
                                        "halfedge mesh has no mesh-soup corner-property domain");
        }
        return result;
    }

    ToHalfedgeMeshResult ToHalfedgeMesh(const MeshSoup::IndexedMesh& mesh,
                                        const MeshSoup::ValidationOptions& options)
    {
        return ToHalfedgeMesh(MeshSoup::BorrowView(mesh), options);
    }

    ToIndexedMeshResult ToIndexedMesh(const HalfedgeMesh::Mesh& mesh)
    {
        ToIndexedMeshResult result;
        std::vector<MeshSoup::Index> remap(mesh.VerticesSize(), kInvalidIndex);
        for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vertex))
            {
                continue;
            }
            remap[i] = result.Mesh.AddVertex(mesh.Position(vertex));
        }

        std::vector<MeshSoup::Index> faceIndices;
        for (std::size_t i = 0u; i < mesh.FacesSize(); ++i)
        {
            const FaceHandle face{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(face))
            {
                continue;
            }

            faceIndices.clear();
            for (const VertexHandle vertex : mesh.VerticesAroundFace(face))
            {
                if (!vertex.IsValid() || vertex.Index >= remap.size() || remap[vertex.Index] == kInvalidIndex)
                {
                    result.Diagnostics.push_back(ConversionDiagnostic{
                        .Kind = ConversionDiagnosticKind::AddFaceFailed,
                        .Severity = MeshSoup::ValidationSeverity::Error,
                        .FaceIndex = i,
                        .VertexIndex = vertex.Index,
                        .AttributeDomainValue = MeshSoup::AttributeDomain::Face,
                        .AttributeName = "face vertex index",
                        .Detail = "halfedge face references an invalid or deleted vertex",
                    });
                    return result;
                }
                faceIndices.push_back(remap[vertex.Index]);
            }

            if (faceIndices.size() < 3u)
            {
                result.Diagnostics.push_back(ConversionDiagnostic{
                    .Kind = ConversionDiagnosticKind::AddFaceFailed,
                    .Severity = MeshSoup::ValidationSeverity::Error,
                    .FaceIndex = i,
                    .AttributeDomainValue = MeshSoup::AttributeDomain::Face,
                    .AttributeName = "face vertex count",
                    .Detail = "halfedge face has fewer than three vertices",
                });
                return result;
            }
            static_cast<void>(result.Mesh.AddFace(faceIndices));
        }

        if (mesh.HasGarbage())
        {
            AppendDeletedElementsWarning(result.Diagnostics,
                                         "deleted halfedge elements are omitted; arbitrary property remapping is skipped until garbage collection compacts storage");
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "vertex properties were not copied because deleted vertices require remapping");
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Face,
                                        "face properties were not copied because deleted faces require remapping");
        }

        const auto vertexProperties = mesh.VertexProperties().Properties();
        if (std::ranges::any_of(vertexProperties, [](std::string_view name) {
                return name != "v:point" && name != "v:connectivity" && name != "v:deleted";
            }))
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Vertex,
                                        "halfedge-to-soup conversion preserves positions; generic halfedge vertex properties remain on the source mesh");
        }

        const auto faceProperties = mesh.FaceProperties().Properties();
        if (std::ranges::any_of(faceProperties, [](std::string_view name) {
                return name != "f:connectivity" && name != "f:deleted";
            }))
        {
            AppendAttributeRemapWarning(result.Diagnostics,
                                        MeshSoup::AttributeDomain::Face,
                                        "halfedge-to-soup conversion preserves face indices; generic halfedge face properties remain on the source mesh");
        }

        return result;
    }
}
