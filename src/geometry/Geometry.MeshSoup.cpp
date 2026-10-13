module;

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
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

import Geometry.RobustPredicates;
import Geometry.Properties;
import Geometry.Validation;

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

    std::size_t ValidationResult::Count(ValidationDiagnosticKind kind) const noexcept
    {
        return Counts[static_cast<std::size_t>(kind)];
    }

    void ValidationResult::AddDiagnostic(ValidationDiagnostic diagnostic)
    {
        auto& count = Counts[static_cast<std::size_t>(diagnostic.Kind)];
        if (count++ < 16u)
            Diagnostics.push_back(std::move(diagnostic));
    }

    double RelativeTriangleAreaSquared(glm::vec3 a, glm::vec3 b, glm::vec3 c) noexcept
    {
        const glm::dvec3 ab = glm::dvec3{b} - glm::dvec3{a};
        const glm::dvec3 ac = glm::dvec3{c} - glm::dvec3{a};
        const glm::dvec3 bc = glm::dvec3{c} - glm::dvec3{b};
        const auto normal = glm::cross(ab, ac);
        const double longestSquared = std::max({glm::dot(ab, ab), glm::dot(ac, ac), glm::dot(bc, bc)});
        return longestSquared == 0.0 ? 0.0 : glm::dot(normal, normal) / (longestSquared * longestSquared);
    }

    bool IsNearlyDegenerateTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c) noexcept
    {
        return !(RelativeTriangleAreaSquared(a, b, c) > 1.0e-24);
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
        case ValidationDiagnosticKind::NonFinitePosition:
            return "NonFinitePosition";
        case ValidationDiagnosticKind::InvalidFaceSize:
            return "InvalidFaceSize";
        case ValidationDiagnosticKind::RepeatedFaceIndex:
            return "RepeatedFaceIndex";
        case ValidationDiagnosticKind::PoorFaceQuality:
            return "PoorFaceQuality";
        case ValidationDiagnosticKind::DuplicateFace:
            return "DuplicateFace";
        case ValidationDiagnosticKind::Count:
            break;
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

        struct FaceMeasure
        {
            double RelativeAreaSquared{0.0};
            bool ZeroArea{true};
        };

        [[nodiscard]] FaceMeasure MeasureFace(const PolygonFace& face,
                                              std::span<const glm::vec3> positions)
        {
            const glm::vec3 first = positions[face.Indices[0]];
            if (face.Indices.size() == 3u)
            {
                const auto b = positions[face.Indices[1]];
                const auto c = positions[face.Indices[2]];
                const bool zero = RobustPredicates::ExactOrientation2D({first.x, first.y}, {b.x, b.y}, {c.x, c.y}) == 0 &&
                    RobustPredicates::ExactOrientation2D({first.y, first.z}, {b.y, b.z}, {c.y, c.z}) == 0 &&
                    RobustPredicates::ExactOrientation2D({first.z, first.x}, {b.z, b.x}, {c.z, c.x}) == 0;
                return {RelativeTriangleAreaSquared(first, b, c), zero};
            }
            glm::dvec3 normal{0.0};
            double longestSquared = 0.0;
            bool collinear = true;
            glm::vec3 second = first;
            for (const Index index : face.Indices)
                if (positions[index] != first) { second = positions[index]; break; }
            for (std::size_t i = 0; i < face.Indices.size(); ++i)
            {
                const glm::vec3 a = positions[face.Indices[i]];
                const glm::vec3 b = positions[face.Indices[(i + 1) % face.Indices.size()]];
                const glm::dvec3 edge = glm::dvec3{b} - glm::dvec3{a};
                longestSquared = std::max(longestSquared, glm::dot(edge, edge));
                normal += glm::cross(glm::dvec3{a} - glm::dvec3{first},
                                     glm::dvec3{b} - glm::dvec3{first});
                collinear = collinear &&
                    RobustPredicates::ExactOrientation2D({first.x, first.y}, {second.x, second.y}, {a.x, a.y}) == 0 &&
                    RobustPredicates::ExactOrientation2D({first.y, first.z}, {second.y, second.z}, {a.y, a.z}) == 0 &&
                    RobustPredicates::ExactOrientation2D({first.z, first.x}, {second.z, second.x}, {a.z, a.x}) == 0;
            }
            // Non-collinear polygons with cancelling area vectors are quality
            // warnings, not proofs of zero area or a simplicity test.
            const bool zero = collinear;
            return {longestSquared > 0.0 ? glm::dot(normal, normal) / (longestSquared * longestSquared) : 0.0, zero};
        }

        void AppendDuplicateVertexDiagnostic(ValidationResult& result,
                                             const std::size_t vertexIndex,
                                             const std::size_t otherVertexIndex)
        {
            result.AddDiagnostic(ValidationDiagnostic{
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

        void CheckStructure(ValidationResult& result, IndexedMeshView view,
                            std::vector<std::uint8_t>& usable)
        {
            std::vector<std::uint8_t> finite(view.Positions.size());
            for (std::size_t i = 0; i < view.Positions.size(); ++i)
            {
                finite[i] = Validation::IsFinite(view.Positions[i]);
                if (!finite[i])
                    result.AddDiagnostic({.Kind = ValidationDiagnosticKind::NonFinitePosition,
                                          .VertexIndex = static_cast<Index>(i)});
            }
            usable.assign(view.Faces.size(), false);
            for (std::size_t f = 0; f < view.Faces.size(); ++f)
            {
                const auto& face = view.Faces[f];
                result.CornerCount += face.Indices.size();
                bool valid = true;
                for (const Index index : face.Indices)
                {
                    if (index >= view.Positions.size())
                    {
                        result.AddDiagnostic({.Kind = ValidationDiagnosticKind::InvalidIndex,
                                              .FaceIndex = f, .VertexIndex = index,
                                              .ExpectedCount = view.Positions.size(), .ActualCount = index});
                        valid = false;
                    }
                    else if (!finite[index]) valid = false;
                }
                if (face.Indices.size() < 3u)
                {
                    result.AddDiagnostic({.Kind = ValidationDiagnosticKind::InvalidFaceSize, .FaceIndex = f});
                    valid = false;
                }
                if (ContainsDuplicateIndex(face))
                {
                    result.AddDiagnostic({.Kind = ValidationDiagnosticKind::RepeatedFaceIndex, .FaceIndex = f});
                    valid = false;
                }
                usable[f] = valid;
            }
            result.StructureChecked = true;
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
            observations.reserve(result.CornerCount);
            std::unordered_map<std::uint64_t, std::size_t> observationByEdge;
            observationByEdge.reserve(result.CornerCount);
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
                        result.AddDiagnostic(ValidationDiagnostic{
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
                        result.AddDiagnostic(ValidationDiagnostic{
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
            result.BoundaryEdgeCount = static_cast<std::size_t>(std::ranges::count_if(
                observations, [](const EdgeObservation& edge) { return edge.IncidentCount == 1u; }));
            std::map<std::vector<Index>, std::size_t> faces;
            for (std::size_t f = 0; f < view.Faces.size(); ++f)
            {
                if (!validForTopology[f]) continue;
                auto key = view.Faces[f].Indices;
                std::rotate(key.begin(), std::min_element(key.begin(), key.end()), key.end());
                auto reversed = key;
                std::reverse(reversed.begin() + 1, reversed.end());
                if (reversed < key) key = std::move(reversed);
                if (!faces.emplace(std::move(key), f).second)
                    result.AddDiagnostic({.Kind = ValidationDiagnosticKind::DuplicateFace,
                                          .Severity = ValidationSeverity::Warning, .FaceIndex = f});
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

            result.AddDiagnostic(ValidationDiagnostic{
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
            const std::size_t corners = result.CornerCount;
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

    ValidationContext::ValidationContext(IndexedMeshView view, ValidationOptions options)
        : m_View(view), m_Options(options) {}

    void ValidationContext::CheckStructure()
    {
        if (!m_Result.StructureChecked)
            MeshSoup::CheckStructure(m_Result, m_View, m_UsableFaces);
    }

    void ValidationContext::CheckDuplicateVertices()
    {
        CheckStructure();
        if (m_Result.Executed.DuplicateVertices) return;
        AppendDuplicateVertexDiagnostics(m_Result, m_View, m_Options);
        m_Result.Executed.DuplicateVertices = true;
    }

    void ValidationContext::CheckFaceGeometry()
    {
        CheckStructure();
        if (m_Result.Executed.FaceGeometry) return;
        for (std::size_t f = 0; f < m_View.Faces.size(); ++f)
        {
            if (!m_UsableFaces[f]) continue;
            const auto measure = MeasureFace(m_View.Faces[f], m_View.Positions);
            if (measure.ZeroArea)
                m_Result.AddDiagnostic({.Kind = ValidationDiagnosticKind::DegenerateFace,
                    .Severity = m_Options.AllowZeroAreaFaces ? ValidationSeverity::Warning : ValidationSeverity::Error,
                    .FaceIndex = f});
            else if (measure.RelativeAreaSquared <= 1.0e-24)
                m_Result.AddDiagnostic({.Kind = ValidationDiagnosticKind::PoorFaceQuality,
                    .Severity = ValidationSeverity::Warning, .FaceIndex = f});
        }
        m_Result.Executed.FaceGeometry = true;
    }

    void ValidationContext::CheckTopology()
    {
        CheckStructure();
        if (m_Result.Executed.Topology) return;
        AppendTopologyDiagnostics(m_Result, m_View, m_UsableFaces);
        m_Result.Executed.Topology = true;
    }

    void ValidationContext::CheckAttributes()
    {
        CheckStructure();
        if (m_Result.Executed.Attributes) return;
        AppendAttributeDiagnostics(m_Result, m_View);
        m_Result.Executed.Attributes = true;
    }

    const ValidationResult& ValidationContext::Validate()
    {
        CheckStructure();
        if (m_Options.Checks.DuplicateVertices) CheckDuplicateVertices();
        if (m_Options.Checks.FaceGeometry) CheckFaceGeometry();
        if (m_Options.Checks.Topology) CheckTopology();
        if (m_Options.Checks.Attributes) CheckAttributes();
        return m_Result;
    }

    ValidationResult Validate(IndexedMeshView view, const ValidationOptions& options)
    {
        ValidationContext context(view, options);
        return context.Validate();
    }

    ValidationResult Validate(const IndexedMesh& mesh, const ValidationOptions& options)
    {
        return Validate(BorrowView(mesh), options);
    }
}
