module;

#include <algorithm>

module Geometry.PointCloudView;

namespace Geometry::PointCloud
{
    Cloud::Cloud()
    {
        // Allocate the mandatory position property up-front.
        m_PPoint = VertexProperty<glm::vec3>(m_Points.Add<glm::vec3>("p:position", glm::vec3(0.f)));
    }

    void Cloud::Clear()
    {
        // Wipe all point data but keep the property slots alive.
        m_Points.Clear();
        // Re-initialise so the registry size resets to 0.
        *this = Cloud();
    }

    VertexHandle Cloud::AddPoint(glm::vec3 position)
    {
        m_Points.PushBack();
        const VertexHandle p = Handle(m_Points.Size() - 1u);
        m_PPoint[p] = position;
        return p;
    }

    void Cloud::EnableNormals(glm::vec3 defaultNormal)
    {
        if (!m_PNormal.IsValid())
        {
            m_PNormal = VertexProperty<glm::vec3>(
                m_Points.GetOrAdd<glm::vec3>("p:normal", defaultNormal));
        }
    }

    void Cloud::EnableColors(glm::vec4 defaultColor)
    {
        if (!m_PColor.IsValid())
        {
            m_PColor = VertexProperty<glm::vec4>(
                m_Points.GetOrAdd<glm::vec4>("p:color", defaultColor));
        }
    }

    void Cloud::EnableRadii(float defaultRadius)
    {
        if (!m_PRadius.IsValid())
        {
            m_PRadius = VertexProperty<float>(
                m_Points.GetOrAdd<float>("p:radius", defaultRadius));
        }
    }

    bool Cloud::IsValid() const noexcept
    {
        // Empty cloud is valid.
        if (IsEmpty()) return true;
        // Optional properties must exactly cover all points when present.
        if (HasNormals() && m_PNormal.Span().size() != PointCount()) return false;
        if (HasColors()  && m_PColor.Span().size()  != PointCount()) return false;
        if (HasRadii()   && m_PRadius.Span().size() != PointCount()) return false;
        return true;
    }

    // =========================================================================
    // ComputeBoundingBox
    // =========================================================================

}
