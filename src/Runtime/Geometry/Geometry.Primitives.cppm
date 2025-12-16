module;

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <vector>

export module Runtime.Geometry.Primitives;

export import Runtime.Geometry.AABB;
export import Runtime.Geometry.OBB;

export namespace Runtime::Geometry
{
    struct Sphere
    {
        glm::vec3 Center;
        float Radius;
    };

    struct Capsule
    {
        glm::vec3 PointA, PointB;
        float Radius;
    };

    struct Cylinder
    {
        glm::vec3 PointA; // Bottom Center
        glm::vec3 PointB; // Top Center
        float Radius;
    };

    struct Ellipsoid
    {
        glm::vec3 Center;
        glm::vec3 Radii; // Scaling factors (Rx, Ry, Rz)
        glm::quat Rotation;
    };

    struct Segment
    {
        glm::vec3 A, B;
    };

    struct Triangle
    {
        glm::vec3 A, B, C;
    };

    struct Plane
    {
        glm::vec3 Normal;
        float Distance;

        void Normalize()
        {
            const float len = glm::length(Normal);
            if (len < 1e-6f) {
                Normal = glm::vec3(0, 1, 0);
                Distance = 0.0f;
                return;
            }
            Normal /= len;
            Distance /= len;
        }
    };

    struct ConvexHull
    {
        // V-Rep (Vertex Representation) - Used for GJK Support Mapping
        std::vector<glm::vec3> Vertices;

        // H-Rep (Half-space Representation) - Used for SDF, SAT, Containment
        // Populated by AssetLoader/PhysicsCooker
        std::vector<Plane> Planes;
    };

    struct Frustum
    {
        std::array<glm::vec3, 8> Corners;
        std::array<Plane, 6> Planes;

        static Frustum CreateFromMatrix(const glm::mat4& viewProj)
        {
            Frustum f;
            // Gribb/Hartmann Plane Extraction
            auto extract = [&](int i, float a, float b, float c, float d)
            {
                f.Planes[i] = Plane{{a, b, c}, d};
                f.Planes[i].Normalize();
            };
            // Left, Right, Bottom, Top, Near, Far (Standard OpenGL/Vulkan layout)
            const float* m = (const float*)&viewProj;
            extract(0, m[3] + m[0], m[7] + m[4], m[11] + m[8], m[15] + m[12]);
            extract(1, m[3] - m[0], m[7] - m[4], m[11] - m[8], m[15] - m[12]);
            extract(2, m[3] + m[1], m[7] + m[5], m[11] + m[9], m[15] + m[13]);
            extract(3, m[3] - m[1], m[7] - m[5], m[11] - m[9], m[15] - m[13]);
            extract(4, m[3] + m[2], m[7] + m[6], m[11] + m[10], m[15] + m[14]);
            extract(5, m[3] - m[2], m[7] - m[6], m[11] - m[10], m[15] - m[14]);

            // Corners via Inverse
            glm::mat4 inv = glm::inverse(viewProj);
            glm::vec4 ndc[8] = {
                {-1, -1, -1, 1}, {1, -1, -1, 1}, {1, 1, -1, 1}, {-1, 1, -1, 1},
                {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1}
            };
            for (int i = 0; i < 8; ++i)
            {
                glm::vec4 res = inv * ndc[i];
                f.Corners[i] = glm::vec3(res) / res.w;
            }
            return f;
        }
    };

    struct Ray
    {
        glm::vec3 Origin;
        glm::vec3 Direction;
    };
}
