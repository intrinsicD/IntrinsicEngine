module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <glm/glm.hpp>

module Geometry.Raycast;

import Geometry.Primitives;
import Geometry.Validation;
import Geometry.IntersectionClassification;
import Geometry.RobustPredicates;

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

    // -----------------------------------------------------------------------
    // GEOM-007 Slice 3 — classifying companion. Reuses the watertight kernel
    // for bit-exact parity on the geometric fields, then folds the result
    // into `Intersection::RayTriangleResult` with shared diagnostics.
    // -----------------------------------------------------------------------
    Intersection::RayTriangleResult
    RayTriangle_Classify(const Ray& ray, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                         float tMin, float tMax)
    {
        namespace IX = Intersection;
        namespace RP = RobustPredicates;

        IX::RayTriangleResult result{};

        if (!Validation::IsValid(ray))
        {
            result.Kind = IX::Kind::DegenerateInput;
            return result;
        }

        const glm::vec3 e0 = b - a;
        const glm::vec3 e1 = c - a;
        const glm::vec3 n = glm::cross(e0, e1);
        if (!(glm::dot(n, n) > 1e-20f))
        {
            result.Kind = IX::Kind::DegenerateInput;
            return result;
        }

        const auto hit = RayTriangle_Watertight(ray, a, b, c, tMin, tMax);
        if (!hit)
        {
            result.Kind = IX::Kind::None;
            return result;
        }

        const double wa = static_cast<double>(hit->U);
        const double wb = static_cast<double>(hit->V);
        const double wc = 1.0 - wa - wb;

        result.RayParam = static_cast<double>(hit->T);
        result.WA = wa;
        result.WB = wb;
        result.WC = wc;
        result.Point = ray.Origin + hit->T * ray.Direction;

        // Boundary classification on the barycentric weights. The watertight
        // kernel returns single-precision weights, so we use a looser relative
        // tolerance than the predicate module's default to avoid missing
        // genuine vertex/edge hits.
        const double weightScale = std::max({1.0,
                                             std::fabs(wa),
                                             std::fabs(wb),
                                             std::fabs(wc)});
        const double weightEps = RP::ScaledEpsilon(weightScale, 1.0e-6);

        const bool aZero = std::fabs(wa) <= weightEps;
        const bool bZero = std::fabs(wb) <= weightEps;
        const bool cZero = std::fabs(wc) <= weightEps;
        const int zeroCount = (aZero ? 1 : 0) + (bZero ? 1 : 0) + (cZero ? 1 : 0);

        if (zeroCount >= 2)
        {
            result.Kind = IX::Kind::Touching;
            if (!aZero) result.OnTriangle = IX::TriangleFeature::VertexA;
            else if (!bZero) result.OnTriangle = IX::TriangleFeature::VertexB;
            else result.OnTriangle = IX::TriangleFeature::VertexC;
        }
        else if (zeroCount == 1)
        {
            result.Kind = IX::Kind::Touching;
            if (aZero) result.OnTriangle = IX::TriangleFeature::EdgeBC; // opposite A
            else if (bZero) result.OnTriangle = IX::TriangleFeature::EdgeCA; // opposite B
            else result.OnTriangle = IX::TriangleFeature::EdgeAB; // opposite C
        }
        else
        {
            result.Kind = IX::Kind::Proper;
            result.OnTriangle = IX::TriangleFeature::Interior;
        }

        // Ray feature: at origin when the parameter is within a scale-aware
        // epsilon of zero, otherwise interior of the ray.
        const double tAbs = std::fabs(result.RayParam);
        const double rayScale = std::max(1.0, tAbs);
        const double rayEps = RP::ScaledEpsilon(rayScale, 1.0e-6);
        result.OnRay = (tAbs <= rayEps) ? IX::RayFeature::Origin
                                        : IX::RayFeature::Interior;
        return result;
    }
}

