// tests/Test_RuntimeGeometry_Containment.cpp
// Comprehensive containment tests for all primitive pairs
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
 
import Runtime.Geometry.Primitives;
import Runtime.Geometry.Containment;
 
using namespace Runtime::Geometry;
 
// =========================================================================
// TEST HELPERS
// =========================================================================
 
inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}
 
// =========================================================================
// AABB CONTAINMENT
// =========================================================================
 
TEST(GeometryContainment, AABB_ContainsPoint_Inside)
{
    AABB box{{0, 0, 0}, {10, 10, 10}};
 
    EXPECT_TRUE(Contains(box, glm::vec3(5, 5, 5)));
    EXPECT_TRUE(Contains(box, glm::vec3(0, 0, 0))); // Boundary inclusive
    EXPECT_TRUE(Contains(box, glm::vec3(10, 10, 10))); // Max boundary
}
 
TEST(GeometryContainment, AABB_ContainsPoint_Outside)
{
    AABB box{{0, 0, 0}, {10, 10, 10}};
 
    EXPECT_FALSE(Contains(box, glm::vec3(-1, 5, 5)));
    EXPECT_FALSE(Contains(box, glm::vec3(5, 11, 5)));
    EXPECT_FALSE(Contains(box, glm::vec3(5, 5, -0.1f)));
}
 
TEST(GeometryContainment, AABB_ContainsAABB_FullyInside)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB inner{{2, 2, 2}, {8, 8, 8}};
 
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, AABB_ContainsAABB_Crossing)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB crossing{{8, 8, 8}, {12, 12, 12}}; // Extends outside
 
    EXPECT_FALSE(Contains(outer, crossing));
}
 
TEST(GeometryContainment, AABB_ContainsAABB_Identical)
{
    AABB box{{0, 0, 0}, {10, 10, 10}};
 
    // A box contains itself
    EXPECT_TRUE(Contains(box, box));
}
 
TEST(GeometryContainment, AABB_ContainsAABB_TouchingBoundary)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB inner{{0, 0, 0}, {10, 10, 10}}; // Same box
 
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, AABB_ContainsAABB_PartiallyOutside)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB partial{{-1, 0, 0}, {5, 5, 5}}; // Extends to x=-1
 
    EXPECT_FALSE(Contains(outer, partial));
}
 
// =========================================================================
// SPHERE CONTAINMENT
// =========================================================================
 
TEST(GeometryContainment, Sphere_ContainsSphere_FullyInside)
{
    Sphere outer{{0, 0, 0}, 10.0f};
    Sphere inner{{2, 0, 0}, 1.0f};
 
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, Sphere_ContainsSphere_Intersecting)
{
    Sphere outer{{0, 0, 0}, 10.0f};
    Sphere intersect{{9, 0, 0}, 2.0f}; // Extends to 11.0, outside outer
 
    EXPECT_FALSE(Contains(outer, intersect));
}
 
TEST(GeometryContainment, Sphere_ContainsSphere_Touching)
{
    Sphere outer{{0, 0, 0}, 2.0f};
    Sphere inner{{0, 0, 0}, 2.0f}; // Same size
 
    // Should NOT contain (they're equal, not one inside other)
    EXPECT_FALSE(Contains(outer, inner));
}
 
TEST(GeometryContainment, Sphere_ContainsSphere_Concentric)
{
    Sphere outer{{0, 0, 0}, 5.0f};
    Sphere inner{{0, 0, 0}, 3.0f}; // Same center, smaller radius
 
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, Sphere_ContainsAABB_FullyInside)
{
    Sphere s{{0, 0, 0}, 2.0f};
    AABB box{{-1, -1, -1}, {1, 1, 1}}; // Diagonal length is sqrt(3) ~ 1.73 < 2.0
 
    EXPECT_TRUE(Contains(s, box));
}
 
TEST(GeometryContainment, Sphere_ContainsAABB_Outside)
{
    Sphere s{{0, 0, 0}, 2.0f};
    AABB bigBox{{-1.5f, -1.5f, -1.5f}, {1.5f, 1.5f, 1.5f}}; // Diagonal > 2.0
 
    EXPECT_FALSE(Contains(s, bigBox));
}
 
TEST(GeometryContainment, Sphere_ContainsAABB_CornerTouching)
{
    Sphere s{{0, 0, 0}, glm::sqrt(3.0f)}; // Radius exactly diagonal length
    AABB box{{-1, -1, -1}, {1, 1, 1}};
 
    // Corners are exactly on the sphere surface - inclusive containment
    EXPECT_TRUE(Contains(s, box));
}
 
// =========================================================================
// FRUSTUM CONTAINMENT
// =========================================================================
 
TEST(GeometryContainment, Frustum_ContainsAABB_FullyInside)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);
 
    // Small box at origin (fully visible)
    AABB smallBox{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
 
    EXPECT_TRUE(Contains(frustum, smallBox));
}
 
TEST(GeometryContainment, Frustum_ContainsAABB_PartiallyOutside)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);
 
    // Large box that extends outside frustum
    AABB largeBox{{-10, -10, -10}, {10, 10, 10}};
 
    EXPECT_FALSE(Contains(frustum, largeBox));
}
 
TEST(GeometryContainment, Frustum_ContainsAABB_BehindCamera)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);
 
    // Box behind camera
    AABB behindBox{{-1, -1, 6}, {1, 1, 8}};
 
    EXPECT_FALSE(Contains(frustum, behindBox));
}
 
TEST(GeometryContainment, Frustum_ContainsSphere_FullyInside)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);
 
    // Small sphere at origin (fully inside)
    Sphere sIn{{0, 0, 0}, 0.5f};
 
    EXPECT_TRUE(Contains(f, sIn));
}
 
TEST(GeometryContainment, Frustum_ContainsSphere_PartiallyOutside)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);
 
    // Large sphere that extends outside
    Sphere sLarge{{0, 0, 0}, 10.0f};
 
    EXPECT_FALSE(Contains(f, sLarge));
}
 
TEST(GeometryContainment, Frustum_ContainsSphere_AtNearPlane)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);
 
    // Sphere touching near plane
    Sphere sNear{{0, 0, -0.15f}, 0.03f};
 
    // Should be inside (not clipped by near plane)
    EXPECT_TRUE(Contains(f, sNear));
}
 
// =========================================================================
// EDGE CASES
// =========================================================================
 
TEST(GeometryContainment, PointOnBoundary_AABB)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    glm::vec3 point{1, 1, 1}; // On corner
 
    // Inclusive containment
    EXPECT_TRUE(Contains(box, point));
}
 
TEST(GeometryContainment, PointOnBoundary_AABBEdge)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    glm::vec3 pointOnEdge{1, 0.5f, 0};
 
    EXPECT_TRUE(Contains(box, pointOnEdge));
}
 
TEST(GeometryContainment, PointOnBoundary_AABBFace)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    glm::vec3 pointOnFace{0.5f, 0.5f, 1};
 
    EXPECT_TRUE(Contains(box, pointOnFace));
}
 
TEST(GeometryContainment, DegenerateAABB_Point)
{
    AABB point{{1, 1, 1}, {1, 1, 1}}; // Zero volume
    AABB box{{0, 0, 0}, {2, 2, 2}};
 
    // Point AABB is inside larger AABB
    EXPECT_TRUE(Contains(box, point));
}
 
TEST(GeometryContainment, DegenerateAABB_Line)
{
    AABB line{{0, 0, 0}, {1, 0, 0}}; // Line along X
    AABB box{{-1, -1, -1}, {2, 1, 1}};
 
    // Line is inside box
    EXPECT_TRUE(Contains(box, line));
}
 
TEST(GeometryContainment, ZeroRadiusSphere)
{
    Sphere outer{{0, 0, 0}, 5.0f};
    Sphere point{{1, 1, 1}, 0.0f}; // Point sphere
 
    // Point is inside
    EXPECT_TRUE(Contains(outer, point));
}
 
TEST(GeometryContainment, ConcentricSpheres_Equal)
{
    Sphere s1{{0, 0, 0}, 5.0f};
    Sphere s2{{0, 0, 0}, 5.0f}; // Same sphere
 
    // Equal spheres: one doesn't strictly contain the other
    EXPECT_FALSE(Contains(s1, s2));
}
 
// =========================================================================
// LARGE VALUES
// =========================================================================
 
TEST(GeometryContainment, LargeCoordinates)
{
    AABB outer{{1e6f, 1e6f, 1e6f}, {1e6f + 10, 1e6f + 10, 1e6f + 10}};
    AABB inner{{1e6f + 2, 1e6f + 2, 1e6f + 2}, {1e6f + 8, 1e6f + 8, 1e6f + 8}};
 
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, VerySmallBox)
{
    AABB outer{{0, 0, 0}, {1, 1, 1}};
    AABB tiny{{0.5f, 0.5f, 0.5f}, {0.5f + 1e-5f, 0.5f + 1e-5f, 0.5f + 1e-5f}};
 
    EXPECT_TRUE(Contains(outer, tiny));
}
 
// =========================================================================
// CONTAINMENT VS OVERLAP DISTINCTION
// =========================================================================
 
TEST(GeometryContainment, ContainmentStricterThanOverlap)
{
    // Two AABBs that overlap but neither contains the other
    AABB a{{0, 0, 0}, {5, 5, 5}};
    AABB b{{3, 3, 3}, {8, 8, 8}};
 
    // They overlap
    // (tested in Overlap tests - not duplicating here)
 
    // But neither contains the other
    EXPECT_FALSE(Contains(a, b));
    EXPECT_FALSE(Contains(b, a));
}
 
TEST(GeometryContainment, FullContainmentImpliesOverlap)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB inner{{2, 2, 2}, {8, 8, 8}};
 
    // If outer contains inner, they must overlap
    EXPECT_TRUE(Contains(outer, inner));
    // (overlap would also be true - not testing here to avoid duplication)
}
 
// =========================================================================
// SPECIAL CASES
// =========================================================================
 
TEST(GeometryContainment, AABB_ContainsAABB_SingleDimensionEqual)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB inner{{0, 2, 2}, {10, 8, 8}}; // X dimension matches exactly
 
    // Inner is flush with outer on X, but contained on Y and Z
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, Sphere_ContainsSphere_CenterAtBoundary)
{
    Sphere outer{{0, 0, 0}, 10.0f};
    Sphere inner{{10, 0, 0}, 0.5f}; // Center at boundary of outer
 
    // Inner sphere center is exactly on outer boundary
    // Inner sphere extends to 10.5, which is outside
    EXPECT_FALSE(Contains(outer, inner));
 
    // If inner radius is 0 (point), it should be contained
    inner.Radius = 0.0f;
    EXPECT_TRUE(Contains(outer, inner));
}
 
TEST(GeometryContainment, Sphere_ContainsAABB_SingleCornerTouching)
{
    Sphere s{{0, 0, 0}, 1.0f};
    AABB box{{0.5f, 0.5f, 0.5f}, {0.9f, 0.9f, 0.9f}};
 
    // Check if all corners are inside sphere
    float maxDist = glm::length(glm::vec3(0.9f, 0.9f, 0.9f));
 
    if (maxDist <= 1.0f)
    {
        EXPECT_TRUE(Contains(s, box));
    }
    else
    {
        EXPECT_FALSE(Contains(s, box));
    }
}
 
TEST(GeometryContainment, FrustumContainment_NearFarPlanes)
{
    // Create frustum with specific near/far planes
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);
 
    // Box entirely within near/far range
    AABB boxInRange{{-0.5f, -0.5f, -5}, {0.5f, 0.5f, -3}};
    EXPECT_TRUE(Contains(f, boxInRange));
 
    // Box too close (before near plane)
    AABB boxTooClose{{-0.1f, -0.1f, -0.5f}, {0.1f, 0.1f, -0.3f}};
    EXPECT_FALSE(Contains(f, boxTooClose));
 
    // Box too far (beyond far plane)
    AABB boxTooFar{{-0.5f, -0.5f, -15}, {0.5f, 0.5f, -12}};
    EXPECT_FALSE(Contains(f, boxTooFar));
}