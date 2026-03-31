// tests/Test_SDF.cpp — Dedicated tests for SDF evaluation and SDFContact (TODO D18).
//
// Validates signed distance field math functions, stateful SDF functors,
// SDF factories, and the gradient-based contact solver.
#include <gtest/gtest.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

import Geometry;

using namespace Geometry;

static constexpr float kEps = 1e-3f;

// ============================================================================
// SDF::Math — Raw Math Functions
// ============================================================================

TEST(SDF_Math, Sphere_AtSurface)
{
    float d = SDF::Math::Sdf_Sphere(glm::vec3(1, 0, 0), 1.0f);
    EXPECT_NEAR(d, 0.0f, kEps);
}

TEST(SDF_Math, Sphere_Inside)
{
    float d = SDF::Math::Sdf_Sphere(glm::vec3(0.5f, 0, 0), 1.0f);
    EXPECT_LT(d, 0.0f);
}

TEST(SDF_Math, Sphere_Outside)
{
    float d = SDF::Math::Sdf_Sphere(glm::vec3(2, 0, 0), 1.0f);
    EXPECT_GT(d, 0.0f);
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, AABB_Inside)
{
    float d = SDF::Math::Sdf_Aabb(glm::vec3(0, 0, 0), glm::vec3(1, 1, 1));
    EXPECT_LT(d, 0.0f);
}

TEST(SDF_Math, AABB_OnFace)
{
    float d = SDF::Math::Sdf_Aabb(glm::vec3(1, 0, 0), glm::vec3(1, 1, 1));
    EXPECT_NEAR(d, 0.0f, kEps);
}

TEST(SDF_Math, AABB_Outside)
{
    float d = SDF::Math::Sdf_Aabb(glm::vec3(2, 0, 0), glm::vec3(1, 1, 1));
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, Capsule_OnSurface)
{
    // Capsule from (0,0,0) to (0,2,0), radius 1
    float d = SDF::Math::Sdf_Capsule(glm::vec3(1, 1, 0), glm::vec3(0, 0, 0), glm::vec3(0, 2, 0), 1.0f);
    EXPECT_NEAR(d, 0.0f, kEps);
}

TEST(SDF_Math, Capsule_Inside)
{
    float d = SDF::Math::Sdf_Capsule(glm::vec3(0, 1, 0), glm::vec3(0, 0, 0), glm::vec3(0, 2, 0), 1.0f);
    EXPECT_LT(d, 0.0f);
}

TEST(SDF_Math, Capsule_DegenerateSegmentFallback)
{
    float d = SDF::Math::Sdf_Capsule(glm::vec3(2, 0, 0), glm::vec3(0, 0, 0), glm::vec3(0, 0, 0), 1.0f);
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, Segment_DistanceToMidpoint)
{
    float d = SDF::Math::Sdf_Segment(glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, Segment_DegenerateToPointDistance)
{
    float d = SDF::Math::Sdf_Segment(glm::vec3(0, 3, 4), glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));
    EXPECT_NEAR(d, 5.0f, kEps);
}

TEST(SDF_Math, Plane_PositiveSide)
{
    float d = SDF::Math::Sdf_Plane(glm::vec3(0, 5, 0), glm::vec3(0, 1, 0), 0.0f);
    EXPECT_NEAR(d, 5.0f, kEps);
}

TEST(SDF_Math, Plane_NegativeSide)
{
    float d = SDF::Math::Sdf_Plane(glm::vec3(0, -3, 0), glm::vec3(0, 1, 0), 0.0f);
    EXPECT_NEAR(d, -3.0f, kEps);
}

TEST(SDF_Math, Triangle_AtVertex)
{
    float d = SDF::Math::Sdf_Triangle(
        glm::vec3(0, 0, 0),
        glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    EXPECT_NEAR(d, 0.0f, kEps);
}

TEST(SDF_Math, Triangle_AboveFace)
{
    float d = SDF::Math::Sdf_Triangle(
        glm::vec3(0.25f, 0.25f, 1.0f),
        glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0));
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, Triangle_DegenerateEdgeFallback)
{
    float d = SDF::Math::Sdf_Triangle(
        glm::vec3(0.5f, 1.0f, 0.0f),
        glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(2, 0, 0));
    EXPECT_NEAR(d, 1.0f, kEps);
}

TEST(SDF_Math, Ray_DistancePerpendicular)
{
    float d = SDF::Math::Sdf_Ray(glm::vec3(0, 3, 0), glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(d, 3.0f, kEps);
}

TEST(SDF_Math, Ray_BehindOrigin)
{
    // Point behind ray origin — clamped at origin
    float d = SDF::Math::Sdf_Ray(glm::vec3(-2, 0, 0), glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(d, 2.0f, kEps);
}

TEST(SDF_Math, Ray_ZeroDirectionFallsBackToPointDistance)
{
    float d = SDF::Math::Sdf_Ray(glm::vec3(0, 3, 4), glm::vec3(0, 0, 0), glm::vec3(0, 0, 0));
    EXPECT_NEAR(d, 5.0f, kEps);
}

// ============================================================================
// SDF Functors via CreateSDF
// ============================================================================

TEST(SDF_Functor, SphereSDF)
{
    Sphere s{glm::vec3(1, 0, 0), 2.0f};
    auto sdf = SDF::CreateSDF(s);
    EXPECT_NEAR(sdf(glm::vec3(3, 0, 0)), 0.0f, kEps);
    EXPECT_LT(sdf(glm::vec3(1, 0, 0)), 0.0f);
    EXPECT_GT(sdf(glm::vec3(5, 0, 0)), 0.0f);
}

TEST(SDF_Functor, AabbSDF)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    auto sdf = SDF::CreateSDF(box);
    EXPECT_LT(sdf(glm::vec3(0, 0, 0)), 0.0f); // Inside
    EXPECT_NEAR(sdf(glm::vec3(1, 0, 0)), 0.0f, kEps); // On surface
    EXPECT_GT(sdf(glm::vec3(2, 0, 0)), 0.0f); // Outside
}

TEST(SDF_Functor, ObbSDF_AxisAligned)
{
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), glm::quat(1, 0, 0, 0)};
    auto sdf = SDF::CreateSDF(obb);
    EXPECT_LT(sdf(glm::vec3(0, 0, 0)), 0.0f);
    EXPECT_GT(sdf(glm::vec3(2, 0, 0)), 0.0f);
}

TEST(SDF_Functor, CapsuleSDF)
{
    Capsule cap{glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 1.0f};
    auto sdf = SDF::CreateSDF(cap);
    EXPECT_LT(sdf(glm::vec3(0, 2, 0)), 0.0f); // Inside
    EXPECT_GT(sdf(glm::vec3(3, 2, 0)), 0.0f); // Outside
}

TEST(SDF_Functor, CylinderSDF)
{
    Cylinder cyl{glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 1.0f};
    auto sdf = SDF::CreateSDF(cyl);
    EXPECT_LT(sdf(glm::vec3(0, 2, 0)), 0.0f); // Center
}

TEST(SDF_Functor, CylinderSDF_Degenerate)
{
    // Degenerate cylinder (PointA == PointB) should not crash
    Cylinder cyl{glm::vec3(1, 1, 1), glm::vec3(1, 1, 1), 0.5f};
    auto sdf = SDF::CreateSDF(cyl);
    float d = sdf(glm::vec3(2, 1, 1));
    EXPECT_TRUE(std::isfinite(d));
}

TEST(SDF_Functor, TriangleSDF)
{
    Triangle tri{glm::vec3(0, 0, 0), glm::vec3(2, 0, 0), glm::vec3(1, 2, 0)};
    auto sdf = SDF::CreateSDF(tri);
    // Point on the triangle surface should be near -thickness
    float d = sdf(glm::vec3(1, 0.5f, 0));
    EXPECT_LT(d, 0.1f);
}

TEST(SDF_Functor, PlaneSDF)
{
    Plane pl{glm::vec3(0, 1, 0), 0.0f};
    auto sdf = SDF::CreateSDF(pl);
    EXPECT_NEAR(sdf(glm::vec3(0, 3, 0)), 3.0f, kEps);
    EXPECT_NEAR(sdf(glm::vec3(0, -2, 0)), -2.0f, kEps);
}

TEST(SDF_Functor, SegmentSDF)
{
    Segment seg{glm::vec3(0, 0, 0), glm::vec3(4, 0, 0)};
    auto sdf = SDF::CreateSDF(seg);
    EXPECT_NEAR(sdf(glm::vec3(2, 3, 0)), 3.0f, kEps);
}

TEST(SDF_Functor, ConvexHullSDF_Inside)
{
    ConvexHull hull;
    hull.Vertices = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                     {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}};
    // Build planes for the cube faces (outward normals)
    hull.Planes = {
        {glm::vec3( 1, 0, 0), -1.0f}, {glm::vec3(-1, 0, 0), -1.0f},
        {glm::vec3(0,  1, 0), -1.0f}, {glm::vec3(0, -1, 0), -1.0f},
        {glm::vec3(0, 0,  1), -1.0f}, {glm::vec3(0, 0, -1), -1.0f}
    };
    auto sdf = SDF::CreateSDF(hull);
    EXPECT_LT(sdf(glm::vec3(0, 0, 0)), 0.0f); // Inside
    EXPECT_GT(sdf(glm::vec3(2, 0, 0)), 0.0f); // Outside
}

TEST(SDF_Functor, ConvexHullSDF_Empty)
{
    ConvexHull hull;
    auto sdf = SDF::CreateSDF(hull);
    float d = sdf(glm::vec3(0, 0, 0));
    // Empty hull should return large positive distance
    EXPECT_GT(d, 1000.0f);
}

// ============================================================================
// SDFContact — Gradient-Based Contact Solver
// ============================================================================

TEST(SDFContact, CalculateGradient_Sphere)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto sdf = SDF::CreateSDF(s);
    glm::vec3 grad = SDF::CalculateGradient(glm::vec3(2, 0, 0), sdf);
    // Gradient should point outward from sphere center
    EXPECT_NEAR(grad.x, 1.0f, 0.01f);
    EXPECT_NEAR(grad.y, 0.0f, 0.01f);
    EXPECT_NEAR(grad.z, 0.0f, 0.01f);
}

TEST(SDFContact, Contact_TwoOverlappingSpheres)
{
    Sphere sa{glm::vec3(0, 0, 0), 2.0f};
    Sphere sb{glm::vec3(3, 0, 0), 2.0f};
    auto sdfA = SDF::CreateSDF(sa);
    auto sdfB = SDF::CreateSDF(sb);

    auto contact = SDF::Contact_General_SDF(sdfA, sdfB, glm::vec3(1.5f, 0, 0));
    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

TEST(SDFContact, Contact_SeparatedSpheres_ReturnsNullopt)
{
    Sphere sa{glm::vec3(0, 0, 0), 1.0f};
    Sphere sb{glm::vec3(10, 0, 0), 1.0f};
    auto sdfA = SDF::CreateSDF(sa);
    auto sdfB = SDF::CreateSDF(sb);

    // With guess at the midpoint (far from both surfaces), may not converge
    auto contact = SDF::Contact_General_SDF(sdfA, sdfB, glm::vec3(5, 0, 0));
    EXPECT_FALSE(contact.has_value());
}

TEST(SDFContact, Contact_SphereAABB)
{
    Sphere s{glm::vec3(0, 0, 0), 2.0f};
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    auto sdfA = SDF::CreateSDF(s);
    auto sdfB = SDF::CreateSDF(box);

    auto contact = SDF::Contact_General_SDF(sdfA, sdfB, glm::vec3(1, 0, 0));
    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}
