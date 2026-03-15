// tests/Test_GeometryQueries.cpp — Unit tests for Geometry::Queries module.
// Covers ClosestRaySegment, ClosestPointSegment, and ClosestPointRay.
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// ClosestRaySegment
// ============================================================================

TEST(GeometryQueries, ClosestRaySegment_Perpendicular)
{
    // Ray along +X, segment along +Y offset in Z.  The closest points should be
    // the ray origin and the segment point at t=0 (the endpoint nearest the ray).
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    auto res = ClosestRaySegment(ray, glm::vec3(0, 1, 0), glm::vec3(0, 2, 0));
    EXPECT_GE(res.RayT, 0.0f);
    EXPECT_GE(res.SegmentT, 0.0f);
    EXPECT_LE(res.SegmentT, 1.0f);
    EXPECT_NEAR(res.DistanceSq, 1.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestRaySegment_Crossing)
{
    // Ray along +X at y=0, segment from (0,-1,0) to (0,1,0).
    // They cross at the origin; expect zero distance.
    Ray ray{glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0)};
    auto res = ClosestRaySegment(ray, glm::vec3(0, -1, 0), glm::vec3(0, 1, 0));
    EXPECT_NEAR(res.DistanceSq, 0.0f, 1e-5f);
    EXPECT_NEAR(res.RayT, 1.0f, 1e-5f);
    EXPECT_NEAR(res.SegmentT, 0.5f, 1e-5f);
}

TEST(GeometryQueries, ClosestRaySegment_DegenerateSegment)
{
    // A zero-length segment should reduce to a point query.
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    auto res = ClosestRaySegment(ray, glm::vec3(2, 1, 0), glm::vec3(2, 1, 0));
    EXPECT_NEAR(res.RayT, 2.0f, 1e-4f);
    EXPECT_NEAR(res.DistanceSq, 1.0f, 1e-4f);
}

// ============================================================================
// ClosestPointSegment
// ============================================================================

TEST(GeometryQueries, ClosestPointSegment_PointOnSegment)
{
    // Point exactly on the segment midpoint.
    glm::vec3 p{1, 0, 0};
    auto res = ClosestPointSegment(p, glm::vec3(0, 0, 0), glm::vec3(2, 0, 0));
    EXPECT_NEAR(res.DistanceSq, 0.0f, 1e-6f);
    EXPECT_NEAR(res.SegmentT, 0.5f, 1e-6f);
    EXPECT_NEAR(glm::distance(res.ClosestPoint, p), 0.0f, 1e-6f);
}

TEST(GeometryQueries, ClosestPointSegment_PointOffSegment)
{
    // Point above the midpoint of a unit X-axis segment.
    glm::vec3 p{0.5f, 1.0f, 0};
    auto res = ClosestPointSegment(p, glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(res.DistanceSq, 1.0f, 1e-5f);
    EXPECT_NEAR(res.SegmentT, 0.5f, 1e-5f);
    EXPECT_NEAR(res.ClosestPoint.x, 0.5f, 1e-5f);
    EXPECT_NEAR(res.ClosestPoint.y, 0.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestPointSegment_PointBeyondEndpointA)
{
    // Point behind segment start; clamped to t=0.
    glm::vec3 p{-1.0f, 0, 0};
    auto res = ClosestPointSegment(p, glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(res.SegmentT, 0.0f, 1e-6f);
    EXPECT_NEAR(res.DistanceSq, 1.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(res.ClosestPoint, glm::vec3(0, 0, 0)), 0.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestPointSegment_PointBeyondEndpointB)
{
    // Point past segment end; clamped to t=1.
    glm::vec3 p{2.0f, 0, 0};
    auto res = ClosestPointSegment(p, glm::vec3(0, 0, 0), glm::vec3(1, 0, 0));
    EXPECT_NEAR(res.SegmentT, 1.0f, 1e-6f);
    EXPECT_NEAR(res.DistanceSq, 1.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(res.ClosestPoint, glm::vec3(1, 0, 0)), 0.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestPointSegment_DegenerateSegment)
{
    // Zero-length segment reduces to a point; result should be distance from
    // query point to that single point.
    glm::vec3 p{1, 0, 0};
    glm::vec3 seg{3, 0, 0};
    auto res = ClosestPointSegment(p, seg, seg);
    EXPECT_NEAR(res.DistanceSq, 4.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(res.ClosestPoint, seg), 0.0f, 1e-5f);
}

// ============================================================================
// ClosestPointRay
// ============================================================================

TEST(GeometryQueries, ClosestPointRay_PointOnRay)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    glm::vec3 p{3.0f, 0, 0};
    auto res = ClosestPointRay(p, ray);
    EXPECT_NEAR(res.DistanceSq, 0.0f, 1e-5f);
    EXPECT_NEAR(res.RayT, 3.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(res.ClosestPoint, p), 0.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestPointRay_PointOffRay)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    glm::vec3 p{2.0f, 3.0f, 0};
    auto res = ClosestPointRay(p, ray);
    EXPECT_NEAR(res.DistanceSq, 9.0f, 1e-4f);   // distance = 3 along Y
    EXPECT_NEAR(res.RayT, 2.0f, 1e-5f);
    EXPECT_NEAR(res.ClosestPoint.x, 2.0f, 1e-5f);
    EXPECT_NEAR(res.ClosestPoint.y, 0.0f, 1e-5f);
}

TEST(GeometryQueries, ClosestPointRay_PointBehindOrigin)
{
    // When the closest foot falls behind the ray origin, it is clamped to t=0.
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    glm::vec3 p{-2.0f, 1.0f, 0};
    auto res = ClosestPointRay(p, ray);
    EXPECT_NEAR(res.RayT, 0.0f, 1e-6f);
    EXPECT_NEAR(res.ClosestPoint.x, 0.0f, 1e-5f);
    // Distance should be point-to-origin.
    const float expectedDistSq = glm::dot(p, p);
    EXPECT_NEAR(res.DistanceSq, expectedDistSq, 1e-4f);
}

TEST(GeometryQueries, ClosestPointRay_OriginOnRay)
{
    // Query point at ray origin: closest point is the origin itself, t=0.
    Ray ray{glm::vec3(1, 2, 3), glm::vec3(0, 1, 0)};
    auto res = ClosestPointRay(ray.Origin, ray);
    EXPECT_NEAR(res.DistanceSq, 0.0f, 1e-6f);
    EXPECT_NEAR(res.RayT, 0.0f, 1e-6f);
}
