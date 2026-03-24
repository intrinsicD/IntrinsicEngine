module;

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloudView;

import Geometry.Properties;

export namespace Geometry::PointCloud
{
    class Cloud
    {
    public:
        Cloud();
        Cloud(const Cloud&)            = default;
        Cloud(Cloud&&) noexcept        = default;
        Cloud& operator=(const Cloud&) = default;
        Cloud& operator=(Cloud&&) noexcept = default;
        ~Cloud()                       = default;

        // ---- Capacity / sizing ----
        // Canonical names match Mesh (VerticesSize/IsEmpty) and Graph conventions.

        [[nodiscard]] std::size_t PointCount() const noexcept { return m_Points.Size(); }
        [[nodiscard]] bool        IsEmpty()    const noexcept { return m_Points.Size() == 0; }

        void Reserve(std::size_t n) { m_Points.Reserve(n); }
        void Clear();

        // ---- Point addition ----

        // Add a point with a mandatory position. Returns its handle.
        VertexHandle AddPoint(glm::vec3 position);

        // ---- Built-in attribute presence ----

        [[nodiscard]] bool HasNormals() const noexcept { return m_PNormal.IsValid(); }
        [[nodiscard]] bool HasColors()  const noexcept { return m_PColor.IsValid(); }
        [[nodiscard]] bool HasRadii()   const noexcept { return m_PRadius.IsValid(); }

        // ---- Built-in attribute activation ----
        // Call once to allocate the property; subsequent AddPoint calls will
        // initialise the slot with the default value supplied here.

        void EnableNormals(glm::vec3 defaultNormal = {0.f, 1.f, 0.f});
        void EnableColors (glm::vec4 defaultColor  = {1.f, 1.f, 1.f, 1.f});
        void EnableRadii  (float     defaultRadius = 0.f);

        // ---- Per-point accessors (by handle) ----

        [[nodiscard]] const glm::vec3& Position(VertexHandle p) const { return m_PPoint[p]; }
        [[nodiscard]]       glm::vec3& Position(VertexHandle p)       { return m_PPoint[p]; }

        [[nodiscard]] const glm::vec3& Normal(VertexHandle p) const { return m_PNormal[p]; }
        [[nodiscard]]       glm::vec3& Normal(VertexHandle p)       { return m_PNormal[p]; }

        [[nodiscard]] const glm::vec4& Color(VertexHandle p) const { return m_PColor[p]; }
        [[nodiscard]]       glm::vec4& Color(VertexHandle p)       { return m_PColor[p]; }

        [[nodiscard]] float  Radius(VertexHandle p) const { return m_PRadius[p]; }
        [[nodiscard]] float& Radius(VertexHandle p)       { return m_PRadius[p]; }

        // ---- Span accessors (for bulk GPU upload / SIMD loops) ----

        [[nodiscard]] std::span<const glm::vec3> Positions() const { return m_PPoint.Span(); }
        [[nodiscard]] std::span<glm::vec3>       Positions()       { return m_PPoint.Span(); }

        [[nodiscard]] std::span<const glm::vec3> Normals() const { return m_PNormal.Span(); }
        [[nodiscard]] std::span<glm::vec3>       Normals()       { return m_PNormal.Span(); }

        [[nodiscard]] std::span<const glm::vec4> Colors() const { return m_PColor.Span(); }
        [[nodiscard]] std::span<glm::vec4>       Colors()       { return m_PColor.Span(); }

        [[nodiscard]] std::span<const float> Radii() const { return m_PRadius.Span(); }
        [[nodiscard]] std::span<float>       Radii()       { return m_PRadius.Span(); }

        // ---- Handle from index ----

        [[nodiscard]] static constexpr VertexHandle Handle(std::size_t index) noexcept
        {
            return VertexHandle{static_cast<PropertyIndex>(index)};
        }

        // ---- User-defined per-point properties ----

        template <class T>
        [[nodiscard]] VertexProperty<T> GetOrAddVertexProperty(std::string_view name, T defaultValue = T())
        {
            return VertexProperty<T>(m_Points.GetOrAdd<T>(std::string{name}, std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return ConstPropertySet(m_Points).Get<T>(name);
        }

        [[nodiscard]] Vertices& PointProperties() noexcept { return m_Points; }
        [[nodiscard]] ConstPropertySet PointProperties() const noexcept { return ConstPropertySet(m_Points); } // NOLINT(readability-convert-member-functions-to-static)

        // Validate internal consistency (built-in optional arrays, if present, match Size()).
        [[nodiscard]] bool IsValid() const noexcept;

    private:
        Vertices m_Points;

        // Built-in properties — always present (position) or lazily allocated.
        VertexProperty<glm::vec3> m_PPoint;   // "p:position"
        VertexProperty<glm::vec3> m_PNormal;  // "p:normal"   (optional)
        VertexProperty<glm::vec4> m_PColor;   // "p:color"    (optional)
        VertexProperty<float>     m_PRadius;  // "p:radius"   (optional)
    };

}
