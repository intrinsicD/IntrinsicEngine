module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

export module Geometry.MeshSoup;

import Geometry.Properties;

export namespace Geometry::MeshSoup
{
    using Index = std::uint32_t;

    struct PolygonFace
    {
        std::vector<Index> Indices{};
    };

    enum class AttributeDomain : std::uint8_t
    {
        Vertex,
        Face,
        Corner,
    };

    struct IndexedMeshView
    {
        std::span<const glm::vec3> Positions;
        std::span<const PolygonFace> Faces;
        const PropertySet* VertexProperties{nullptr};
        const PropertySet* FaceProperties{nullptr};
        const PropertySet* CornerProperties{nullptr};
    };

    class IndexedMesh
    {
    public:
        IndexedMesh();
        IndexedMesh(const IndexedMesh& other);
        IndexedMesh(IndexedMesh&& other) noexcept;
        IndexedMesh& operator=(const IndexedMesh& other);
        IndexedMesh& operator=(IndexedMesh&& other) noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept { return m_VertexProperties.Empty() && m_Faces.empty(); }
        [[nodiscard]] std::size_t VertexCount() const noexcept { return m_VertexProperties.Size(); }
        [[nodiscard]] std::size_t FaceCount() const noexcept { return m_Faces.size(); }
        [[nodiscard]] std::size_t CornerCount() const noexcept { return m_CornerProperties.Size(); }

        [[nodiscard]] std::span<const glm::vec3> Positions() const noexcept { return m_VPoint.Span(); }
        [[nodiscard]] std::span<glm::vec3> Positions() noexcept { return m_VPoint.Span(); }

        [[nodiscard]] std::span<const PolygonFace> Faces() const noexcept { return m_Faces; }
        [[nodiscard]] std::span<PolygonFace> Faces() noexcept { return m_Faces; }

        [[nodiscard]] Vertices& VertexProperties() noexcept { return m_VertexProperties; }
        [[nodiscard]] ConstPropertySet VertexProperties() const noexcept { return ConstPropertySet(m_VertexProperties); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] Geometry::Faces& FaceProperties() noexcept { return m_FaceProperties; }
        [[nodiscard]] ConstPropertySet FaceProperties() const noexcept { return ConstPropertySet(m_FaceProperties); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] PropertySet& CornerProperties() noexcept { return m_CornerProperties; }
        [[nodiscard]] ConstPropertySet CornerProperties() const noexcept { return ConstPropertySet(m_CornerProperties); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] const glm::vec3& Position(std::size_t index) const
        {
            return m_VPoint[VertexHandle{static_cast<PropertyIndex>(index)}];
        }

        [[nodiscard]] glm::vec3& Position(std::size_t index)
        {
            return m_VPoint[VertexHandle{static_cast<PropertyIndex>(index)}];
        }

        [[nodiscard]] const PolygonFace& Face(std::size_t index) const { return m_Faces[index]; }
        [[nodiscard]] PolygonFace& Face(std::size_t index) { return m_Faces[index]; }

        template <class T>
        [[nodiscard]] VertexProperty<T> GetOrAddVertexProperty(std::string_view name, T defaultValue = T())
        {
            return VertexProperty<T>(m_VertexProperties.GetOrAdd<T>(std::string{name}, std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return ConstPropertySet(m_VertexProperties).Get<T>(name);
        }

        template <class T>
        [[nodiscard]] FaceProperty<T> GetOrAddFaceProperty(std::string_view name, T defaultValue = T())
        {
            return FaceProperty<T>(m_FaceProperties.GetOrAdd<T>(std::string{name}, std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetFaceProperty(std::string_view name) const
        {
            return ConstPropertySet(m_FaceProperties).Get<T>(name);
        }

        template <class T>
        [[nodiscard]] Property<T> GetOrAddCornerProperty(std::string_view name, T defaultValue = T())
        {
            return Property<T>(m_CornerProperties.GetOrAdd<T>(std::string{name}, std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetCornerProperty(std::string_view name) const
        {
            return ConstPropertySet(m_CornerProperties).Get<T>(name);
        }

        [[nodiscard]] Index AddVertex(glm::vec3 position);
        [[nodiscard]] std::size_t AddFace(std::vector<Index> indices);
        [[nodiscard]] std::size_t AddTriangle(Index v0, Index v1, Index v2);
        void Clear();
        [[nodiscard]] IndexedMeshView BorrowView() const noexcept;

    private:
        void EnsureProperties();

        std::vector<PolygonFace> m_Faces;
        Vertices m_VertexProperties;
        Geometry::Faces m_FaceProperties;
        PropertySet m_CornerProperties;
        VertexProperty<glm::vec3> m_VPoint;
    };

    enum class ValidationSeverity : std::uint8_t
    {
        Warning,
        Error,
    };

    enum class ValidationDiagnosticKind : std::uint8_t
    {
        DuplicateVertex,
        InvalidIndex,
        DegenerateFace,
        NonManifoldEdge,
        InconsistentWinding,
        AttributeArityMismatch,
    };

    struct ValidationOptions
    {
        float DuplicateVertexTolerance{0.0f};
        float DegenerateAreaTolerance{1.0e-12f};
    };

    struct ValidationDiagnostic
    {
        ValidationDiagnosticKind Kind{ValidationDiagnosticKind::InvalidIndex};
        ValidationSeverity Severity{ValidationSeverity::Error};
        std::size_t FaceIndex{static_cast<std::size_t>(-1)};
        Index VertexIndex{static_cast<Index>(-1)};
        Index OtherVertexIndex{static_cast<Index>(-1)};
        Index EdgeStart{static_cast<Index>(-1)};
        Index EdgeEnd{static_cast<Index>(-1)};
        std::string AttributeName;
        AttributeDomain AttributeDomainValue{AttributeDomain::Vertex};
        std::size_t ExpectedCount{0u};
        std::size_t ActualCount{0u};
    };

    struct ValidationResult
    {
        std::vector<ValidationDiagnostic> Diagnostics;

        [[nodiscard]] bool HasErrors() const noexcept;
        [[nodiscard]] std::size_t Count(ValidationDiagnosticKind kind) const noexcept;
    };

    [[nodiscard]] IndexedMeshView BorrowView(const IndexedMesh& mesh) noexcept;
    [[nodiscard]] std::string_view ToString(AttributeDomain domain) noexcept;
    [[nodiscard]] std::string_view ToString(ValidationDiagnosticKind kind) noexcept;
    [[nodiscard]] bool IsValid(const ValidationResult& result) noexcept;
    [[nodiscard]] ValidationResult Validate(IndexedMeshView view, const ValidationOptions& options = {});
    [[nodiscard]] ValidationResult Validate(const IndexedMesh& mesh, const ValidationOptions& options = {});
}
