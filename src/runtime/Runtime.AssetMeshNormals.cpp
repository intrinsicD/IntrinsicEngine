module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetMeshNormals;

import Extrinsic.Core.Error;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr const char* kPositionProperty = "v:point";
        constexpr const char* kNormalProperty = "v:normal";
        constexpr const char* kFaceVerticesProperty = "f:vertices";

        [[nodiscard]] std::vector<glm::vec3> ComputeAreaWeightedVertexNormals(
            const std::vector<glm::vec3>& positions,
            const std::vector<std::vector<std::uint32_t>>& faces)
        {
            std::vector<glm::vec3> normals(positions.size(), glm::vec3{0.0f});

            for (const std::vector<std::uint32_t>& face : faces)
            {
                if (face.size() < 3u)
                {
                    continue;
                }

                const std::uint32_t root = face[0];
                for (std::size_t i = 1u; i + 1u < face.size(); ++i)
                {
                    const std::uint32_t a = root;
                    const std::uint32_t b = face[i];
                    const std::uint32_t c = face[i + 1u];
                    if (a >= positions.size() || b >= positions.size() || c >= positions.size())
                    {
                        continue;
                    }

                    const glm::vec3 weightedNormal =
                        glm::cross(positions[b] - positions[a],
                                   positions[c] - positions[a]);
                    const float length = glm::length(weightedNormal);
                    if (!std::isfinite(length) || length <= 1.0e-12f)
                    {
                        continue;
                    }

                    normals[a] += weightedNormal;
                    normals[b] += weightedNormal;
                    normals[c] += weightedNormal;
                }
            }

            for (glm::vec3& normal : normals)
            {
                const float length = glm::length(normal);
                if (std::isfinite(length) && length > 1.0e-6f)
                {
                    normal /= length;
                }
                else
                {
                    normal = glm::vec3{0.0f};
                }
            }

            return normals;
        }

        [[nodiscard]] std::vector<glm::vec3> ResolveVertexNormals(
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            const std::vector<glm::vec3>& positions,
            const std::vector<std::vector<std::uint32_t>>& faces)
        {
            const auto explicitNormals =
                meshPayload.Vertices.Get<glm::vec3>(kNormalProperty);
            if (explicitNormals &&
                explicitNormals.Vector().size() == positions.size())
            {
                return explicitNormals.Vector();
            }

            return ComputeAreaWeightedVertexNormals(positions, faces);
        }

        void WriteVertexNormals(
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::vector<glm::vec3>& normals)
        {
            if (mesh.VerticesSize() != normals.size())
            {
                return;
            }

            auto normalProperty = mesh.VertexProperties().GetOrAdd<glm::vec3>(
                std::string{kNormalProperty},
                glm::vec3{0.0f});
            normalProperty.Vector() = normals;
        }

        [[nodiscard]] bool CanUseDisconnectedRenderableFallback(
            const Geometry::Mesh::Conversion::ToHalfedgeMeshResult& converted) noexcept
        {
            bool hasRenderableTopologyFailure = false;
            for (const Geometry::Mesh::Conversion::ConversionDiagnostic& diagnostic :
                 converted.Diagnostics)
            {
                if (diagnostic.Severity !=
                    Geometry::MeshSoup::ValidationSeverity::Error)
                {
                    continue;
                }

                if (diagnostic.Kind ==
                    Geometry::Mesh::Conversion::ConversionDiagnosticKind::AddFaceFailed)
                {
                    hasRenderableTopologyFailure = true;
                    continue;
                }

                if (diagnostic.Kind !=
                    Geometry::Mesh::Conversion::ConversionDiagnosticKind::ValidationDiagnostic)
                {
                    return false;
                }

                if (diagnostic.ValidationKind ==
                        Geometry::MeshSoup::ValidationDiagnosticKind::NonManifoldEdge ||
                    diagnostic.ValidationKind ==
                        Geometry::MeshSoup::ValidationDiagnosticKind::InconsistentWinding)
                {
                    hasRenderableTopologyFailure = true;
                    continue;
                }

                return false;
            }
            return hasRenderableTopologyFailure;
        }

        [[nodiscard]] std::optional<Geometry::HalfedgeMesh::Mesh>
        BuildDisconnectedRenderableMesh(
            const std::vector<glm::vec3>& positions,
            const std::vector<std::vector<std::uint32_t>>& faces,
            const std::vector<glm::vec3>& normals)
        {
            if (positions.empty() || faces.empty() || normals.size() != positions.size())
            {
                return std::nullopt;
            }

            Geometry::HalfedgeMesh::Mesh mesh;
            auto normalProperty = mesh.VertexProperties().GetOrAdd<glm::vec3>(
                std::string{kNormalProperty},
                glm::vec3{0.0f});
            std::vector<Geometry::VertexHandle> faceVertices;

            for (const std::vector<std::uint32_t>& face : faces)
            {
                if (face.size() < 3u)
                {
                    return std::nullopt;
                }

                faceVertices.clear();
                faceVertices.reserve(face.size());
                for (const std::uint32_t index : face)
                {
                    if (index >= positions.size())
                    {
                        return std::nullopt;
                    }
                    const Geometry::VertexHandle vertex = mesh.AddVertex(positions[index]);
                    if (!vertex.IsValid())
                    {
                        return std::nullopt;
                    }
                    normalProperty.Vector()[vertex.Index] = normals[index];
                    faceVertices.push_back(vertex);
                }

                if (!mesh.AddFace(faceVertices).has_value())
                {
                    return std::nullopt;
                }
            }

            return mesh;
        }
    }

    Core::Expected<Geometry::HalfedgeMesh::Mesh> BuildRuntimeHalfedgeMeshWithNormals(
        const Geometry::MeshIO::MeshIOResult& meshPayload,
        const RuntimeMeshMaterializationOptions options)
    {
        const auto positions = meshPayload.Vertices.Get<glm::vec3>(kPositionProperty);
        if (!positions || positions.Vector().empty())
        {
            return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                Core::ErrorCode::AssetInvalidData);
        }

        const auto faces =
            meshPayload.Faces.Get<std::vector<std::uint32_t>>(kFaceVerticesProperty);
        if (!faces || faces.Vector().empty())
        {
            return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                Core::ErrorCode::AssetInvalidData);
        }

        Geometry::MeshSoup::IndexedMesh soup{};
        for (const glm::vec3& position : positions.Vector())
        {
            static_cast<void>(soup.AddVertex(position));
        }

        for (const std::vector<std::uint32_t>& face : faces.Vector())
        {
            if (face.size() < 3u)
            {
                return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                    Core::ErrorCode::InvalidFormat);
            }
            for (const std::uint32_t index : face)
            {
                if (index >= soup.VertexCount())
                {
                    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                        Core::ErrorCode::OutOfRange);
                }
            }
            static_cast<void>(soup.AddFace(face));
        }

        std::vector<glm::vec3> normals = ResolveVertexNormals(
            meshPayload,
            positions.Vector(),
            faces.Vector());

        auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(soup);
        if (!converted.Succeeded())
        {
            if (options.AllowDisconnectedRenderableFallback &&
                CanUseDisconnectedRenderableFallback(converted))
            {
                if (std::optional<Geometry::HalfedgeMesh::Mesh> fallback =
                        BuildDisconnectedRenderableMesh(
                            positions.Vector(),
                            faces.Vector(),
                            normals))
                {
                    return std::move(*fallback);
                }
            }
            return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                Core::ErrorCode::InvalidFormat);
        }

        WriteVertexNormals(converted.Mesh, normals);
        return std::move(converted.Mesh);
    }
}
