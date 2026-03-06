module;

#include <array>
#include <glm/glm.hpp>

export module Geometry:Frustum;

import :AABB;
import :Plane;

export namespace Geometry
{
    struct Frustum
    {
        std::array<glm::vec3, 8> Corners{};
        std::array<Plane, 6> Planes{};

        [[nodiscard]] glm::vec3 GetCenter() const
        {
            glm::vec3 center(0.0f);
            for (const glm::vec3& corner : Corners)
            {
                center += corner;
            }
            return center / 8.0f;
        }

        static Frustum CreateFromMatrix(const glm::mat4& viewProj)
        {
            Frustum f;
            auto extract = [&](int i, float a, float b, float c, float d)
            {
                f.Planes[i] = Plane{{a, b, c}, d};
                f.Planes[i].Normalize();
            };

            const float* m = reinterpret_cast<const float*>(&viewProj);
            extract(0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
            extract(1, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
            extract(2, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
            extract(3, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
            extract(4, m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);
            extract(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);

            const glm::mat4 inv = glm::inverse(viewProj);
            const glm::vec4 ndc[8] = {
                {-1.0f, -1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, -1.0f, 1.0f},
                {1.0f, 1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, -1.0f, 1.0f},
                {-1.0f, -1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f, 1.0f},
                {1.0f, 1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f, 1.0f}
            };

            for (int i = 0; i < 8; ++i)
            {
                const glm::vec4 res = inv * ndc[i];
                f.Corners[i] = glm::vec3(res) / res.w;
            }
            return f;
        }
    };

    [[nodiscard]] bool Contains(const Frustum& frustum, const glm::vec3& point)
    {
        for (const Plane& plane : frustum.Planes)
        {
            if (glm::dot(plane.Normal, point) + plane.Distance < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] AABB ComputeAABB(const Frustum& frustum)
    {
        AABB bounds;
        for (const glm::vec3& corner : frustum.Corners)
        {
            bounds = Union(bounds, corner);
        }
        return bounds;
    }
}

