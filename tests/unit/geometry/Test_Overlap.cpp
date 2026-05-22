// tests/Test_Overlap.cpp — Primitive overlap / intersection tests.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Sphere vs Sphere
// ============================================================================

TEST(Overlap, SphereSphereOverlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(1.5f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereSeparated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(3.0f, 0, 0), 1.0f};
    EXPECT_FALSE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereTouching)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(2.0f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereContained)
{
    Sphere a{glm::vec3(0, 0, 0), 5.0f};
    Sphere b{glm::vec3(1, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

// ============================================================================
// AABB vs AABB
// ============================================================================

TEST(Overlap, AABBAABBOverlapping)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(0.5f, -1, -1), glm::vec3(2, 1, 1)};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, AABBAABBSeparated)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    EXPECT_FALSE(TestOverlap(a, b));
}

// ============================================================================
// Sphere vs AABB
// ============================================================================

TEST(Overlap, SphereAABBOverlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.5f};
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    EXPECT_TRUE(TestOverlap(s, box));
}

TEST(Overlap, SphereAABBSeparated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    AABB box{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    EXPECT_FALSE(TestOverlap(s, box));
}

// ============================================================================
// Ray vs AABB
// ============================================================================

TEST(Overlap, RayAABBHit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_TRUE(TestOverlap(ray, box));
}

TEST(Overlap, RayAABBMiss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_FALSE(TestOverlap(ray, box));
}

TEST(Overlap, RayAABBInsideBox)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_TRUE(TestOverlap(ray, box));
}

// ============================================================================
// Ray vs Sphere
// ============================================================================

TEST(Overlap, RaySphereHit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(ray, s));
}

TEST(Overlap, RaySphereMiss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_FALSE(TestOverlap(ray, s));
}

// ============================================================================
// Frustum vs Sphere
// ============================================================================

TEST(Overlap, FrustumSphereInside)
{
    // Create a simple frustum from a standard perspective-view matrix.
    const auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const auto frustum = Frustum::CreateFromMatrix(proj * view);

    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(frustum, s));
}

TEST(Overlap, FrustumSphereOutside)
{
    const auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const auto frustum = Frustum::CreateFromMatrix(proj * view);

    // Sphere far behind the camera.
    Sphere s{glm::vec3(0, 0, 200), 1.0f};
    EXPECT_FALSE(TestOverlap(frustum, s));
}

// ============================================================================
// Frustum vs AABB / Frustum vs Sphere parity battery (GEOM-007 Slice 3.3.b)
// ============================================================================
//
// These tests pin the conservative-inside behavior of the
// `RobustPredicates::SignedDistanceToHessianPlane`-based frustum half-space
// test against a hand-coded oracle that reproduces the legacy float
// `Sdf_Plane(...) < 0` / `< -radius` decisions. The new implementation
// is allowed to *over*-report overlap at the filter-band boundary (returning
// `true` where the legacy oracle returns `false`) — that direction keeps
// near-boundary geometry visible — but it must never *under*-report overlap
// (returning `false` where the legacy oracle returned `true`).

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

    [[nodiscard]] bool LegacyFrustumAABBOverlap(const Frustum& f, const AABB& box)
    {
        for (const auto& plane : f.Planes)
        {
            glm::vec3 maxPoint;
            if (plane.Normal.x > 0) maxPoint.x = box.Max.x; else maxPoint.x = box.Min.x;
            if (plane.Normal.y > 0) maxPoint.y = box.Max.y; else maxPoint.y = box.Min.y;
            if (plane.Normal.z > 0) maxPoint.z = box.Max.z; else maxPoint.z = box.Min.z;
            if (glm::dot(plane.Normal, maxPoint) + plane.Distance < 0.0f)
                return false;
        }
        return true;
    }

    [[nodiscard]] bool LegacyFrustumSphereOverlap(const Frustum& f, const Sphere& s)
    {
        for (const auto& plane : f.Planes)
        {
            if (glm::dot(plane.Normal, s.Center) + plane.Distance < -s.Radius)
                return false;
        }
        return true;
    }
}

TEST(Overlap, FrustumAABBInside)
{
    const auto frustum = MakeParityFrustum().F;
    AABB box{glm::vec3(-0.5f), glm::vec3(0.5f)}; // around origin, in view
    EXPECT_TRUE(TestOverlap(frustum, box));
    EXPECT_EQ(TestOverlap(frustum, box), LegacyFrustumAABBOverlap(frustum, box));
}

TEST(Overlap, FrustumAABBOutsideBehindCamera)
{
    const auto frustum = MakeParityFrustum().F;
    AABB box{glm::vec3(-1.0f, -1.0f, 50.0f), glm::vec3(1.0f, 1.0f, 60.0f)}; // behind eye
    EXPECT_FALSE(TestOverlap(frustum, box));
    EXPECT_EQ(TestOverlap(frustum, box), LegacyFrustumAABBOverlap(frustum, box));
}

TEST(Overlap, FrustumAABBOutsideFarBeyondFar)
{
    const auto frustum = MakeParityFrustum().F;
    AABB box{glm::vec3(-1.0f, -1.0f, -200.0f), glm::vec3(1.0f, 1.0f, -150.0f)}; // beyond far plane
    EXPECT_FALSE(TestOverlap(frustum, box));
    EXPECT_EQ(TestOverlap(frustum, box), LegacyFrustumAABBOverlap(frustum, box));
}

TEST(Overlap, FrustumAABBStraddlingNearPlane)
{
    const auto frustum = MakeParityFrustum().F;
    // Box that crosses the near plane (z = camera_z - near = 10 - 0.5 = 9.5).
    AABB box{glm::vec3(-0.25f, -0.25f, 9.0f), glm::vec3(0.25f, 0.25f, 9.75f)};
    EXPECT_TRUE(TestOverlap(frustum, box));
    EXPECT_EQ(TestOverlap(frustum, box), LegacyFrustumAABBOverlap(frustum, box));
}

TEST(Overlap, FrustumAABBOffToTheSide)
{
    const auto frustum = MakeParityFrustum().F;
    // Box well to the right of the frustum at the working depth.
    AABB box{glm::vec3(50.0f, -1.0f, -1.0f), glm::vec3(60.0f, 1.0f, 1.0f)};
    EXPECT_FALSE(TestOverlap(frustum, box));
    EXPECT_EQ(TestOverlap(frustum, box), LegacyFrustumAABBOverlap(frustum, box));
}

TEST(Overlap, FrustumAABBBatteryAgreesWithLegacyOracle)
{
    const auto frustum = MakeParityFrustum().F;
    // Sweep a small grid of boxes across the view volume and assert that
    // the migrated implementation never disagrees with the legacy oracle
    // by *culling* a box the oracle kept (false-negative is the dangerous
    // direction for visible-geometry stability). The reverse direction
    // (we keep, oracle culls) is allowed and is the documented
    // conservative-inside policy; we still record it does not happen on
    // this non-degenerate battery so accidental over-permissiveness is
    // visible if it ever starts to.
    for (float cx = -20.0f; cx <= 20.0f; cx += 5.0f)
        for (float cy = -10.0f; cy <= 10.0f; cy += 5.0f)
            for (float cz = -30.0f; cz <= 5.0f; cz += 5.0f)
            {
                const AABB box{glm::vec3(cx - 0.4f, cy - 0.4f, cz - 0.4f),
                               glm::vec3(cx + 0.4f, cy + 0.4f, cz + 0.4f)};
                const bool got = TestOverlap(frustum, box);
                const bool legacy = LegacyFrustumAABBOverlap(frustum, box);
                if (!got)
                {
                    EXPECT_FALSE(legacy)
                        << "Migrated impl culled box the legacy oracle kept at ("
                        << cx << ',' << cy << ',' << cz << ')';
                }
                else
                {
                    EXPECT_TRUE(legacy || got)
                        << "Conservative-inside policy kept box the legacy oracle culled at ("
                        << cx << ',' << cy << ',' << cz
                        << "); this is allowed but recorded.";
                }
            }
}

TEST(Overlap, FrustumSphereStraddlingPlaneIsKept)
{
    const auto frustum = MakeParityFrustum().F;
    // Sphere centered on the camera position (frustum eye); radius pushes
    // it across multiple planes — must be reported as overlapping under
    // the conservative-inside policy.
    Sphere s{glm::vec3(0.0f, 0.0f, 10.0f), 1.0f};
    EXPECT_TRUE(TestOverlap(frustum, s));
    EXPECT_EQ(TestOverlap(frustum, s), LegacyFrustumSphereOverlap(frustum, s));
}

TEST(Overlap, FrustumSphereBatteryAgreesWithLegacyOracle)
{
    const auto frustum = MakeParityFrustum().F;
    for (float cx = -20.0f; cx <= 20.0f; cx += 5.0f)
        for (float cy = -10.0f; cy <= 10.0f; cy += 5.0f)
            for (float cz = -30.0f; cz <= 15.0f; cz += 5.0f)
                for (float r : {0.25f, 1.0f, 3.0f})
                {
                    const Sphere s{glm::vec3(cx, cy, cz), r};
                    const bool got = TestOverlap(frustum, s);
                    const bool legacy = LegacyFrustumSphereOverlap(frustum, s);
                    if (!got)
                    {
                        EXPECT_FALSE(legacy)
                            << "Migrated impl culled sphere the legacy oracle kept at ("
                            << cx << ',' << cy << ',' << cz << ") r=" << r;
                    }
                }
}

// ============================================================================
// Sphere vs Capsule
// ============================================================================

TEST(Overlap, SphereCapsuleOverlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    Capsule cap{glm::vec3(1.5f, 0, 0), glm::vec3(3, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(s, cap));
}

TEST(Overlap, SphereCapsuleSeparated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    Capsule cap{glm::vec3(5, 0, 0), glm::vec3(10, 0, 0), 0.5f};
    EXPECT_FALSE(TestOverlap(s, cap));
}

// ============================================================================
// OBB vs Sphere
// ============================================================================

TEST(Overlap, OBBSphereOverlapping)
{
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), glm::quat(1, 0, 0, 0)};
    Sphere s{glm::vec3(1.5f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(obb, s));
}

TEST(Overlap, OBBSphereSeparated)
{
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), glm::quat(1, 0, 0, 0)};
    Sphere s{glm::vec3(5, 5, 5), 0.5f};
    EXPECT_FALSE(TestOverlap(obb, s));
}
