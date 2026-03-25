module;

#include <memory>
#include <glm/glm.hpp>

module Geometry.PointCloud;

import Geometry.PointCloudFwd;

namespace Geometry::PointCloud
{
    Cloud::Cloud() : m_Properties(std::make_shared<CloudProperties>()), m_Vertices(m_Properties->Vertices),
                     m_DeletedVertices(m_Properties->DeletedVertices)
    {
        // Allocate the mandatory position property up-front.
        EnsureProperties();
    }

    Cloud::Cloud(PropertySet& Vertices, size_t& DeletedVertices) : m_Properties(std::make_shared<CloudProperties>()),
                                                                   m_Vertices(Vertices),
                                                                   m_DeletedVertices(DeletedVertices)
    {
        EnsureProperties();
    }

    Cloud::Cloud(const Cloud& rhs)
        : m_Properties(std::make_shared<CloudProperties>())
          , m_Vertices(m_Properties->Vertices)
          , m_DeletedVertices(m_Properties->DeletedVertices)
    {
        m_Vertices = rhs.m_Vertices;
        m_DeletedVertices = rhs.m_DeletedVertices;
        EnsureProperties();
    }

    Cloud::~Cloud() = default;

    Cloud& Cloud::operator=(const Cloud& other) noexcept
    {
        if (this != &other)
        {
            // Reference members are fixed at construction time; copy into the
            // currently-bound storage instead of rebinding `m_Properties`.
            m_Vertices = other.m_Vertices;
            m_DeletedVertices = other.m_DeletedVertices;
            EnsureProperties();
        }
        return *this;
    }

    Cloud& Cloud::operator=(Cloud&& other) noexcept
    {
        if (this != &other)
        {
            // Move the contents of the currently-bound sets; do not swap the
            // backing pointers because the mesh exposes reference members.
            if (&m_Vertices != &other.m_Vertices) m_Vertices = std::move(other.m_Vertices);
            m_DeletedVertices = other.m_DeletedVertices;
            EnsureProperties();
        }
        return *this;
    }

    void Cloud::EnsureProperties()
    {
        m_PPoint = VertexProperty<glm::vec3>(m_Vertices.GetOrAdd<glm::vec3>("p:position", glm::vec3(0.f)));
        m_VDeleted = VertexProperty<bool>(m_Vertices.GetOrAdd<bool>("p:deleted", false));
        if (m_Vertices.Exists("p:normal")) EnableNormals();
        if (m_Vertices.Exists("p:color")) EnableColors();
        if (m_Vertices.Exists("p:radius")) EnableRadii();
    }

    void Cloud::Clear()
    {
        // Wipe all point data but keep the property slots alive.
        m_Vertices.Clear();
        m_DeletedVertices = 0;
        EnsureProperties();
    }

    VertexHandle Cloud::AddPoint(glm::vec3 position)
    {
        m_Vertices.PushBack();
        const VertexHandle p = Handle(m_Vertices.Size() - 1u);
        m_PPoint[p] = position;
        return p;
    }

    void Cloud::DeletePoint(VertexHandle p)
    {
        if (p.IsValid() && p.Index < m_Vertices.Size() && !m_VDeleted[p])
        {
            m_VDeleted[p] = true;
            ++m_DeletedVertices;
        }
    }

    void Cloud::GarbageCollection()
    {
        if (!HasGarbage()) return;

        auto nv = VerticesSize();

        assert(nv <= std::numeric_limits<PropertyIndex>::max());

        auto vmap = Geometry::VertexProperty<VertexHandle>(m_Vertices.Add<VertexHandle>("v:garbage-collection", {}));

        for (std::size_t i = 0; i < nv; ++i)
            vmap[VertexHandle{static_cast<PropertyIndex>(i)}] = VertexHandle{
                static_cast<PropertyIndex>(i)
            };

        auto swap_vertex_slots = [&](std::size_t a, std::size_t b)
        {
            m_Vertices.Swap(a, b);
            using std::swap;
            swap(vmap[VertexHandle{static_cast<PropertyIndex>(a)}], vmap[VertexHandle{static_cast<PropertyIndex>(b)}]);
        };
        if (nv > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = nv - 1;
            while (true)
            {
                while (!m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;
                swap_vertex_slots(i0, i1);
            }
            nv = m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
        }

        m_Vertices.Remove(vmap);

        m_Vertices.Resize(nv);
        m_Vertices.Shrink_to_fit();

        m_DeletedVertices = 0;
        EnsureProperties();
    }

    void Cloud::EnableNormals(glm::vec3 defaultNormal)
    {
        if (!m_PNormal.IsValid())
        {
            m_PNormal = VertexProperty<glm::vec3>(
                m_Vertices.GetOrAdd<glm::vec3>("p:normal", defaultNormal));
        }
    }

    void Cloud::EnableColors(glm::vec4 defaultColor)
    {
        if (!m_PColor.IsValid())
        {
            m_PColor = VertexProperty<glm::vec4>(
                m_Vertices.GetOrAdd<glm::vec4>("p:color", defaultColor));
        }
    }

    void Cloud::EnableRadii(float defaultRadius)
    {
        if (!m_PRadius.IsValid())
        {
            m_PRadius = VertexProperty<float>(
                m_Vertices.GetOrAdd<float>("p:radius", defaultRadius));
        }
    }

    bool Cloud::IsValid() const noexcept
    {
        // Empty cloud is valid.
        if (IsEmpty()) return true;
        // Optional properties must exactly cover all points when present.
        if (HasNormals() && m_PNormal.Span().size() != VerticesSize()) return false;
        if (HasColors() && m_PColor.Span().size() != VerticesSize()) return false;
        if (HasRadii() && m_PRadius.Span().size() != VerticesSize()) return false;
        return true;
    }

    // =========================================================================
    // ComputeBoundingBox
    // =========================================================================
}
