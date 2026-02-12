module;

#include <cmath>
#include <optional>
#include <glm/glm.hpp>

module Geometry:Raycast.Impl;

import :Raycast;
import :Primitives;
import :Validation;

namespace Geometry
{
    namespace
    {
        // Returns major axis permutation Kx, Ky, Kz for direction.
        // Kz = index of maximal abs component.
        struct Permute
        {
            int Kx = 0;
            int Ky = 1;
            int Kz = 2;
        };

        [[nodiscard]] inline Permute MajorAxisPermutation(const glm::vec3& d)
        {
            const glm::vec3 ad = glm::abs(d);
            int kz = 0;
            if (ad.y > ad.x) kz = 1;
            if (ad.z > ad[kz]) kz = 2;

            int kx = (kz + 1) % 3;
            int ky = (kx + 1) % 3;

            // Swap to preserve winding based on direction sign.
            if (d[kz] < 0.0f) std::swap(kx, ky);
            return {kx, ky, kz};
        }

        [[nodiscard]] inline float GetByIndex(const glm::vec3& v, int i)
        {
            return i == 0 ? v.x : (i == 1 ? v.y : v.z);
        }
    }

    std::optional<RayTriangleHit>
    RayTriangle_Watertight(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                           float tMin, float tMax)
    {
        if (!Validation::IsValid(ray)) return std::nullopt;

        // Reject degenerate triangles early.
        const glm::vec3 e0 = b - a;
        const glm::vec3 e1 = c - a;
        const glm::vec3 n = glm::cross(e0, e1);
        const float n2 = glm::dot(n, n);
        if (!(n2 > 1e-20f)) return std::nullopt;

        const Permute p = MajorAxisPermutation(ray.Direction);

        // Translate vertices relative to ray origin.
        glm::vec3 A = a - ray.Origin;
        glm::vec3 B = b - ray.Origin;
        glm::vec3 C = c - ray.Origin;

        // Permute.
        const auto permute = [&](glm::vec3 v)
        {
            return glm::vec3{GetByIndex(v, p.Kx), GetByIndex(v, p.Ky), GetByIndex(v, p.Kz)};
        };
        A = permute(A);
        B = permute(B);
        C = permute(C);

        glm::vec3 D = permute(ray.Direction);

        // Shear so ray points down +Z.
        const float Sx = -D.x / D.z;
        const float Sy = -D.y / D.z;
        const float Sz = 1.0f / D.z;

        A.x += Sx * A.z;
        A.y += Sy * A.z;
        B.x += Sx * B.z;
        B.y += Sy * B.z;
        C.x += Sx * C.z;
        C.y += Sy * C.z;

        // Compute edge function coefficients in 2D.
        float U = C.x * B.y - C.y * B.x;
        float V = A.x * C.y - A.y * C.x;
        float W = B.x * A.y - B.y * A.x;

        // If any are zero, recompute with double for robustness.
        if (U == 0.0f || V == 0.0f || W == 0.0f)
        {
            const auto Cx = static_cast<double>(C.x);
            const auto Cy = static_cast<double>(C.y);
            const auto Bx = static_cast<double>(B.x);
            const auto By = static_cast<double>(B.y);
            const auto Ax = static_cast<double>(A.x);
            const auto Ay = static_cast<double>(A.y);

            U = static_cast<float>(Cx * By - Cy * Bx);
            V = static_cast<float>(Ax * Cy - Ay * Cx);
            W = static_cast<float>(Bx * Ay - By * Ax);
        }

        // Same sign test (allows zero).
        const bool hasNeg = (U < 0.0f) || (V < 0.0f) || (W < 0.0f);
        const bool hasPos = (U > 0.0f) || (V > 0.0f) || (W > 0.0f);
        if (hasNeg && hasPos) return std::nullopt;

        const float det = U + V + W;
        if (det == 0.0f) return std::nullopt;

        // Compute scaled z distances.
        A.z *= Sz;
        B.z *= Sz;
        C.z *= Sz;
        const float Tscaled = U * A.z + V * B.z + W * C.z;

        // Hit distance along ray.
        const float t = Tscaled / det;
        if (!(t >= tMin && t <= tMax)) return std::nullopt;

        RayTriangleHit hit;
        hit.T = t;
        hit.U = U / det;
        hit.V = V / det;
        return hit;
    }
}

