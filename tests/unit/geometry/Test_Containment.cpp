// tests/Test_Containment.cpp — Dedicated tests for Geometry::Contains (TODO D18).
//
// Validates analytic containment checks and fallback vertex-enumeration paths
// for various primitive pair combinations.
#include <gtest/gtest.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Sphere contains Sphere
// ============================================================================

TEST(Containment, SphereSphere_FullyContained)
{
    Sphere outer{glm::vec3(0, 0, 0), 5.0f};
    Sphere inner{glm::vec3(1, 0, 0), 1.0f};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PartialOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 2.0f};
    Sphere inner{glm::vec3(1.5f, 0, 0), 1.0f};
    // Inner extends beyond outer: 1.5 + 1.0 = 2.5 > 2.0
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_NoOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.0f};
    Sphere inner{glm::vec3(5, 0, 0), 1.0f};
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_Concentric)
{
    Sphere outer{glm::vec3(0, 0, 0), 3.0f};
    Sphere inner{glm::vec3(0, 0, 0), 2.0f};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PointContainment)
{
    Sphere outer{glm::vec3(0, 0, 0), 2.0f};
    Sphere inner{glm::vec3(1, 0, 0), 0.0f}; // Point (zero radius)
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PointOutside)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.0f};
    Sphere inner{glm::vec3(5, 0, 0), 0.0f};
    EXPECT_FALSE(Contains(outer, inner));
}

// ============================================================================
// AABB contains point
// ============================================================================

TEST(Containment, AABBPoint_Inside)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, glm::vec3(0, 0, 0)));
}

TEST(Containment, AABBPoint_OnBoundary)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, glm::vec3(1, 0, 0)));
}

TEST(Containment, AABBPoint_Outside)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_FALSE(Contains(box, glm::vec3(2, 0, 0)));
}

// ============================================================================
// AABB contains AABB
// ============================================================================

TEST(Containment, AABBAABB_FullyContained)
{
    AABB outer{glm::vec3(-5, -5, -5), glm::vec3(5, 5, 5)};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, AABBAABB_PartialOverlap)
{
    AABB outer{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    AABB inner{glm::vec3(0, 0, 0), glm::vec3(2, 2, 2)};
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, AABBAABB_Equal)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, box));
}

// ============================================================================
// Sphere contains AABB
// ============================================================================

TEST(Containment, SphereAABB_FullyContained)
{
    Sphere outer{glm::vec3(0, 0, 0), 5.0f};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    // Distance from center to corner = sqrt(3) ≈ 1.73 < 5.0
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereAABB_PartialOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.5f};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    // Corner distance sqrt(3) ≈ 1.73 > 1.5
    EXPECT_FALSE(Contains(outer, inner));
}

// ============================================================================
// Frustum contains AABB
// ============================================================================

TEST(Containment, FrustumAABB_Inside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    // Small box near the camera's look-at target
    AABB box{glm::vec3(-0.1f, -0.1f, -0.1f), glm::vec3(0.1f, 0.1f, 0.1f)};
    EXPECT_TRUE(Contains(frustum, box));
}

TEST(Containment, FrustumAABB_Outside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    // Box behind camera
    AABB box{glm::vec3(-1, -1, 50), glm::vec3(1, 1, 55)};
    EXPECT_FALSE(Contains(frustum, box));
}

// ============================================================================
// Frustum contains Sphere
// ============================================================================

TEST(Containment, FrustumSphere_Inside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    EXPECT_TRUE(Contains(frustum, s));
}

TEST(Containment, FrustumSphere_StraddlingPlane)
{
    auto proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    // Very large sphere centered at origin — will straddle planes
    Sphere s{glm::vec3(0, 0, 0), 100.0f};
    EXPECT_FALSE(Contains(frustum, s));
}

// ============================================================================
// Frustum contains AABB / Sphere parity battery (GEOM-007 Slice 3.3.c)
// ============================================================================
//
// These tests pin the conservative-EXCLUDE behavior of the
// `RobustPredicates::SignedDistanceToHessianPlane`-based frustum strict-
// containment test against a hand-coded oracle that reproduces the legacy
// float `Sdf_Plane(...) < 0` / `< radius` decisions. The new implementation
// is allowed to *under*-report containment at the filter-band boundary
// (returning `false` where the legacy oracle returned `true`) — that
// direction matches the documented "no false positives on near-plane
// primitives" policy — but it must never *over*-report containment
// (returning `true` where the legacy oracle returned `false`).

namespace
{
    struct ParityFrustum
    {
        Frustum F;
        glm::mat4 ViewProj;
    };

    [[nodiscard]] ParityFrustum MakeParityFrustum()
    {
        const auto proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 50.0f);
        const auto view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        return ParityFrustum{Frustum::CreateFromMatrix(proj * view), proj * view};
    }

    [[nodiscard]] bool LegacyFrustumContainsAABB(const Frustum& f, const AABB& box)
    {
        for (const auto& plane : f.Planes)
        {
            glm::vec3 negativeVertex = box.Max;
            if (plane.Normal.x >= 0) negativeVertex.x = box.Min.x;
            if (plane.Normal.y >= 0) negativeVertex.y = box.Min.y;
            if (plane.Normal.z >= 0) negativeVertex.z = box.Min.z;
            if (glm::dot(plane.Normal, negativeVertex) + plane.Distance < 0.0f)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool LegacyFrustumContainsSphere(const Frustum& f, const Sphere& s)
    {
        for (const auto& plane : f.Planes)
        {
            if (glm::dot(plane.Normal, s.Center) + plane.Distance < s.Radius)
                return false;
        }
        return true;
    }
}

TEST(Containment, FrustumAABB_DeepInsideMatchesLegacy)
{
    const auto frustum = MakeParityFrustum().F;
    // Tiny box safely inside the working volume.
    AABB box{glm::vec3(-0.25f), glm::vec3(0.25f)};
    EXPECT_TRUE(Contains(frustum, box));
    EXPECT_EQ(Contains(frustum, box), LegacyFrustumContainsAABB(frustum, box));
}

TEST(Containment, FrustumAABB_StraddlingNearPlaneIsNotContained)
{
    const auto frustum = MakeParityFrustum().F;
    // Near plane lives at z = 9.5 (eye at +10, near=0.5). Box straddles it.
    AABB box{glm::vec3(-0.25f, -0.25f, 9.0f), glm::vec3(0.25f, 0.25f, 9.75f)};
    EXPECT_FALSE(Contains(frustum, box));
    EXPECT_EQ(Contains(frustum, box), LegacyFrustumContainsAABB(frustum, box));
}

TEST(Containment, FrustumAABB_OutsideMatchesLegacy)
{
    const auto frustum = MakeParityFrustum().F;
    AABB box{glm::vec3(50.0f, -1.0f, -1.0f), glm::vec3(60.0f, 1.0f, 1.0f)};
    EXPECT_FALSE(Contains(frustum, box));
    EXPECT_EQ(Contains(frustum, box), LegacyFrustumContainsAABB(frustum, box));
}

TEST(Containment, FrustumAABB_BatteryNeverOverReportsContainment)
{
    const auto frustum = MakeParityFrustum().F;
    for (float cx = -10.0f; cx <= 10.0f; cx += 2.5f)
        for (float cy = -6.0f; cy <= 6.0f; cy += 2.5f)
            for (float cz = -20.0f; cz <= 5.0f; cz += 2.5f)
            {
                const AABB box{glm::vec3(cx - 0.4f, cy - 0.4f, cz - 0.4f),
                               glm::vec3(cx + 0.4f, cy + 0.4f, cz + 0.4f)};
                const bool got = Contains(frustum, box);
                const bool legacy = LegacyFrustumContainsAABB(frustum, box);
                if (got)
                {
                    // Conservative-exclude policy: if we say "contained",
                    // the legacy oracle must also say "contained".
                    EXPECT_TRUE(legacy)
                        << "Migrated impl reported containment the legacy oracle rejected at ("
                        << cx << ',' << cy << ',' << cz << ')';
                }
                // The reverse direction (we exclude, legacy includes) is the
                // documented near-boundary safety margin and is allowed.
            }
}

TEST(Containment, FrustumSphere_DeepInsideMatchesLegacy)
{
    const auto frustum = MakeParityFrustum().F;
    Sphere s{glm::vec3(0.0f, 0.0f, 0.0f), 0.25f};
    EXPECT_TRUE(Contains(frustum, s));
    EXPECT_EQ(Contains(frustum, s), LegacyFrustumContainsSphere(frustum, s));
}

TEST(Containment, FrustumSphere_StraddlingFarPlaneIsNotContained)
{
    const auto frustum = MakeParityFrustum().F;
    // Far plane at z = 10 - 50 = -40. Sphere centered slightly inside.
    Sphere s{glm::vec3(0.0f, 0.0f, -39.5f), 1.0f};
    EXPECT_FALSE(Contains(frustum, s));
    EXPECT_EQ(Contains(frustum, s), LegacyFrustumContainsSphere(frustum, s));
}

TEST(Containment, FrustumSphere_BatteryNeverOverReportsContainment)
{
    const auto frustum = MakeParityFrustum().F;
    for (float cx = -10.0f; cx <= 10.0f; cx += 2.5f)
        for (float cy = -6.0f; cy <= 6.0f; cy += 2.5f)
            for (float cz = -20.0f; cz <= 5.0f; cz += 2.5f)
                for (float r : {0.1f, 0.5f, 2.0f})
                {
                    const Sphere s{glm::vec3(cx, cy, cz), r};
                    const bool got = Contains(frustum, s);
                    const bool legacy = LegacyFrustumContainsSphere(frustum, s);
                    if (got)
                    {
                        EXPECT_TRUE(legacy)
                            << "Migrated impl reported sphere containment the legacy oracle rejected at ("
                            << cx << ',' << cy << ',' << cz << ") r=" << r;
                    }
                }
}

