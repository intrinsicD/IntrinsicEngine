module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.MeshSoup;

import Geometry.Properties;

namespace Geometry::MeshSoup
{
    IndexedMesh::IndexedMesh() // NOLINT(cppcoreguidelines-pro-type-member-init)
        : m_Faces(),
          m_VertexProperties(),
          m_FaceProperties(),
          m_CornerProperties(),
          m_VPoint()
    {
        EnsureProperties();
    }

    IndexedMesh::IndexedMesh(const IndexedMesh& other) // NOLINT(cppcoreguidelines-pro-type-member-init)
        : m_Faces(other.m_Faces),
          m_VertexProperties(other.m_VertexProperties),
          m_FaceProperties(other.m_FaceProperties),
          m_CornerProperties(other.m_CornerProperties),
          m_VPoint()
    {
        EnsureProperties();
    }

    IndexedMesh::IndexedMesh(IndexedMesh&& other) noexcept // NOLINT(cppcoreguidelines-pro-type-member-init)
        : m_Faces(std::move(other.m_Faces)),
          m_VertexProperties(std::move(other.m_VertexProperties)),
          m_FaceProperties(std::move(other.m_FaceProperties)),
          m_CornerProperties(std::move(other.m_CornerProperties)),
          m_VPoint()
    {
        EnsureProperties();
        other.EnsureProperties();
    }

    IndexedMesh& IndexedMesh::operator=(const IndexedMesh& other)
    {
        if (this != &other)
        {
            m_Faces = other.m_Faces;
            m_VertexProperties = other.m_VertexProperties;
            m_FaceProperties = other.m_FaceProperties;
            m_CornerProperties = other.m_CornerProperties;
            EnsureProperties();
        }
        return *this;
    }

    IndexedMesh& IndexedMesh::operator=(IndexedMesh&& other) noexcept
    {
        if (this != &other)
        {
            m_Faces = std::move(other.m_Faces);
            m_VertexProperties = std::move(other.m_VertexProperties);
            m_FaceProperties = std::move(other.m_FaceProperties);
            m_CornerProperties = std::move(other.m_CornerProperties);
            EnsureProperties();
            other.EnsureProperties();
        }
        return *this;
    }

    Index IndexedMesh::AddVertex(glm::vec3 position) // NOLINT(readability-convert-member-functions-to-static)
    {
        m_VertexProperties.PushBack();
        const auto index = static_cast<Index>(m_VertexProperties.Size() - 1u);
        m_VPoint[VertexHandle{index}] = position;
        return index;
    }

    std::size_t IndexedMesh::AddFace(std::vector<Index> indices) // NOLINT(readability-convert-member-functions-to-static)
    {
        const std::size_t cornerCount = indices.size();
        m_Faces.push_back(PolygonFace{.Indices = std::move(indices)});
        m_FaceProperties.PushBack();
        for (std::size_t i = 0u; i < cornerCount; ++i)
        {
            m_CornerProperties.PushBack();
        }
        return m_Faces.size() - 1u;
    }

    std::size_t IndexedMesh::AddTriangle(Index v0, Index v1, Index v2)
    {
        return AddFace(std::vector<Index>{v0, v1, v2});
    }

    void IndexedMesh::Clear()
    {
        m_Faces.clear();
        m_VertexProperties.Clear();
        m_FaceProperties.Clear();
        m_CornerProperties.Clear();
        EnsureProperties();
    }

    IndexedMeshView IndexedMesh::BorrowView() const noexcept
    {
        return IndexedMeshView{
            .Positions = m_VPoint.Span(),
            .Faces = m_Faces,
            .VertexProperties = &m_VertexProperties,
            .FaceProperties = &m_FaceProperties,
            .CornerProperties = &m_CornerProperties,
        };
    }

    void IndexedMesh::EnsureProperties()
    {
        m_VPoint = VertexProperty<glm::vec3>(m_VertexProperties.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f}));
    }

    bool ValidationResult::HasErrors() const noexcept // NOLINT(readability-convert-member-functions-to-static)
    {
        return std::ranges::any_of(Diagnostics, [](const ValidationDiagnostic& diagnostic) {
            return diagnostic.Severity == ValidationSeverity::Error;
        });
    }

    std::size_t ValidationResult::Count(ValidationDiagnosticKind kind) const noexcept // NOLINT(readability-convert-member-functions-to-static)
    {
        return static_cast<std::size_t>(std::ranges::count_if(Diagnostics, [kind](const ValidationDiagnostic& diagnostic) {
            return diagnostic.Kind == kind;
        }));
    }

    IndexedMeshView BorrowView(const IndexedMesh& mesh) noexcept
    {
        return mesh.BorrowView();
    }

    std::string_view ToString(AttributeDomain domain) noexcept
    {
        switch (domain)
        {
        case AttributeDomain::Vertex:
            return "Vertex";
        case AttributeDomain::Face:
            return "Face";
        case AttributeDomain::Corner:
            return "Corner";
        }
        return "Unknown";
    }

    std::string_view ToString(ValidationDiagnosticKind kind) noexcept
    {
        switch (kind)
        {
        case ValidationDiagnosticKind::DuplicateVertex:
            return "DuplicateVertex";
        case ValidationDiagnosticKind::InvalidIndex:
            return "InvalidIndex";
        case ValidationDiagnosticKind::DegenerateFace:
            return "DegenerateFace";
        case ValidationDiagnosticKind::NonManifoldEdge:
            return "NonManifoldEdge";
        case ValidationDiagnosticKind::InconsistentWinding:
            return "InconsistentWinding";
        case ValidationDiagnosticKind::AttributeArityMismatch:
            return "AttributeArityMismatch";
        }
        return "Unknown";
    }

    bool IsValid(const ValidationResult& result) noexcept
    {
        return !result.HasErrors();
    }

    namespace
    {
        constexpr Index kInvalidIndex = static_cast<Index>(-1);

        struct EdgeObservation
        {
            Index Start{kInvalidIndex};
            Index End{kInvalidIndex};
            std::size_t FaceIndex{static_cast<std::size_t>(-1)};
            std::size_t IncidentCount{0u};
            bool SameDirectedEdgeSeen{false};
            bool NonManifoldReported{false};
            bool WindingReported{false};
        };

        struct PositionKey
        {
            std::uint32_t X{0u};
            std::uint32_t Y{0u};
            std::uint32_t Z{0u};

            [[nodiscard]] friend bool operator==(const PositionKey& lhs, const PositionKey& rhs) noexcept = default;
        };

        struct PositionKeyHash
        {
            [[nodiscard]] std::size_t operator()(const PositionKey& key) const noexcept
            {
                std::size_t seed = key.X;
                seed ^= static_cast<std::size_t>(key.Y) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
                seed ^= static_cast<std::size_t>(key.Z) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
                return seed;
            }
        };

        [[nodiscard]] std::size_t CornerCount(IndexedMeshView view) noexcept
        {
            std::size_t count = 0u;
            for (const PolygonFace& face : view.Faces)
            {
                count += face.Indices.size();
            }
            return count;
        }

        [[nodiscard]] bool NearlySame(const glm::vec3& a, const glm::vec3& b, const float tolerance) noexcept
        {
            if (tolerance <= 0.0f)
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
            const glm::vec3 delta = a - b;
            return glm::dot(delta, delta) <= tolerance * tolerance;
        }

        [[nodiscard]] std::uint32_t CanonicalFloatBits(const float value) noexcept
        {
            return value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value);
        }

        [[nodiscard]] bool HasNaN(const glm::vec3& value) noexcept
        {
            return std::isnan(value.x) || std::isnan(value.y) || std::isnan(value.z);
        }

        [[nodiscard]] PositionKey MakePositionKey(const glm::vec3& value) noexcept
        {
            return PositionKey{
                .X = CanonicalFloatBits(value.x),
                .Y = CanonicalFloatBits(value.y),
                .Z = CanonicalFloatBits(value.z),
            };
        }

        [[nodiscard]] glm::vec3 NewellNormal(const PolygonFace& face, std::span<const glm::vec3> positions) noexcept
        {
            glm::vec3 normal{0.0f};
            for (std::size_t i = 0u; i < face.Indices.size(); ++i)
            {
                const glm::vec3& current = positions[face.Indices[i]];
                const glm::vec3& next = positions[face.Indices[(i + 1u) % face.Indices.size()]];
                normal.x += (current.y - next.y) * (current.z + next.z);
                normal.y += (current.z - next.z) * (current.x + next.x);
                normal.z += (current.x - next.x) * (current.y + next.y);
            }
            return normal;
        }

        [[nodiscard]] bool ContainsDuplicateIndex(const PolygonFace& face) noexcept
        {
            for (std::size_t i = 0u; i < face.Indices.size(); ++i)
            {
                for (std::size_t j = i + 1u; j < face.Indices.size(); ++j)
                {
                    if (face.Indices[i] == face.Indices[j])
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] bool HasOnlyValidIndices(const PolygonFace& face, const std::size_t vertexCount) noexcept
        {
            return std::ranges::all_of(face.Indices, [vertexCount](const Index index) {
                return static_cast<std::size_t>(index) < vertexCount;
            });
        }

        [[nodiscard]] bool IsDegenerateFace(const PolygonFace& face,
                                            std::span<const glm::vec3> positions,
                                            const ValidationOptions& options) noexcept
        {
            if (face.Indices.size() < 3u || ContainsDuplicateIndex(face))
            {
                return true;
            }
            const glm::vec3 normal = NewellNormal(face, positions);
            return glm::dot(normal, normal) <= options.DegenerateAreaTolerance;
        }

        void AppendDuplicateVertexDiagnostic(ValidationResult& result,
                                             const std::size_t vertexIndex,
                                             const std::size_t otherVertexIndex)
        {
            result.Diagnostics.push_back(ValidationDiagnostic{
                .Kind = ValidationDiagnosticKind::DuplicateVertex,
                .Severity = ValidationSeverity::Warning,
                .VertexIndex = static_cast<Index>(vertexIndex),
                .OtherVertexIndex = static_cast<Index>(otherVertexIndex),
                .AttributeName = {},
                .AttributeDomainValue = AttributeDomain::Vertex,
                .ExpectedCount = 0u,
                .ActualCount = 0u,
            });
        }

        void AppendExactDuplicateVertexDiagnostics(ValidationResult& result, IndexedMeshView view)
        {
            std::unordered_map<PositionKey, std::vector<Index>, PositionKeyHash> verticesByPosition;
            verticesByPosition.reserve(view.Positions.size());
            for (std::size_t i = 0u; i < view.Positions.size(); ++i)
            {
                const glm::vec3& position = view.Positions[i];
                if (HasNaN(position))
                {
                    continue;
                }

                std::vector<Index>& duplicates = verticesByPosition[MakePositionKey(position)];
                for (const Index duplicate : duplicates)
                {
                    AppendDuplicateVertexDiagnostic(result, duplicate, i);
                }
                duplicates.push_back(static_cast<Index>(i));
            }
        }

        void AppendDuplicateVertexDiagnostics(ValidationResult& result,
                                              IndexedMeshView view,
                                              const ValidationOptions& options)
        {
            if (options.DuplicateVertexTolerance <= 0.0f)
            {
                AppendExactDuplicateVertexDiagnostics(result, view);
                return;
            }

            for (std::size_t i = 0u; i < view.Positions.size(); ++i)
            {
                for (std::size_t j = i + 1u; j < view.Positions.size(); ++j)
                {
                    if (NearlySame(view.Positions[i], view.Positions[j], options.DuplicateVertexTolerance))
                    {
                        AppendDuplicateVertexDiagnostic(result, i, j);
                    }
                }
            }
        }

        void AppendFaceDiagnostics(ValidationResult& result,
                                   IndexedMeshView view,
                                   const ValidationOptions& options,
                                   std::vector<std::uint8_t>& validForTopology)
        {
            validForTopology.assign(view.Faces.size(), false);
            for (std::size_t faceIndex = 0u; faceIndex < view.Faces.size(); ++faceIndex)
            {
                const PolygonFace& face = view.Faces[faceIndex];
                bool hasInvalidIndex = false;
                for (const Index index : face.Indices)
                {
                    if (static_cast<std::size_t>(index) >= view.Positions.size())
                    {
                        hasInvalidIndex = true;
                        result.Diagnostics.push_back(ValidationDiagnostic{
                            .Kind = ValidationDiagnosticKind::InvalidIndex,
                            .Severity = ValidationSeverity::Error,
                            .FaceIndex = faceIndex,
                            .VertexIndex = index,
                            .AttributeName = {},
                            .AttributeDomainValue = AttributeDomain::Vertex,
                            .ExpectedCount = view.Positions.size(),
                            .ActualCount = static_cast<std::size_t>(index),
                        });
                    }
                }

                if (!hasInvalidIndex && IsDegenerateFace(face, view.Positions, options))
                {
                    result.Diagnostics.push_back(ValidationDiagnostic{
                        .Kind = ValidationDiagnosticKind::DegenerateFace,
                        .Severity = ValidationSeverity::Error,
                        .FaceIndex = faceIndex,
                        .AttributeName = {},
                        .AttributeDomainValue = AttributeDomain::Face,
                        .ExpectedCount = 0u,
                        .ActualCount = 0u,
                    });
                }

                validForTopology[faceIndex] = !hasInvalidIndex && !IsDegenerateFace(face, view.Positions, options) &&
                                              HasOnlyValidIndices(face, view.Positions.size());
            }
        }

        [[nodiscard]] std::uint64_t MakeUndirectedEdgeKey(const Index a, const Index b) noexcept
        {
            const Index lo = std::min(a, b);
            const Index hi = std::max(a, b);
            return (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
        }

        void AppendTopologyDiagnostics(ValidationResult& result,
                                       IndexedMeshView view,
                                       std::span<const std::uint8_t> validForTopology)
        {
            std::vector<EdgeObservation> observations;
            observations.reserve(CornerCount(view));
            std::unordered_map<std::uint64_t, std::size_t> observationByEdge;
            observationByEdge.reserve(CornerCount(view));
            for (std::size_t faceIndex = 0u; faceIndex < view.Faces.size(); ++faceIndex)
            {
                if (!validForTopology[faceIndex])
                {
                    continue;
                }

                const PolygonFace& face = view.Faces[faceIndex];
                for (std::size_t i = 0u; i < face.Indices.size(); ++i)
                {
                    const Index start = face.Indices[i];
                    const Index end = face.Indices[(i + 1u) % face.Indices.size()];
                    const std::uint64_t edgeKey = MakeUndirectedEdgeKey(start, end);
                    const auto [edgeIt, inserted] = observationByEdge.try_emplace(edgeKey, observations.size());
                    if (inserted)
                    {
                        observations.push_back(EdgeObservation{
                            .Start = start,
                            .End = end,
                            .FaceIndex = faceIndex,
                            .IncidentCount = 1u,
                        });
                        continue;
                    }

                    EdgeObservation& edge = observations[edgeIt->second];
                    ++edge.IncidentCount;
                    if (edge.Start == start && edge.End == end)
                    {
                        edge.SameDirectedEdgeSeen = true;
                    }

                    if (edge.IncidentCount > 2u && !edge.NonManifoldReported)
                    {
                        edge.NonManifoldReported = true;
                        result.Diagnostics.push_back(ValidationDiagnostic{
                            .Kind = ValidationDiagnosticKind::NonManifoldEdge,
                            .Severity = ValidationSeverity::Error,
                            .FaceIndex = faceIndex,
                            .EdgeStart = std::min(start, end),
                            .EdgeEnd = std::max(start, end),
                            .AttributeName = {},
                            .AttributeDomainValue = AttributeDomain::Face,
                            .ExpectedCount = 2u,
                            .ActualCount = edge.IncidentCount,
                        });
                    }

                    if (edge.SameDirectedEdgeSeen && !edge.WindingReported)
                    {
                        edge.WindingReported = true;
                        result.Diagnostics.push_back(ValidationDiagnostic{
                            .Kind = ValidationDiagnosticKind::InconsistentWinding,
                            .Severity = ValidationSeverity::Error,
                            .FaceIndex = faceIndex,
                            .EdgeStart = start,
                            .EdgeEnd = end,
                            .AttributeName = {},
                            .AttributeDomainValue = AttributeDomain::Face,
                            .ExpectedCount = 0u,
                            .ActualCount = 0u,
                        });
                    }
                }
            }
        }

        void AppendAttributeArityDiagnostic(ValidationResult& result,
                                            std::string_view name,
                                            AttributeDomain domain,
                                            std::size_t expected,
                                            std::size_t actual)
        {
            if (actual == expected)
            {
                return;
            }

            result.Diagnostics.push_back(ValidationDiagnostic{
                .Kind = ValidationDiagnosticKind::AttributeArityMismatch,
                .Severity = ValidationSeverity::Error,
                .AttributeName = std::string{name},
                .AttributeDomainValue = domain,
                .ExpectedCount = expected,
                .ActualCount = actual,
            });
        }

        void AppendAttributeDiagnostics(ValidationResult& result, IndexedMeshView view)
        {
            const std::size_t corners = CornerCount(view);
            if (view.VertexProperties != nullptr)
            {
                AppendAttributeArityDiagnostic(result,
                                               "vertex properties",
                                               AttributeDomain::Vertex,
                                               view.Positions.size(),
                                               view.VertexProperties->Size());
            }
            if (view.FaceProperties != nullptr)
            {
                AppendAttributeArityDiagnostic(result,
                                               "face properties",
                                               AttributeDomain::Face,
                                               view.Faces.size(),
                                               view.FaceProperties->Size());
            }
            if (view.CornerProperties != nullptr)
            {
                AppendAttributeArityDiagnostic(result,
                                               "corner properties",
                                               AttributeDomain::Corner,
                                               corners,
                                               view.CornerProperties->Size());
            }
        }
    }

    ValidationResult Validate(IndexedMeshView view, const ValidationOptions& options)
    {
        ValidationResult result;
        AppendDuplicateVertexDiagnostics(result, view, options);

        std::vector<std::uint8_t> validForTopology;
        AppendFaceDiagnostics(result, view, options, validForTopology);
        AppendTopologyDiagnostics(result, view, validForTopology);
        AppendAttributeDiagnostics(result, view);
        return result;
    }

    ValidationResult Validate(const IndexedMesh& mesh, const ValidationOptions& options)
    {
        return Validate(BorrowView(mesh), options);
    }
}
