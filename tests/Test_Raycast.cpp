// tests/Test_Raycast.cpp — Watertight ray-triangle intersection tests.
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <optional>
#include <glm/glm.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Basic hit / miss
// ============================================================================

TEST(Raycast, RayHitsTriangleFrontFace)
{
    Ray ray{glm::vec3(0.25f, 0.25f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->T, 1.0f, 1e-5f);
    EXPECT_GE(hit->U, 0.0f);
    EXPECT_GE(hit->V, 0.0f);
    EXPECT_LE(hit->U + hit->V, 1.0f + 1e-5f);
}

TEST(Raycast, RayMissesTriangle)
{
    // Ray shoots past the triangle entirely.
    Ray ray{glm::vec3(2.0f, 2.0f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    EXPECT_FALSE(hit.has_value());
}

TEST(Raycast, RayHitsTriangleFromBehind)
{
    // Ray origin behind the triangle, pointing towards it.
    Ray ray{glm::vec3(0.25f, 0.25f, 1.0f), glm::vec3(0, 0, -1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->T, 1.0f, 1e-5f);
}

TEST(Raycast, RayParallelToTriangle)
{
    // Ray lies in the plane of the triangle — should miss.
    Ray ray{glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(1, 0, 0)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    EXPECT_FALSE(hit.has_value());
}

// ============================================================================
// Edge / corner hits (watertight property)
// ============================================================================

TEST(Raycast, RayHitsTriangleEdge)
{
    // Hit the midpoint of edge a-b at (0.5, 0, 0).
    Ray ray{glm::vec3(0.5f, 0.0f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    // Watertight intersection should report a valid hit on the edge.
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->T, 1.0f, 1e-5f);
}

TEST(Raycast, RayHitsTriangleVertex)
{
    // Hit vertex a at (0, 0, 0).
    Ray ray{glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->T, 1.0f, 1e-5f);
}

// ============================================================================
// Degenerate triangle (zero-area)
// ============================================================================

TEST(Raycast, DegenerateTriangleReturnsNullopt)
{
    Ray ray{glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0, 0, 1)};
    // Collinear vertices — degenerate triangle.
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{2, 0, 0};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    EXPECT_FALSE(hit.has_value());
}

// ============================================================================
// tMin / tMax bounds
// ============================================================================

TEST(Raycast, RespectsTMinBound)
{
    Ray ray{glm::vec3(0.25f, 0.25f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    // Triangle is at t=1.0, but tMin=1.5 — should miss.
    auto hit = RayTriangle_Watertight(ray, a, b, c, 1.5f);
    EXPECT_FALSE(hit.has_value());
}

TEST(Raycast, RespectsTMaxBound)
{
    Ray ray{glm::vec3(0.25f, 0.25f, -1.0f), glm::vec3(0, 0, 1)};
    glm::vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};

    // Triangle is at t=1.0, but tMax=0.5 — should miss.
    auto hit = RayTriangle_Watertight(ray, a, b, c, 0.0f, 0.5f);
    EXPECT_FALSE(hit.has_value());
}

// ============================================================================
// Barycentric coordinates
// ============================================================================

TEST(Raycast, BarycentricCoordinatesAtCentroid)
{
    // Hit the centroid of the triangle.
    const glm::vec3 a{0, 0, 0}, b{3, 0, 0}, c{0, 3, 0};
    const glm::vec3 centroid = (a + b + c) / 3.0f;
    Ray ray{centroid + glm::vec3(0, 0, -1), glm::vec3(0, 0, 1)};

    auto hit = RayTriangle_Watertight(ray, a, b, c);
    ASSERT_TRUE(hit.has_value());
    // At centroid, barycentrics should be approximately equal (~1/3 each).
    const float w = 1.0f - hit->U - hit->V;
    EXPECT_NEAR(hit->U, 1.0f / 3.0f, 0.05f);
    EXPECT_NEAR(hit->V, 1.0f / 3.0f, 0.05f);
    EXPECT_NEAR(w, 1.0f / 3.0f, 0.05f);
}
