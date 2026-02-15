module;

#include <cstdint>
#include <cmath>
#include <vector>
#include <span>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

module Graphics:DebugDraw.Impl;

import :DebugDraw;

namespace Graphics
{
    // =========================================================================
    // Depth-tested primitives
    // =========================================================================

    void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, uint32_t color)
    {
        m_Lines.push_back({from, color, to, color});
    }

    void DebugDraw::Line(const glm::vec3& from, const glm::vec3& to, uint32_t colorStart, uint32_t colorEnd)
    {
        m_Lines.push_back({from, colorStart, to, colorEnd});
    }

    void DebugDraw::Box(const glm::vec3& min, const glm::vec3& max, uint32_t color)
    {
        BoxImpl(m_Lines, min, max, color);
    }

    void DebugDraw::WireBox(const glm::mat4& transform, const glm::vec3& halfExtents, uint32_t color)
    {
        // Compute 8 corners of the OBB
        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 local{
                (i & 1) ? halfExtents.x : -halfExtents.x,
                (i & 2) ? halfExtents.y : -halfExtents.y,
                (i & 4) ? halfExtents.z : -halfExtents.z
            };
            corners[i] = glm::vec3(transform * glm::vec4(local, 1.0f));
        }

        // 12 edges of the box (bit-difference edges of the 3-bit corner index)
        // Edges along X (bit 0 differs): 0-1, 2-3, 4-5, 6-7
        m_Lines.push_back({corners[0], color, corners[1], color});
        m_Lines.push_back({corners[2], color, corners[3], color});
        m_Lines.push_back({corners[4], color, corners[5], color});
        m_Lines.push_back({corners[6], color, corners[7], color});

        // Edges along Y (bit 1 differs): 0-2, 1-3, 4-6, 5-7
        m_Lines.push_back({corners[0], color, corners[2], color});
        m_Lines.push_back({corners[1], color, corners[3], color});
        m_Lines.push_back({corners[4], color, corners[6], color});
        m_Lines.push_back({corners[5], color, corners[7], color});

        // Edges along Z (bit 2 differs): 0-4, 1-5, 2-6, 3-7
        m_Lines.push_back({corners[0], color, corners[4], color});
        m_Lines.push_back({corners[1], color, corners[5], color});
        m_Lines.push_back({corners[2], color, corners[6], color});
        m_Lines.push_back({corners[3], color, corners[7], color});
    }

    void DebugDraw::Sphere(const glm::vec3& center, float radius, uint32_t color, uint32_t segments)
    {
        SphereImpl(m_Lines, center, radius, color, segments);
    }

    void DebugDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius,
                           uint32_t color, uint32_t segments)
    {
        if (segments < 3) segments = 3;

        // Build an orthonormal basis from the normal
        glm::vec3 n = glm::normalize(normal);
        glm::vec3 up = (std::abs(n.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 u = glm::normalize(glm::cross(n, up));
        glm::vec3 v = glm::cross(n, u);

        const float step = glm::two_pi<float>() / static_cast<float>(segments);
        glm::vec3 prev = center + u * radius;

        for (uint32_t i = 1; i <= segments; ++i)
        {
            float angle = step * static_cast<float>(i);
            float cs = std::cos(angle);
            float sn = std::sin(angle);
            glm::vec3 curr = center + (u * cs + v * sn) * radius;
            m_Lines.push_back({prev, color, curr, color});
            prev = curr;
        }
    }

    void DebugDraw::Arrow(const glm::vec3& from, const glm::vec3& to, float headSize, uint32_t color)
    {
        glm::vec3 dir = to - from;
        float len = glm::length(dir);
        if (len < 1e-6f) return;
        dir /= len;

        // Shaft
        m_Lines.push_back({from, color, to, color});

        // Arrowhead: two lines forming a V
        glm::vec3 up = (std::abs(dir.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 right = glm::normalize(glm::cross(dir, up));
        glm::vec3 upDir = glm::cross(right, dir);

        glm::vec3 headBase = to - dir * headSize;
        float headWidth = headSize * 0.4f;

        m_Lines.push_back({to, color, headBase + right * headWidth, color});
        m_Lines.push_back({to, color, headBase - right * headWidth, color});
        m_Lines.push_back({to, color, headBase + upDir * headWidth, color});
        m_Lines.push_back({to, color, headBase - upDir * headWidth, color});
    }

    void DebugDraw::Axes(const glm::vec3& origin, float size)
    {
        AxesImpl(m_Lines, origin, size);
    }

    void DebugDraw::Axes(const glm::mat4& transform, float size)
    {
        glm::vec3 origin = glm::vec3(transform[3]);
        glm::vec3 x = glm::vec3(transform[0]) * size;
        glm::vec3 y = glm::vec3(transform[1]) * size;
        glm::vec3 z = glm::vec3(transform[2]) * size;

        m_Lines.push_back({origin, Red(),   origin + x, Red()});
        m_Lines.push_back({origin, Green(), origin + y, Green()});
        m_Lines.push_back({origin, Blue(),  origin + z, Blue()});
    }

    void DebugDraw::Frustum(const glm::mat4& invViewProj, uint32_t color)
    {
        // 8 NDC corners of a clip cube [-1,1]^3 (with Vulkan depth [0,1])
        glm::vec4 ndcCorners[8] = {
            {-1, -1, 0, 1}, { 1, -1, 0, 1}, { 1,  1, 0, 1}, {-1,  1, 0, 1}, // near
            {-1, -1, 1, 1}, { 1, -1, 1, 1}, { 1,  1, 1, 1}, {-1,  1, 1, 1}  // far
        };

        glm::vec3 corners[8];
        for (int i = 0; i < 8; ++i)
        {
            glm::vec4 world = invViewProj * ndcCorners[i];
            corners[i] = glm::vec3(world) / world.w;
        }

        // Near plane
        m_Lines.push_back({corners[0], color, corners[1], color});
        m_Lines.push_back({corners[1], color, corners[2], color});
        m_Lines.push_back({corners[2], color, corners[3], color});
        m_Lines.push_back({corners[3], color, corners[0], color});

        // Far plane
        m_Lines.push_back({corners[4], color, corners[5], color});
        m_Lines.push_back({corners[5], color, corners[6], color});
        m_Lines.push_back({corners[6], color, corners[7], color});
        m_Lines.push_back({corners[7], color, corners[4], color});

        // Connecting edges
        m_Lines.push_back({corners[0], color, corners[4], color});
        m_Lines.push_back({corners[1], color, corners[5], color});
        m_Lines.push_back({corners[2], color, corners[6], color});
        m_Lines.push_back({corners[3], color, corners[7], color});
    }

    void DebugDraw::Grid(const glm::vec3& origin, const glm::vec3& axisU, const glm::vec3& axisV,
                         int countU, int countV, float spacing, uint32_t color)
    {
        float halfU = static_cast<float>(countU) * spacing * 0.5f;
        float halfV = static_cast<float>(countV) * spacing * 0.5f;
        glm::vec3 u = glm::normalize(axisU);
        glm::vec3 v = glm::normalize(axisV);

        // Lines along V direction (varying U position)
        for (int i = 0; i <= countU; ++i)
        {
            float t = static_cast<float>(i) * spacing - halfU;
            glm::vec3 a = origin + u * t - v * halfV;
            glm::vec3 b = origin + u * t + v * halfV;
            m_Lines.push_back({a, color, b, color});
        }

        // Lines along U direction (varying V position)
        for (int j = 0; j <= countV; ++j)
        {
            float t = static_cast<float>(j) * spacing - halfV;
            glm::vec3 a = origin + v * t - u * halfU;
            glm::vec3 b = origin + v * t + u * halfU;
            m_Lines.push_back({a, color, b, color});
        }
    }

    void DebugDraw::Cross(const glm::vec3& center, float size, uint32_t color)
    {
        float half = size * 0.5f;
        m_Lines.push_back({center - glm::vec3(half, 0, 0), color, center + glm::vec3(half, 0, 0), color});
        m_Lines.push_back({center - glm::vec3(0, half, 0), color, center + glm::vec3(0, half, 0), color});
        m_Lines.push_back({center - glm::vec3(0, 0, half), color, center + glm::vec3(0, 0, half), color});
    }

    // =========================================================================
    // Overlay primitives (no depth test)
    // =========================================================================

    void DebugDraw::OverlayLine(const glm::vec3& from, const glm::vec3& to, uint32_t color)
    {
        m_OverlayLines.push_back({from, color, to, color});
    }

    void DebugDraw::OverlayLine(const glm::vec3& from, const glm::vec3& to, uint32_t colorStart, uint32_t colorEnd)
    {
        m_OverlayLines.push_back({from, colorStart, to, colorEnd});
    }

    void DebugDraw::OverlayBox(const glm::vec3& min, const glm::vec3& max, uint32_t color)
    {
        BoxImpl(m_OverlayLines, min, max, color);
    }

    void DebugDraw::OverlaySphere(const glm::vec3& center, float radius, uint32_t color, uint32_t segments)
    {
        SphereImpl(m_OverlayLines, center, radius, color, segments);
    }

    void DebugDraw::OverlayAxes(const glm::vec3& origin, float size)
    {
        AxesImpl(m_OverlayLines, origin, size);
    }

    // =========================================================================
    // Frame Lifecycle
    // =========================================================================

    void DebugDraw::Reset()
    {
        m_Lines.clear();
        m_OverlayLines.clear();
    }

    std::span<const DebugDraw::LineSegment> DebugDraw::GetLines() const
    {
        return m_Lines;
    }

    std::span<const DebugDraw::LineSegment> DebugDraw::GetOverlayLines() const
    {
        return m_OverlayLines;
    }

    // =========================================================================
    // Shared Implementation
    // =========================================================================

    void DebugDraw::BoxImpl(std::vector<LineSegment>& target,
                            const glm::vec3& min, const glm::vec3& max, uint32_t color)
    {
        // 8 corners
        glm::vec3 c[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z},
            {max.x, max.y, min.z}, {min.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z},
            {max.x, max.y, max.z}, {min.x, max.y, max.z}
        };

        // Bottom face
        target.push_back({c[0], color, c[1], color});
        target.push_back({c[1], color, c[2], color});
        target.push_back({c[2], color, c[3], color});
        target.push_back({c[3], color, c[0], color});

        // Top face
        target.push_back({c[4], color, c[5], color});
        target.push_back({c[5], color, c[6], color});
        target.push_back({c[6], color, c[7], color});
        target.push_back({c[7], color, c[4], color});

        // Vertical edges
        target.push_back({c[0], color, c[4], color});
        target.push_back({c[1], color, c[5], color});
        target.push_back({c[2], color, c[6], color});
        target.push_back({c[3], color, c[7], color});
    }

    void DebugDraw::SphereImpl(std::vector<LineSegment>& target,
                               const glm::vec3& center, float radius,
                               uint32_t color, uint32_t segments)
    {
        if (segments < 4) segments = 4;

        const float step = glm::two_pi<float>() / static_cast<float>(segments);

        // XY plane (Z-axis great circle)
        {
            glm::vec3 prev = center + glm::vec3(radius, 0, 0);
            for (uint32_t i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 curr = center + glm::vec3(std::cos(angle), std::sin(angle), 0.0f) * radius;
                target.push_back({prev, color, curr, color});
                prev = curr;
            }
        }

        // XZ plane (Y-axis great circle)
        {
            glm::vec3 prev = center + glm::vec3(radius, 0, 0);
            for (uint32_t i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 curr = center + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
                target.push_back({prev, color, curr, color});
                prev = curr;
            }
        }

        // YZ plane (X-axis great circle)
        {
            glm::vec3 prev = center + glm::vec3(0, radius, 0);
            for (uint32_t i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 curr = center + glm::vec3(0.0f, std::cos(angle), std::sin(angle)) * radius;
                target.push_back({prev, color, curr, color});
                prev = curr;
            }
        }
    }

    void DebugDraw::AxesImpl(std::vector<LineSegment>& target,
                             const glm::vec3& origin, float size)
    {
        target.push_back({origin, Red(),   origin + glm::vec3(size, 0, 0), Red()});
        target.push_back({origin, Green(), origin + glm::vec3(0, size, 0), Green()});
        target.push_back({origin, Blue(),  origin + glm::vec3(0, 0, size), Blue()});
    }
}
