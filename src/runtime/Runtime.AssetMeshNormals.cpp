module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
        constexpr const char* kTexcoordProperty = "v:texcoord";
        constexpr const char* kFaceVerticesProperty = "f:vertices";

        [[nodiscard]] bool ShouldCopyVertexProperty(const std::string_view name) noexcept
        {
            return name != kPositionProperty && name != kNormalProperty;
        }

        template <typename T>
        void CopyVertexProperty(
            const Geometry::PropertySet& source,
            Geometry::PropertySet& target,
            const std::string_view name,
            const std::size_t vertexCount)
        {
            if (!ShouldCopyVertexProperty(name))
            {
                return;
            }

            const auto property = source.Get<T>(name);
            if (!property || property.Vector().size() != vertexCount)
            {
                return;
            }

            auto targetProperty = target.GetOrAdd<T>(std::string{name}, T{});
            targetProperty.Vector() = property.Vector();
        }

        template <typename T>
        void CopyVertexPropertyRemapped(
            const Geometry::PropertySet& source,
            Geometry::PropertySet& target,
            const std::string_view name,
            const std::vector<std::uint32_t>& sourceVertexForTargetVertex)
        {
            if (!ShouldCopyVertexProperty(name))
            {
                return;
            }

            const auto property = source.Get<T>(name);
            if (!property)
            {
                return;
            }
            const auto& values = property.Vector();
            for (const std::uint32_t sourceIndex : sourceVertexForTargetVertex)
            {
                if (sourceIndex >= values.size())
                {
                    return;
                }
            }

            auto targetProperty = target.GetOrAdd<T>(std::string{name}, T{});
            auto& out = targetProperty.Vector();
            out.resize(sourceVertexForTargetVertex.size());
            for (std::size_t i = 0u; i < sourceVertexForTargetVertex.size(); ++i)
            {
                out[i] = values[sourceVertexForTargetVertex[i]];
            }
        }

        void CopySupportedVertexProperties(
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::size_t vertexCount)
        {
            Geometry::PropertySet& target = mesh.VertexProperties();
            for (const std::string& name : meshPayload.Vertices.Properties())
            {
                CopyVertexProperty<glm::vec2>(meshPayload.Vertices, target, name, vertexCount);
                CopyVertexProperty<glm::vec3>(meshPayload.Vertices, target, name, vertexCount);
                CopyVertexProperty<glm::vec4>(meshPayload.Vertices, target, name, vertexCount);
                CopyVertexProperty<float>(meshPayload.Vertices, target, name, vertexCount);
                CopyVertexProperty<std::uint32_t>(meshPayload.Vertices, target, name, vertexCount);
            }
        }

        void CopySupportedVertexPropertiesRemapped(
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::vector<std::uint32_t>& sourceVertexForTargetVertex)
        {
            Geometry::PropertySet& target = mesh.VertexProperties();
            for (const std::string& name : meshPayload.Vertices.Properties())
            {
                CopyVertexPropertyRemapped<glm::vec2>(
                    meshPayload.Vertices, target, name, sourceVertexForTargetVertex);
                CopyVertexPropertyRemapped<glm::vec3>(
                    meshPayload.Vertices, target, name, sourceVertexForTargetVertex);
                CopyVertexPropertyRemapped<glm::vec4>(
                    meshPayload.Vertices, target, name, sourceVertexForTargetVertex);
                CopyVertexPropertyRemapped<float>(
                    meshPayload.Vertices, target, name, sourceVertexForTargetVertex);
                CopyVertexPropertyRemapped<std::uint32_t>(
                    meshPayload.Vertices, target, name, sourceVertexForTargetVertex);
            }
        }

        void WriteVertexNormalsRemapped(
            Geometry::HalfedgeMesh::Mesh& mesh,
            const std::vector<glm::vec3>& normals,
            const std::vector<std::uint32_t>& sourceVertexForTargetVertex)
        {
            if (mesh.VerticesSize() != sourceVertexForTargetVertex.size())
            {
                return;
            }

            auto normalProperty = mesh.VertexProperties().GetOrAdd<glm::vec3>(
                std::string{kNormalProperty},
                glm::vec3{0.0f});
            auto& out = normalProperty.Vector();
            out.resize(sourceVertexForTargetVertex.size());
            for (std::size_t i = 0u; i < sourceVertexForTargetVertex.size(); ++i)
            {
                const std::uint32_t sourceIndex = sourceVertexForTargetVertex[i];
                out[i] = sourceIndex < normals.size()
                    ? normals[sourceIndex]
                    : glm::vec3{0.0f};
            }
        }

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

        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool HasValidTexcoords(const Geometry::HalfedgeMesh::Mesh& mesh)
        {
            const auto texcoords = mesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty);
            if (!texcoords || texcoords.Vector().size() != mesh.VerticesSize())
            {
                return false;
            }
            for (const glm::vec2 texcoord : texcoords.Vector())
            {
                if (!IsFinite(texcoord))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool HasValidTexcoords(
            const Geometry::MeshIO::MeshIOResult& meshPayload) noexcept
        {
            const auto texcoords =
                meshPayload.Vertices.Get<glm::vec2>(kTexcoordProperty);
            if (!texcoords ||
                texcoords.Vector().size() != meshPayload.Vertices.Size())
            {
                return false;
            }
            for (const glm::vec2 texcoord : texcoords.Vector())
            {
                if (!IsFinite(texcoord))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] float NormalizeAxis(
            const float value,
            const float minValue,
            const float extent,
            const std::size_t index,
            const std::size_t count) noexcept
        {
            if (std::isfinite(extent) && extent > 1.0e-6f)
            {
                return (value - minValue) / extent;
            }
            return count > 1u
                ? static_cast<float>(index) / static_cast<float>(count - 1u)
                : 0.0f;
        }

        void WriteFallbackTexcoords(Geometry::HalfedgeMesh::Mesh& mesh)
        {
            const std::span<const glm::vec3> positions = mesh.Positions();
            auto texcoords = mesh.VertexProperties().GetOrAdd<glm::vec2>(
                std::string{kTexcoordProperty},
                glm::vec2{0.0f});
            auto& out = texcoords.Vector();
            out.assign(positions.size(), glm::vec2{0.0f});
            if (positions.empty())
            {
                return;
            }

            glm::vec3 minP{positions[0]};
            glm::vec3 maxP{positions[0]};
            for (const glm::vec3 position : positions)
            {
                minP = glm::min(minP, position);
                maxP = glm::max(maxP, position);
            }

            const glm::vec3 extent = maxP - minP;
            std::array<int, 3u> axes{0, 1, 2};
            std::sort(
                axes.begin(),
                axes.end(),
                [extent](const int lhs, const int rhs)
                {
                    return extent[lhs] > extent[rhs];
                });
            const int uAxis = axes[0];
            const int vAxis = axes[1];

            for (std::size_t i = 0u; i < positions.size(); ++i)
            {
                const glm::vec3 position = positions[i];
                out[i] = glm::vec2{
                    NormalizeAxis(position[uAxis], minP[uAxis], extent[uAxis], i, positions.size()),
                    NormalizeAxis(position[vAxis], minP[vAxis], extent[vAxis], i, positions.size()),
                };
            }
        }

        void EnsureRuntimeMeshTexcoords(Geometry::HalfedgeMesh::Mesh& mesh)
        {
            if (!HasValidTexcoords(mesh))
            {
                WriteFallbackTexcoords(mesh);
            }
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
            const Geometry::MeshIO::MeshIOResult& meshPayload,
            const std::vector<glm::vec3>& positions,
            const std::vector<std::vector<std::uint32_t>>& faces,
            const std::vector<glm::vec3>& normals)
        {
            if (positions.empty() || faces.empty() || normals.size() != positions.size())
            {
                return std::nullopt;
            }

            Geometry::HalfedgeMesh::Mesh mesh;
            std::vector<Geometry::VertexHandle> faceVertices;
            std::vector<std::uint32_t> sourceVertexForTargetVertex;

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
                    sourceVertexForTargetVertex.push_back(index);
                    faceVertices.push_back(vertex);
                }

                if (!mesh.AddFace(faceVertices).has_value())
                {
                    return std::nullopt;
                }
            }

            CopySupportedVertexPropertiesRemapped(
                meshPayload,
                mesh,
                sourceVertexForTargetVertex);
            WriteVertexNormalsRemapped(mesh, normals, sourceVertexForTargetVertex);
            return mesh;
        }
    }

    bool MeshPayloadHasValidVertexTexcoords(
        const Geometry::MeshIO::MeshIOResult& meshPayload) noexcept
    {
        return HasValidTexcoords(meshPayload);
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
                            meshPayload,
                            positions.Vector(),
                            faces.Vector(),
                            normals))
                {
                    EnsureRuntimeMeshTexcoords(*fallback);
                    return std::move(*fallback);
                }
            }
            return Core::Err<Geometry::HalfedgeMesh::Mesh>(
                Core::ErrorCode::InvalidFormat);
        }

        CopySupportedVertexProperties(meshPayload, converted.Mesh, positions.Vector().size());
        WriteVertexNormals(converted.Mesh, normals);
        EnsureRuntimeMeshTexcoords(converted.Mesh);
        return std::move(converted.Mesh);
    }
}
